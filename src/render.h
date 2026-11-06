#ifndef BLOG_RENDER_H
#define BLOG_RENDER_H
#include <stddef.h>
#include <stdio.h>

/* Validate before creating output, then render with the same Markdown dialect. */
void render_validate(const char *text, size_t size, const char *path);
void render_html(FILE *out, const char *text, size_t size, const char *path);
#endif
