/* SPDX-License-Identifier: GPL-3.0-only */

#include "platform/psp2_ime.h"

#include <string.h>

#include <psp2/common_dialog.h>
#include <psp2/ime_dialog.h>

/*
 * The one adapter that opens the system text entry. The dialog is polled once
 * per frame so the UI keeps drawing; the result is UTF-8 or cancellation. A
 * password is never logged: the text lives in the static buffer below and in
 * the caller's ImeResponse, and nowhere else.
 */

#define PSP2_IME_TITLE_CAP (SCE_IME_DIALOG_MAX_TITLE_LENGTH + 1)
#define PSP2_IME_INITIAL_CAP IME_TEXT_MAX
#define PSP2_IME_INPUT_CAP (SCE_IME_DIALOG_MAX_TEXT_LENGTH + 1)

static SceWChar16 s_title[PSP2_IME_TITLE_CAP];
static SceWChar16 s_initial[PSP2_IME_INITIAL_CAP];
static SceWChar16 s_input[PSP2_IME_INPUT_CAP];
static int s_active;
static int s_config_state; /* 0 = untried, 1 = ready, -1 = failed */
static TextInputPort s_port;

/* Bounded UTF-8 to UTF-16LE. Invalid input becomes U+FFFD; a value that does
 * not fit is truncated at a code-point boundary and the result is terminated. */
static void ime_utf8_to_utf16(const char *src, SceWChar16 *dst, size_t cap)
{
    size_t out = 0;
    if (dst == NULL || cap == 0)
        return;

    dst[0] = 0;
    if (src == NULL)
        return;

    const unsigned char *p = (const unsigned char *)src;
    while (*p != 0 && out + 1 < cap) {
        uint32_t code;
        size_t need;

        if (*p < 0x80) {
            code = *p;
            need = 1;
        } else if ((*p & 0xE0) == 0xC0) {
            code = (uint32_t)(*p & 0x1Fu);
            need = 2;
        } else if ((*p & 0xF0) == 0xE0) {
            code = (uint32_t)(*p & 0x0Fu);
            need = 3;
        } else if ((*p & 0xF8) == 0xF0) {
            code = (uint32_t)(*p & 0x07u);
            need = 4;
        } else {
            code = 0xFFFD;
            need = 1;
        }

        if (need > 1) {
            for (size_t i = 1; i < need; ++i) {
                if ((p[i] & 0xC0) != 0x80) {
                    code = 0xFFFD;
                    need = 1;
                    break;
                }
                code = (code << 6) | (uint32_t)(p[i] & 0x3Fu);
            }
        }

        if ((need == 2 && code < 0x80) || (need == 3 && code < 0x800) ||
            (need == 4 && (code < 0x10000 || code > 0x10FFFF)) ||
            (code >= 0xD800 && code <= 0xDFFF))
            code = 0xFFFD;

        p += need;

        if (code >= 0x10000) {
            if (out + 2 >= cap)
                break;
            code -= 0x10000;
            dst[out++] = (SceWChar16)(0xD800 + (code >> 10));
            dst[out++] = (SceWChar16)(0xDC00 + (code & 0x3FF));
        } else {
            dst[out++] = (SceWChar16)code;
        }
    }

    dst[out] = 0;
}

/* Bounded UTF-16LE to UTF-8. A lone surrogate becomes U+FFFD; a value that
 * does not fit is truncated at a code-point boundary and the result is
 * terminated. Returns the byte count written, excluding the terminator. */
static size_t ime_utf16_to_utf8(const SceWChar16 *src, char *dst, size_t cap)
{
    size_t out = 0;
    if (dst == NULL || cap == 0)
        return 0;

    dst[0] = '\0';
    if (src == NULL)
        return 0;

    for (size_t i = 0; src[i] != 0; ++i) {
        uint32_t code = src[i];

        if (code >= 0xD800 && code <= 0xDBFF && src[i + 1] >= 0xDC00 &&
            src[i + 1] <= 0xDFFF) {
            code = 0x10000 + ((code - 0xD800) << 10) +
                   ((uint32_t)src[++i] - 0xDC00);
        } else if (code >= 0xD800 && code <= 0xDFFF) {
            code = 0xFFFD;
        }

        unsigned char encoded[4];
        size_t len;

        if (code < 0x80) {
            encoded[0] = (unsigned char)code;
            len = 1;
        } else if (code < 0x800) {
            encoded[0] = (unsigned char)(0xC0 | (code >> 6));
            encoded[1] = (unsigned char)(0x80 | (code & 0x3F));
            len = 2;
        } else if (code < 0x10000) {
            encoded[0] = (unsigned char)(0xE0 | (code >> 12));
            encoded[1] = (unsigned char)(0x80 | ((code >> 6) & 0x3F));
            encoded[2] = (unsigned char)(0x80 | (code & 0x3F));
            len = 3;
        } else {
            encoded[0] = (unsigned char)(0xF0 | (code >> 18));
            encoded[1] = (unsigned char)(0x80 | ((code >> 12) & 0x3F));
            encoded[2] = (unsigned char)(0x80 | ((code >> 6) & 0x3F));
            encoded[3] = (unsigned char)(0x80 | (code & 0x3F));
            len = 4;
        }

        if (out + len + 1 > cap)
            break;

        memcpy(dst + out, encoded, len);
        out += len;
    }

    dst[out] = '\0';
    return out;
}

