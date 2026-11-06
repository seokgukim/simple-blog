#ifndef BLOG_METADATA_H
#define BLOG_METADATA_H
#include <stddef.h>

typedef struct {
    char *title;
    char *introduction;
    char *date;
    char **tags;
    size_t tag_count;
    size_t body_offset;
} Metadata;

/* Parses front matter and validates the blog's schema; exits on invalid input. */
Metadata metadata_read(const char *text, size_t size, const char *path, int about);
void metadata_free(Metadata *m);
#endif
