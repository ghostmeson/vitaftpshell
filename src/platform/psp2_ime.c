/* SPDX-License-Identifier: GPL-3.0-only */

#include "platform/psp2_ime.h"

#include <string.h>

#include <psp2/apputil.h>
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
static int s_apputil_state; /* 0 = untried, 1 = ready, -1 = failed */
static int s_config_state;  /* 0 = untried, 1 = ready, -1 = failed */
static int s_last_error;
static int s_none_polls;
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

static int ime_ensure_apputil(void)
{
    if (s_apputil_state != 0)
        return s_apputil_state > 0;

    /* The SDK's IME sample initializes apputil before it sets the common-dialog
     * config. Without this, sceCommonDialogSetConfigParam can reject the call
     * and the IME never opens. This is the order the shipped sample proves. */
    SceAppUtilInitParam init;
    SceAppUtilBootParam boot;
    memset(&init, 0, sizeof(init));
    memset(&boot, 0, sizeof(boot));

    int rc = sceAppUtilInit(&init, &boot);
    if (rc < 0) {
        s_apputil_state = -1;
        s_last_error = rc;
        return 0;
    }

    s_apputil_state = 1;
    return 1;
}

static int ime_ensure_config(void)
{
    if (s_config_state != 0)
        return s_config_state > 0;
    if (!ime_ensure_apputil())
        return 0;

    /* sceCommonDialogConfigParamInit leaves language and enterButtonAssign at
     * MAX_VALUE; the dialog rejects a concrete request when those sentinels
     * reach it. Set the two values the console actually accepts. */
    SceCommonDialogConfigParam config;
    sceCommonDialogConfigParamInit(&config);
    config.language = SCE_SYSTEM_PARAM_LANG_ENGLISH_US;
    config.enterButtonAssign = SCE_SYSTEM_PARAM_ENTER_BUTTON_CROSS;

    int rc = sceCommonDialogSetConfigParam(&config);
    if (rc < 0) {
        s_config_state = -1;
        s_last_error = rc;
        return 0;
    }

    s_config_state = 1;
    return 1;
}

static ImeBeginResult ime_begin(void *ctx, const ImeRequest *request)
{
    (void)ctx;
    if (request == NULL)
        return IME_BEGIN_ERROR;
    if (s_active)
        return IME_BEGIN_ERROR;

    s_none_polls = 0;
    if (!ime_ensure_config())
        return IME_BEGIN_UNAVAILABLE;
    s_last_error = 0;

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
    /* The shipped SDK sample sets one forced language. Leaving the mask at zero
     * makes sceImeDialogInit reject the request as an invalid language. */
    param.supportedLanguages = SCE_IME_LANGUAGE_ENGLISH;
    param.languagesForced = SCE_TRUE;
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
        s_last_error = rc;
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
    if (status == SCE_COMMON_DIALOG_STATUS_RUNNING) {
        s_none_polls = 0;
        return IME_POLL_RUNNING;
    }

    if (status == SCE_COMMON_DIALOG_STATUS_NONE) {
        /* The dialog can report NONE for a frame or two after init; the common
         * dialog update in the draw path is what moves it to RUNNING. Treat a
         * short NONE window as still starting, not as a failure. The cap keeps
         * a dialog that never starts from holding the app forever. */
        if (++s_none_polls < 120)
            return IME_POLL_RUNNING;

        s_last_error = SCE_COMMON_DIALOG_ERROR_NOT_RUNNING;
        s_active = 0;
        ime_close_dialog();
        memset(s_input, 0, sizeof(s_input));
        return IME_POLL_ERROR;
    }

    s_none_polls = 0;
    if (status != SCE_COMMON_DIALOG_STATUS_FINISHED) {
        s_last_error = SCE_COMMON_DIALOG_ERROR_UNEXPECTED_FATAL;
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
        s_last_error = got_result < 0 ? got_result : terminated;
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
    s_none_polls = 0;
    /* The input and initial buffers may hold a password; leave no copy behind. */
    memset(s_input, 0, sizeof(s_input));
    memset(s_initial, 0, sizeof(s_initial));
}

static int ime_last_error(void *ctx)
{
    (void)ctx;
    return s_last_error;
}

TextInputPort *psp2_ime_port(void)
{
    s_port.ctx = NULL;
    s_port.begin = ime_begin;
    s_port.poll = ime_poll;
    s_port.end = ime_end;
    s_port.last_error = ime_last_error;
    return &s_port;
}
