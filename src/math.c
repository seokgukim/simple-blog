#include "math.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "itex2MML.h"

static void fail(const char *path, const char *message) {
    fprintf(stderr, "%s: LaTeX: %s\n", path, message);
    exit(1);
}
static int member(const char *list, const char *word) {
    size_t n = strlen(word);
    for (const char *p = list; *p;) {
        const char *end = strchr(p, ' ');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len == n && !memcmp(p, word, n)) return 1;
        p += len; if (*p) p++;
    }
    return 0;
}
static int name(const char **p, char word[32]) {
    size_t n = 0;
    while (isalnum((unsigned char)**p) || **p == '-' || **p == ':') {
        if (n == 31) return 0;
        word[n++] = *(*p)++;
    }
    word[n] = 0;
    return n != 0;
}
static void space(const char **p) {
    while (isspace((unsigned char)**p)) (*p)++;
}
/* itex includes SVG, links and other extensions. Accept only inert presentation
 * MathML, never arbitrary converter markup. In particular, no href, style,
 * event attributes, annotation-xml, mglyph, or foreign HTML/SVG content. */
static int safe_mathml(const char *p) {
    const char *tags = "math semantics annotation mrow mi mn mo mtext mspace "
        "msub msup msubsup munder mover munderover mmultiscripts mprescripts none "
        "mfrac msqrt mroot mtable mtr mtd mstyle mpadded mphantom menclose";
    const char *attrs = "xmlns display encoding mathvariant mathsize mathcolor mathbackground "
        "displaystyle scriptlevel stretchy symmetric largeop movablelimits fence separator "
        "form accent accentunder lspace rspace minsize maxsize width height depth "
        "linethickness bevelled numalign denomalign columnalign rowalign columnspacing "
        "rowspacing columnlines rowlines columnspan rowspan equalrows equalcolumns "
        "columnwidth frame framespacing align notation voffset";
    char stack[256][32]; size_t depth = 0; int roots = 0;
    while (*p) {
        if (*p != '<') {
            if (!depth && !isspace((unsigned char)*p)) return 0;
            p++; continue;
        }
        p++; int closing = *p == '/'; if (closing) p++;
        char tag[32];
        if (!name(&p, tag) || !member(tags, tag)) return 0;
        if (closing) {
            space(&p);
            if (*p++ != '>' || !depth || strcmp(stack[--depth], tag)) return 0;
            continue;
        }
        if (!depth && (roots++ || strcmp(tag, "math"))) return 0;
        while (*p && *p != '>' && *p != '/') {
            if (!isspace((unsigned char)*p)) return 0;
            space(&p); if (*p == '>' || *p == '/') break;
            char attr[32];
            if (!name(&p, attr) || !member(attrs, attr)) return 0;
            space(&p); if (*p++ != '=') return 0;
            space(&p); char quote = *p++;
            if (quote != '\'' && quote != '"') return 0;
            const char *value = p;
            while (*p && *p != quote && *p != '<') p++;
            if (*p != quote) return 0;
            if (!strcmp(attr, "xmlns") && ((size_t)(p - value) != 34 ||
                memcmp(value, "http://www.w3.org/1998/Math/MathML", 34))) return 0;
            p++;
        }
        int empty = *p == '/'; if (empty) p++;
        if (*p++ != '>') return 0;
        if (!empty) {
            if (depth == 256) return 0;
            strcpy(stack[depth++], tag);
        }
    }
    return roots == 1 && depth == 0;
}

char *math_html(const char *raw, size_t size, int display, const char *path) {
    if (size > 65536) fail(path, "expression exceeds 64 KiB");
    char *input = malloc(size * 2 + 5);
    if (!input) fail(path, "out of memory");
    /* itex accepts aligned, but not MathJax's align* inside math delimiters.
     * Normalize only the parser input, never the author's file. */
    size_t edge = display ? 2 : 1, n = edge;
    memset(input, '$', edge);
    for (size_t i = 0; i < size;) {
        if (size - i >= 14 && !memcmp(raw + i, "\\begin{align*}", 14)) {
            memcpy(input + n, "\\begin{aligned}", 15); n += 15; i += 14;
        } else if (size - i >= 12 && !memcmp(raw + i, "\\end{align*}", 12)) {
            memcpy(input + n, "\\end{aligned}", 13); n += 13; i += 12;
        } else {
            if (raw[i] == '\\' && i + 1 < size) input[n++] = raw[i++];
            input[n++] = raw[i++];
        }
    }
    memset(input + n, '$', edge); n += edge; input[n] = 0;
    char *html = itex2MML_parse(input, n);
    free(input);
    if (!html) fail(path, "unsupported or invalid expression (itex2MML)");
    if (!safe_mathml(html)) {
        itex2MML_free_string(html);
        fail(path, "unsafe or unsupported MathML markup");
    }
    return html;
}
