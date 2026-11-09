#include "core/site.h"

#include <string.h>

static int site_valid(const Site *site)
{
    if (site == NULL)
        return 0;
    if (site->name[0] == '\0' || site->host[0] == '\0')
        return 0;
    if (site->port == 0)
        return 0;
    return 1;
}

void site_book_clear(SiteBook *book)
{
    if (book == NULL)
        return;
    memset(book, 0, sizeof(*book));
}

void site_book_seed_example(SiteBook *book)
{
    if (book == NULL || book->count >= SITE_BOOK_MAX)
        return;

    Site *site = &book->items[book->count];
    memset(site, 0, sizeof(*site));
    strncpy(site->name, "ftp.gnu.org", sizeof(site->name) - 1);
    strncpy(site->host, "ftp.gnu.org", sizeof(site->host) - 1);
    site->port = 21;
    site->anonymous = 1;
    book->count++;
    book->parsed = 1;
}

void site_book_init(SiteBook *book)
{
    site_book_clear(book);
    site_book_seed_example(book);
}

int site_book_add(SiteBook *book, const Site *site)
{
    if (book == NULL || !site_valid(site) || book->count >= SITE_BOOK_MAX)
        return -1;

    book->items[book->count] = *site;
    unsigned index = book->count;
    book->count++;
    return (int)index;
}

int site_book_update(SiteBook *book, unsigned index, const Site *site)
{
    if (book == NULL || index >= book->count || !site_valid(site))
        return 0;
    book->items[index] = *site;
    return 1;
}

int site_book_remove(SiteBook *book, unsigned index)
{
    if (book == NULL || index >= book->count)
        return 0;

    for (unsigned i = index + 1; i < book->count; ++i)
        book->items[i - 1] = book->items[i];
    book->count--;
    memset(&book->items[book->count], 0, sizeof(book->items[0]));
    return 1;
}

const Site *site_book_at(const SiteBook *book, unsigned index)
{
    if (book == NULL || index >= book->count)
        return NULL;
    return &book->items[index];
}

unsigned site_book_count(const SiteBook *book)
{
    return book != NULL ? book->count : 0;
}
