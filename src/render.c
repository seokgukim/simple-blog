#include "render.h"
#include <stdlib.h>
#include <string.h>
#include "md4c-html.h"
#include "entity.h"
#include "math.h"
#include "highlight.h"

#define FLAGS (MD_DIALECT_GITHUB | MD_FLAG_NOHTML | MD_FLAG_LATEXMATHSPANS)
typedef struct { const char *path; int images; const char *input; } Validation;
typedef struct { FILE *out; const char *path; } Output;

static void fail(const char *path, const char *message) {
    fprintf(stderr, "%s: %s\n", path, message); exit(1);
}
/* Only the scheme is needed, but entities and ASCII controls must be decoded
 * just as a browser would, otherwise java&#9;script: bypasses the check. */
typedef struct { char text[16]; size_t len; int done, unsafe; } Scheme;
static void scheme_char(Scheme *s, unsigned c) {
    if (s->done || c <= 32 || c == 127) return;
    if (c == '/' || c == '?' || c == '#') { s->done = 1; return; }
    if (c == ':') {
        s->text[s->len] = 0;
        s->unsafe = strcmp(s->text, "http") && strcmp(s->text, "https") && strcmp(s->text, "mailto");
        s->done = 1; return;
    }
    if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    if (s->len < sizeof(s->text) - 1) s->text[s->len++] = c < 128 ? (char)c : '!';
}
static int unsafe_url(const MD_ATTRIBUTE *a) {
    Scheme s = {0};
    for (size_t i = 0; a->size && a->substr_offsets[i] < a->size && !s.done; i++) {
        MD_OFFSET begin = a->substr_offsets[i], end = a->substr_offsets[i+1];
        if (a->substr_types[i] == MD_TEXT_ENTITY) {
            const char *p = a->text + begin;
            if (end - begin >= 4 && p[1] == '#') {
                unsigned value = 0, base = 10;
                MD_OFFSET j = begin + 2;
                if (a->text[j] == 'x' || a->text[j] == 'X') { base = 16; j++; }
                for (; j < end && a->text[j] != ';'; j++) {
                    unsigned c = (unsigned char)a->text[j];
                    unsigned digit = c <= '9' ? c - '0' : (c | 32) - 'a' + 10;
                    value = value * base + digit;
                }
                scheme_char(&s, value);
            } else {
                const ENTITY *entity = entity_lookup(p, end - begin);
                if (entity) {
                    scheme_char(&s, entity->codepoints[0]);
                    if (entity->codepoints[1]) scheme_char(&s, entity->codepoints[1]);
                } else {
                    for (MD_OFFSET j = begin; j < end; j++) scheme_char(&s, (unsigned char)a->text[j]);
                }
            }
        } else {
            for (MD_OFFSET j = begin; j < end; j++) scheme_char(&s, (unsigned char)a->text[j]);
        }
    }
    return s.unsafe;
}
static int block(MD_BLOCKTYPE type, void *detail, void *ctx) {
    (void)type; (void)detail; (void)ctx; return 0;
}
static int enter_span(MD_SPANTYPE type, void *detail, void *ctx) {
    Validation *v = ctx;
    if (type == MD_SPAN_IMG) v->images++;
    if (!v->images && (type == MD_SPAN_LATEXMATH || type == MD_SPAN_LATEXMATH_DISPLAY)) {
        const MD_SPAN_LATEXMATH_DETAIL *range = detail;
        free(math_html(v->input + range->begin, range->end - range->begin,
                       type == MD_SPAN_LATEXMATH_DISPLAY, v->path));
    }
    const MD_ATTRIBUTE *url = NULL;
    if (type == MD_SPAN_A) url = &((MD_SPAN_A_DETAIL *)detail)->href;
    if (type == MD_SPAN_IMG) url = &((MD_SPAN_IMG_DETAIL *)detail)->src;
    if (url && unsafe_url(url)) fail(v->path, "unsupported URL scheme (only http, https, mailto or relative links allowed)");
    return 0;
}
static int leave_span(MD_SPANTYPE type, void *detail, void *ctx) {
    Validation *v = ctx;
    if (type == MD_SPAN_IMG) v->images--;
    (void)detail; return 0;
}
static int text(MD_TEXTTYPE type, const MD_CHAR *s, MD_SIZE size, void *ctx) {
    (void)type; (void)s; (void)size; (void)ctx; return 0;
}
void render_validate(const char *input, size_t size, const char *path) {
    if (size > (MD_SIZE)-1) fail(path, "file too large");
    MD_PARSER parser = {0};
    parser.flags = FLAGS;
    parser.enter_block = block; parser.leave_block = block;
    parser.enter_span = enter_span; parser.leave_span = leave_span; parser.text = text;
    Validation v = {path, 0, input};
    if (md_parse(input, (MD_SIZE)size, &parser, &v)) fail(path, "Markdown validation failed");
}
static void output(const MD_CHAR *text, MD_SIZE size, void *ctx) {
    Output *o = ctx;
    if (fwrite(text, 1, size, o->out) != size) { perror("write Markdown HTML"); exit(1); }
}
static void math_output(const MD_CHAR *raw, MD_SIZE size, int display, void *ctx) {
    Output *o = ctx;
    char *html = math_html(raw, size, display, o->path);
    fputs(html, o->out); free(html);
}
static void code_output(const MD_CHAR *text, MD_SIZE size, const MD_CHAR *lang, MD_SIZE lang_size, void *ctx) {
    Output *o = ctx;
    highlight_html(o->out, text, size, lang, lang_size);
}
void render_html(FILE *out, const char *text, size_t size, const char *path) {
    if (size > (MD_SIZE)-1) fail(path, "file too large");
    Output o = {out, path};
    if (md_html_custom(text, (MD_SIZE)size, output, &o, FLAGS, 0, math_output, code_output))
        fail(path, "Markdown rendering failed");
}
