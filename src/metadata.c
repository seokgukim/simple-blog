#define _POSIX_C_SOURCE 200809L
#include "metadata.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yaml.h>

static void invalid(const char *path, const char *message) {
    fprintf(stderr, "%s: %s\n", path, message);
    exit(1);
}
static char *copy(const char *s) {
    char *p = strdup(s);
    if (!p) { perror("strdup"); exit(1); }
    return p;
}
static char *scalar(yaml_node_t *node, const char *path) {
    if (!node || node->type != YAML_SCALAR_NODE || !node->data.scalar.length ||
        memchr(node->data.scalar.value, 0, node->data.scalar.length))
        invalid(path, "metadata values must be nonempty strings (tags must be a list)");
    return (char *)node->data.scalar.value;
}
static int delimiter(const char *text, size_t start, size_t end) {
    if (end > start && text[end-1] == '\r') end--;
    return end - start == 3 && !memcmp(text + start, "---", 3);
}
static int valid_date(const char *s) {
    if (strlen(s) != 10 || s[4] != '-' || s[7] != '-') return 0;
    for (int i = 0; i < 10; i++)
        if (i != 4 && i != 7 && (s[i] < '0' || s[i] > '9')) return 0;
    int y = atoi(s), m = atoi(s + 5), d = atoi(s + 8);
    const int days[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
    if (y == 0 || m < 1 || m > 12) return 0;
    int leap = y % 4 == 0 && (y % 100 != 0 || y % 400 == 0);
    return d >= 1 && d <= days[m] + (m == 2 && leap);
}
Metadata metadata_read(const char *text, size_t size, const char *path, int about) {
    Metadata m = {0};
    if (memchr(text, 0, size)) invalid(path, "NUL byte in Markdown input");
    size_t start = size >= 3 && !memcmp(text, "\xef\xbb\xbf", 3) ? 3 : 0;
    m.body_offset = start;
    size_t line_end = start;
    while (line_end < size && text[line_end] != '\n') line_end++;
    if (delimiter(text, start, line_end)) {
        size_t yaml_start = line_end < size ? line_end + 1 : size;
        size_t end = yaml_start;
        int closed = 0;
        while (end < size) {
            line_end = end;
            while (line_end < size && text[line_end] != '\n') line_end++;
            if (delimiter(text, end, line_end)) {
                m.body_offset = line_end < size ? line_end + 1 : size;
                closed = 1;
                break;
            }
            end = line_end < size ? line_end + 1 : size;
        }
        if (!closed) invalid(path, "unclosed YAML front matter");
        yaml_parser_t parser;
        yaml_document_t doc;
        if (!yaml_parser_initialize(&parser)) invalid(path, "cannot initialize YAML parser");
        yaml_parser_set_input_string(&parser, (const unsigned char *)text + yaml_start, end - yaml_start);
        if (!yaml_parser_load(&parser, &doc)) {
            fprintf(stderr, "%s:%zu: invalid YAML: %s\n", path,
                    parser.problem_mark.line + 2, parser.problem ? parser.problem : "parse failure");
            exit(1);
        }
        yaml_node_t *root = yaml_document_get_root_node(&doc);
        if (!root || root->type != YAML_MAPPING_NODE) invalid(path, "front matter must be a YAML mapping");
        for (yaml_node_pair_t *p = root->data.mapping.pairs.start; p < root->data.mapping.pairs.top; p++) {
            char *key = scalar(yaml_document_get_node(&doc, p->key), path);
            for (yaml_node_pair_t *q = root->data.mapping.pairs.start; q < p; q++)
                if (!strcmp(key, scalar(yaml_document_get_node(&doc, q->key), path)))
                    invalid(path, "duplicate YAML field");
            yaml_node_t *value = yaml_document_get_node(&doc, p->value);
            if (!strcmp(key, "title")) m.title = copy(scalar(value, path));
            else if (!strcmp(key, "introduction")) m.introduction = copy(scalar(value, path));
            else if (!strcmp(key, "date")) {
                m.date = copy(scalar(value, path));
                if (!valid_date(m.date)) invalid(path, "date must be a valid YYYY-MM-DD calendar date");
            } else if (!strcmp(key, "tags")) {
                if (!value || value->type != YAML_SEQUENCE_NODE) invalid(path, "tags must be a YAML list");
                for (yaml_node_item_t *i = value->data.sequence.items.start; i < value->data.sequence.items.top; i++) {
                    char *tag = scalar(yaml_document_get_node(&doc, *i), path);
                    size_t j = 0;
                    while (j < m.tag_count && strcmp(m.tags[j], tag)) j++;
                    if (j < m.tag_count) continue;
                    char **tags = realloc(m.tags, (m.tag_count + 1) * sizeof(*tags));
                    if (!tags) invalid(path, "out of memory");
                    m.tags = tags; m.tags[m.tag_count++] = copy(tag);
                }
            }
        }
        yaml_document_delete(&doc);
        /* Reject an explicit second YAML document rather than ignoring it. */
        if (!yaml_parser_load(&parser, &doc) || yaml_document_get_root_node(&doc))
            invalid(path, "front matter must contain exactly one YAML document");
        yaml_document_delete(&doc);
        yaml_parser_delete(&parser);
    }
    if (about && (!m.title || !m.introduction))
        invalid(path, "about.md requires YAML title and introduction");
    return m;
}
void metadata_free(Metadata *m) {
    free(m->title); free(m->introduction); free(m->date);
    for (size_t i = 0; i < m->tag_count; i++) free(m->tags[i]);
    free(m->tags);
}
