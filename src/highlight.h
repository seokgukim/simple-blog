#ifndef BLOG_HIGHLIGHT_H
#define BLOG_HIGHLIGHT_H
#include <stdio.h>
#include <stddef.h>
/* Write escaped code, optionally wrapped in lexical token spans. No tags around
 * the whole block; unsupported language labels produce escaped plain text. */
void highlight_html(FILE *out, const char *text, size_t size, const char *language, size_t language_size);
#endif
