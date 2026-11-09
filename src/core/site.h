#ifndef VFTP_CORE_SITE_H
#define VFTP_CORE_SITE_H

#include <stdint.h>

/*
 * Saved-site book. Fixed capacity, no allocation: the whole book lives in one
 * record the worker owns and saves. The first run seeds one editable anonymous
 * site; it is never auto-connected.
 */

#define SITE_NAME_MAX 64
#define SITE_HOST_MAX 128
#define SITE_USER_MAX 64
#define SITE_PASS_MAX 128
#define SITE_BOOK_MAX 32

typedef struct {
    char name[SITE_NAME_MAX];
    char host[SITE_HOST_MAX];
    uint16_t port;
    int anonymous;
    char username[SITE_USER_MAX];
    char password[SITE_PASS_MAX];
} Site;

typedef struct {
    Site items[SITE_BOOK_MAX];
    unsigned count;
    int parsed;  /* the header was accepted, or this is a fresh book */
    int partial; /* at least one line was dropped */
} SiteBook;

void site_book_init(SiteBook *book);
void site_book_clear(SiteBook *book);
void site_book_seed_example(SiteBook *book);

int site_book_add(SiteBook *book, const Site *site);
int site_book_update(SiteBook *book, unsigned index, const Site *site);
int site_book_remove(SiteBook *book, unsigned index);

const Site *site_book_at(const SiteBook *book, unsigned index);
unsigned site_book_count(const SiteBook *book);

#endif
