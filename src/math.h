#ifndef BLOG_MATH_H
#define BLOG_MATH_H
#include <stddef.h>
#include <stdio.h>
/* Convert a parsed formula to safe, self-contained MathML; caller frees it. */
char *math_html(const char *raw, size_t size, int display, const char *source_path);
#endif