static int ime_ensure_config(void)
{
    if (s_config_state != 0)
        return s_config_state > 0;

    /* [C] The common-dialog config is assumed to be set once, before any dialog
     * is created. The IME initialization order is an open decision in
     * AGENTS.md, so this sequence has not run on hardware. */
    SceCommonDialogConfigParam config;
    sceCommonDialogConfigParamInit(&config);
    s_config_state = sceCommonDialogSetConfigParam(&config) >= 0 ? 1 : -1;
    return s_config_state > 0;
}

static ImeBeginResult ime_begin(void *ctx, const ImeRequest *request)
{
    (void)ctx;
    if (request == NULL)
        return IME_BEGIN_ERROR;
    if (s_active)
        return IME_BEGIN_ERROR;
    if (!ime_ensure_config())
        return IME_BEGIN_UNAVAILABLE;

    ime_utf8_to_utf16(request->title != NULL ? request->title : "", s_title,
                      PSP2_IME_TITLE_CAP);
    memset(s_initial, 0, sizeof(s_initial));
    ime_utf8_to_utf16(request->initial != NULL ? request->initial : "",
                      s_initial, PSP2_IME_INITIAL_CAP);
    memset(s_input, 0, sizeof(s_input));

    /* Clamp to both the request and the dialog's hard limit. A zero request is
     * passed through; the platform decides whether that dialog is valid. */
    SceUInt32 max = SCE_IME_DIALOG_MAX_TEXT_LENGTH;
    if (request->max_length < (size_t)SCE_IME_DIALOG_MAX_TEXT_LENGTH)
        max = (SceUInt32)request->max_length;

    SceImeDialogParam param;
    sceImeDialogParamInit(&param);
    param.type = SCE_IME_TYPE_DEFAULT;
    param.dialogMode = SCE_IME_DIALOG_DIALOG_MODE_WITH_CANCEL;
    param.textBoxMode = request->password
                            ? SCE_IME_DIALOG_TEXTBOX_MODE_PASSWORD
                            : SCE_IME_DIALOG_TEXTBOX_MODE_DEFAULT;
    param.title = s_title;
    param.initialText = s_initial;
    param.maxTextLength = max;
    param.inputTextBuffer = s_input;

    /* [C] Password masking through TEXTBOX_MODE_PASSWORD is unverified on
     * hardware. The typed text is never logged and never leaves this adapter
     * except through poll()'s ImeResponse. */
    int rc = sceImeDialogInit(&param);
    if (rc < 0) {
        memset(s_input, 0, sizeof(s_input));
        return IME_BEGIN_UNAVAILABLE;
    }

    s_active = 1;
    return IME_BEGIN_RUNNING;
}

static void ime_close_dialog(void)
{
    /* Both calls have no error channel in this contract. Abort is issued first
     * so Term never waits on a dialog that is still running; a call that fails
     * because nothing is open is harmless. */
    int aborted = sceImeDialogAbort();
    int terminated = sceImeDialogTerm();
    (void)aborted;
    (void)terminated;
}

static ImePollResult ime_poll(void *ctx, ImeResponse *out)
{
    (void)ctx;
    if (!s_active || out == NULL)
        return IME_POLL_ERROR;

    SceCommonDialogStatus status = sceImeDialogGetStatus();
    if (status == SCE_COMMON_DIALOG_STATUS_RUNNING)
        return IME_POLL_RUNNING;

    if (status == SCE_COMMON_DIALOG_STATUS_NONE) {
        /* [C] Whether the dialog needs a frame between init and the first
         * status query is unverified. NONE while this adapter believes the
         * dialog is open is treated as an error rather than assumed running. */
        s_active = 0;
        ime_close_dialog();
        memset(s_input, 0, sizeof(s_input));
        return IME_POLL_ERROR;
    }

    if (status != SCE_COMMON_DIALOG_STATUS_FINISHED) {
        s_active = 0;
        ime_close_dialog();
        memset(s_input, 0, sizeof(s_input));
        return IME_POLL_ERROR;
    }

    SceImeDialogResult result;
    memset(&result, 0, sizeof(result));

    int got_result = sceImeDialogGetResult(&result);
    int terminated = sceImeDialogTerm();

    if (got_result < 0 || terminated < 0) {
        /* Keep the active flag only when Term failed: the dialog may still be
         * open, and end() can retry the abort/term pair. A closed dialog is
         * never reported as active. */
        s_active = terminated < 0 ? 1 : 0;
        memset(s_input, 0, sizeof(s_input));
        return IME_POLL_ERROR;
    }

    s_active = 0;

    if (result.result == SCE_COMMON_DIALOG_RESULT_OK) {
        ime_utf16_to_utf8(s_input, out->text, sizeof(out->text));
        memset(s_input, 0, sizeof(s_input));
        return IME_POLL_DONE;
    }

    memset(s_input, 0, sizeof(s_input));

    if (result.result == SCE_COMMON_DIALOG_RESULT_USER_CANCELED ||
        result.result == SCE_COMMON_DIALOG_RESULT_ABORTED)
        return IME_POLL_CANCELLED;

    return IME_POLL_ERROR;
}

static void ime_end(void *ctx)
{
    (void)ctx;

    if (s_active)
        ime_close_dialog();

    s_active = 0;
    /* The input and initial buffers may hold a password; leave no copy behind. */
    memset(s_input, 0, sizeof(s_input));
    memset(s_initial, 0, sizeof(s_initial));
}

TextInputPort *psp2_ime_port(void)
{
    s_port.ctx = NULL;
    s_port.begin = ime_begin;
    s_port.poll = ime_poll;
    s_port.end = ime_end;
    return &s_port;
}
