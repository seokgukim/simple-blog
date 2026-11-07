#define _XOPEN_SOURCE 700
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include "render.h"
#include "metadata.h"

#ifndef BLOG_PROJECT_DIR
#define BLOG_PROJECT_DIR "."
#endif

typedef struct {
    char *path, *html, *text;
    size_t size;
    Metadata meta;
} Page;
typedef struct {
    Page about;
    Page *posts;
    size_t count;
} Site;

/* GCC 14 treats this CLI's deliberate allocation/I/O error exit as a tainted
 * assertion failure. Suppress only that diagnostic at this error handler;
 * keep all other analyzer checks, including bounds and taint checks, enabled. */
#if defined(__GNUC__) && __GNUC__ >= 14 && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wanalyzer-tainted-assertion"
#endif
static void die(const char *s) { perror(s); exit(1); }
#if defined(__GNUC__) && __GNUC__ >= 14 && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
static void fail(const char *path, const char *message) {
    fprintf(stderr, "%s: %s\n", path, message); exit(1);
}
static void *alloc(size_t n) {
    void *p = malloc(n); if (!p) die("malloc"); return p;
}
static char *copy(const char *s) {
    char *p = strdup(s); if (!p) die("strdup"); return p;
}
static char *join(const char *a, const char *b) {
    size_t n = strlen(a) + strlen(b) + 2;
    char *p = alloc(n); snprintf(p, n, "%s/%s", a, b); return p;
}
static void mkdir_one(const char *p) {
    struct stat st;
    if (mkdir(p, 0777) && (errno != EEXIST || lstat(p, &st) || !S_ISDIR(st.st_mode))) die(p);
}
/* Resolve a destination before creating it, including symlinked parents. */
static char *destination_path(const char *path) {
    char *resolved = realpath(path, NULL);
    if (resolved) return resolved;
    if (errno != ENOENT) die(path);
    char *buf = copy(path);
    size_t n = strlen(buf);
    while (n > 1 && buf[n-1] == '/') buf[--n] = 0;
    char *slash = strrchr(buf, '/');
    char *base = copy(slash ? slash + 1 : buf);
    if (slash) { if (slash == buf) slash[1] = 0; else *slash = 0; }
    char *parent = realpath(slash ? buf : ".", NULL); if (!parent) die(path);
    resolved = join(parent, base);
    free(parent); free(base); free(buf); return resolved;
}
static int contains_path(const char *parent, const char *path) {
    size_t n = strlen(parent);
    if (n == 1 && parent[0] == '/') return path[0] == '/';
    return !strncmp(parent, path, n) && (path[n] == '/' || path[n] == 0);
}
static void check_empty(const char *path) {
    DIR *d = opendir(path);
    if (!d) { if (errno == ENOENT) return; die(path); }
    struct dirent *e;
    for (;;) {
        errno = 0; e = readdir(d);
        if (!e) { if (errno) die(path); break; }
        if (strcmp(e->d_name, ".") && strcmp(e->d_name, ".."))
            fail(path, "output must be a new or empty directory");
    }
    if (closedir(d)) die(path);
}
static char *read_all(const char *path, size_t *size) {
    int fd = open(path, O_RDONLY | O_NOFOLLOW); if (fd < 0) die(path);
    struct stat st; if (fstat(fd, &st)) die(path);
    if (!S_ISREG(st.st_mode)) fail(path, "expected a regular file");
    FILE *f = fdopen(fd, "rb"); if (!f) die(path);
    size_t len = 0, cap = 4096; char *p = alloc(cap);
    for (;;) {
        if (len == cap) {
            cap *= 2; char *q = realloc(p, cap); if (!q) die("realloc"); p = q;
        }
        size_t n = fread(p + len, 1, cap - len, f); len += n;
        if (!n) { if (ferror(f)) die(path); break; }
    }
    if (fclose(f)) die(path);
    char *q = realloc(p, len + 1); if (!q) die("realloc"); q[len] = 0; *size = len; return q;
}
static Page load_page(const char *root, const char *rel, int about) {
    Page p = {0};
    p.path = copy(rel);
    char *path = join(root, rel);
    p.text = read_all(path, &p.size);
    p.meta = metadata_read(p.text, p.size, path, about);
    /* Missing post titles fall back to the relative Markdown path. */
    if (!p.meta.title) p.meta.title = copy(rel);
    render_validate(p.text + p.meta.body_offset, p.size - p.meta.body_offset, path);
    free(path);
    size_t n = strlen(rel);
    /* Keep the suffix-removal precondition local, not only in discover(). */
    if (n <= 3 || strcmp(rel + n - 3, ".md"))
        fail(rel, "expected a Markdown filename ending in .md");
    if (n > SIZE_MAX - 3) fail(rel, "filename too long");
    p.html = alloc(n + 3);
    memcpy(p.html, rel, n);
    memcpy(p.html + n - 3, ".html", sizeof(".html"));
    return p;
}
static void discover(Site *site, const char *root, const char *rel) {
    char *dir = *rel ? join(root, rel) : copy(root);
    DIR *d = opendir(dir); if (!d) die(dir);
    for (;;) {
        errno = 0; struct dirent *e = readdir(d);
        if (!e) { if (errno) die(dir); break; }
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char *r = *rel ? join(rel, e->d_name) : copy(e->d_name);
        char *path = join(root, r); struct stat st;
        if (lstat(path, &st)) die(path);
        if (S_ISDIR(st.st_mode)) discover(site, root, r);
        else if (S_ISREG(st.st_mode)) {
            size_t n = strlen(r);
            if (n > 3 && !strcmp(r + n - 3, ".md") && strcmp(r, "about.md")) {
                Page p = load_page(root, r, 0);
                if (!strcmp(p.html, "index.html") || !strcmp(p.html, "posts.html") ||
                    contains_path("tags", p.html) || contains_path("public", p.html) ||
                    contains_path("about.html", p.html) || contains_path("index.html", p.html) ||
                    contains_path("posts.html", p.html) || contains_path("tags.html", p.html))
                    fail(r, "conflicts with a reserved site path");
                Page *posts = realloc(site->posts, (site->count + 1) * sizeof(*posts));
                if (!posts) die("realloc");
                site->posts = posts; site->posts[site->count++] = p;
            }
        }
        /* Symlinks are deliberately not followed. */
        free(path); free(r);
    }
    if (closedir(d)) die(dir);
    free(dir);
}
static int post_order(const void *a, const void *b) {
    const Page *x = a, *y = b;
    int order = strcmp(y->meta.date ? y->meta.date : "", x->meta.date ? x->meta.date : "");
    return order ? order : strcmp(x->path, y->path);
}
static void escaped(FILE *f, const char *s) {
    for (; *s; s++) switch (*s) {
        case '&': fputs("&amp;", f); break; case '<': fputs("&lt;", f); break;
        case '>': fputs("&gt;", f); break; case '"': fputs("&quot;", f); break;
        default: fputc((unsigned char)*s, f);
    }
}
static void url(FILE *f, const char *s) {
    static const char hex[] = "0123456789ABCDEF";
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || strchr("-._~/", c)) fputc(c, f);
        else { fputc('%', f); fputc(hex[c >> 4], f); fputc(hex[c & 15], f); }
    }
}
/* Root-relative site paths are emitted relative to the current HTML page. */
static void link(FILE *f, const char *current, const char *target, const char *label) {
    fputs("<a href=\"", f);
    for (const char *s = current; *s; s++) if (*s == '/') fputs("../", f);
    url(f, target); fputs("\">", f); escaped(f, label); fputs("</a>", f);
}
static FILE *open_output(const char *root, const char *rel) {
    char *path = join(root, rel);
    for (char *p = path + strlen(root) + 1; *p; p++)
        if (*p == '/') { *p = 0; mkdir_one(path); *p = '/'; }
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0666);
    if (fd < 0) die(path);
    FILE *f = fdopen(fd, "wb"); if (!f) die(path);
    free(path); return f;
}
static FILE *begin_page(const Site *site, const char *root, const char *rel, const char *title) {
    FILE *f = open_output(root, rel);
    fputs("<!doctype html>\n<html><head><meta charset=\"utf-8\"><title>", f);
    escaped(f, title);
    fputs("</title><meta property=\"og:site_name\" content=\"", f);
    escaped(f, site->about.meta.title);
    fputs("\"><meta property=\"og:title\" content=\"", f);
    escaped(f, title);
    fputs("\"><meta property=\"og:type\" content=\"website\"><meta property=\"og:image\" content=\"", f);
    for (const char *s = rel; *s; s++) if (*s == '/') fputs("../", f);
    fputs("public/logo.svg\"><link rel=\"icon\" type=\"image/svg+xml\" href=\"", f);
    for (const char *s = rel; *s; s++) if (*s == '/') fputs("../", f);
    fputs("public/logo.svg\"><link rel=\"stylesheet\" href=\"", f);
    for (const char *s = rel; *s; s++) if (*s == '/') fputs("../", f);
    fputs("public/code.css\"></head><body>\n<header>\n<h1 align=\"center\">", f);
    link(f, rel, "index.html", site->about.meta.title);
    fputs("</h1>\n<p align=\"center\">", f); escaped(f, site->about.meta.introduction);
    fputs("</p>\n<nav aria-label=\"Main\"><p align=\"center\">", f);
    link(f, rel, "about.html", "About"); fputc(' ', f);
    link(f, rel, "posts.html", "Post"); fputc(' ', f);
    link(f, rel, "tags.html", "Tag");
    fputs("</p></nav>\n</header>\n<hr>\n<main>\n", f);
    return f;
}
static void end_page(FILE *f, const Site *site) {
    fputs("</main>\n", f);
    if (site->about.meta.author) {
        time_t now = time(NULL);
        struct tm *local = localtime(&now);
        if (!local) { fprintf(stderr, "cannot determine current year\n"); exit(1); }
        fprintf(f, "<hr>\n<footer>© %d ", local->tm_year + 1900);
        escaped(f, site->about.meta.author);
        fputs("</footer>\n", f);
    }
    fputs("</body></html>\n", f);
    if (ferror(f) || fclose(f)) die("write HTML");
}
static void markdown(FILE *f, const Page *p) {
    render_html(f, p->text + p->meta.body_offset, p->size - p->meta.body_offset, p->path);
}
static void post_item(FILE *f, const char *current, const Page *p) {
    fputs("<li>", f); link(f, current, p->html, p->meta.title);
    if (p->meta.date) { fputs(" <time>", f); escaped(f, p->meta.date); fputs("</time>", f); }
    fputs("</li>\n", f);
}
/* Preserve UTF-8 names; escape separators, controls and the escape marker.
 * URL escaping is a separate step, so a literal %2F tag cannot collide with /. */
static char *tag_path(const char *tag) {
    static const char hex[] = "0123456789ABCDEF";
    size_t n = strlen(tag), end = 5;
    if (n > 250) fail(tag, "tag filename exceeds 250 bytes before .html");
    char *path = alloc(3 * n + 11);
    memcpy(path, "tags/", 5);
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)tag[i];
        if (c < 32 || c == 127 || c == '/' || c == '\\' || c == '%') {
            path[end++] = '%'; path[end++] = hex[c >> 4]; path[end++] = hex[c & 15];
        } else path[end++] = (char)c;
    }
    if (end - 5 > 250) fail(tag, "tag filename exceeds 250 bytes after escaping");
    strcpy(path + end, ".html"); return path;
}
static int has_tag(const Page *p, const char *tag) {
    for (size_t i = 0; i < p->meta.tag_count; i++)
        if (!strcmp(p->meta.tags[i], tag)) return 1;
    return 0;
}
static int tag_order(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}
static void write_tags(const Site *site, const char *dest) {
    char **tags = NULL;
    size_t count = 0;
    for (size_t i = 0; i < site->count; i++) {
        const Metadata *m = &site->posts[i].meta;
        for (size_t j = 0; j < m->tag_count; j++) {
            size_t k = 0;
            while (k < count && strcmp(tags[k], m->tags[j])) k++;
            if (k < count) continue;
            char **more = realloc(tags, (count + 1) * sizeof(*more));
            if (!more) die("realloc");
            tags = more; tags[count++] = m->tags[j];
        }
    }
    if (count > 1) qsort(tags, count, sizeof(*tags), tag_order);
    FILE *index = begin_page(site, dest, "tags.html", "Tags");
    fputs("<h2>Tags</h2>\n<ul>\n", index);
    for (size_t i = 0; i < count; i++) {
        char *path = tag_path(tags[i]);
        fputs("<li>", index); link(index, "tags.html", path, tags[i]); fputs("</li>\n", index);
        FILE *f = begin_page(site, dest, path, tags[i]);
        fputs("<h2>", f); escaped(f, tags[i]); fputs("</h2>\n<ul>\n", f);
        for (size_t j = 0; j < site->count; j++)
            if (has_tag(&site->posts[j], tags[i])) post_item(f, path, &site->posts[j]);
        fputs("</ul>\n", f); end_page(f, site); free(path);
    }
    fputs("</ul>\n", index); end_page(index, site); free(tags);
}
static void write_site(const Site *site, const char *dest, const char *logo, size_t logo_size,
                       const char *css, size_t css_size) {
    mkdir_one(dest);
    FILE *f = open_output(dest, "public/logo.svg");
    if (fwrite(logo, 1, logo_size, f) != logo_size || fclose(f)) die("write logo");
    f = open_output(dest, "public/code.css");
    if (fwrite(css, 1, css_size, f) != css_size || fclose(f)) die("write code stylesheet");
    f = begin_page(site, dest, "index.html", site->about.meta.title);
    fputs("<p align=\"center\"><img src=\"public/logo.svg\" alt=\"Blog logo\"></p>\n<h2>Recent post</h2>\n", f);
    if (site->count) { fputs("<ul>\n", f); post_item(f, "index.html", &site->posts[0]); fputs("</ul>\n", f); }
    else fputs("<p>No posts yet.</p>\n", f);
    end_page(f, site);
    f = begin_page(site, dest, "about.html", site->about.meta.title);
    markdown(f, &site->about); end_page(f, site);
    f = begin_page(site, dest, "posts.html", "Posts");
    fputs("<h2>Posts</h2>\n<ul>\n", f);
    for (size_t i = 0; i < site->count; i++) post_item(f, "posts.html", &site->posts[i]);
    fputs("</ul>\n", f); end_page(f, site);
    write_tags(site, dest);
    for (size_t i = 0; i < site->count; i++) {
        const Page *p = &site->posts[i];
        f = begin_page(site, dest, p->html, p->meta.title);
        fputs("<article>\n<h2>", f); escaped(f, p->meta.title); fputs("</h2>\n", f);
        if (p->meta.date) { fputs("<p><time>", f); escaped(f, p->meta.date); fputs("</time></p>\n", f); }
        if (p->meta.tag_count) {
            fputs("<p>Tags: ", f);
            for (size_t j = 0; j < p->meta.tag_count; j++) {
                char *path = tag_path(p->meta.tags[j]);
                if (j) fputs(", ", f);
                link(f, p->html, path, p->meta.tags[j]); free(path);
            }
            fputs("</p>\n", f);
        }
        markdown(f, p); fputs("</article>\n", f); end_page(f, site);
    }
}
static void free_page(Page *p) {
    free(p->path); free(p->html); free(p->text); metadata_free(&p->meta);
}
int main(int argc, char **argv) {
    if (argc != 2 && argc != 3) {
        fprintf(stderr, "Usage: %s TARGET_DIR [OUTPUT_DIR]\nDefault output: %s/export/\n", argv[0], BLOG_PROJECT_DIR);
        return 2;
    }
    struct stat st;
    if (stat(argv[1], &st) || !S_ISDIR(st.st_mode)) fail(argv[1], "invalid source directory");
    char *source = realpath(argv[1], NULL); if (!source) die(argv[1]);
    char *destination = destination_path(argc == 3 ? argv[2] : BLOG_PROJECT_DIR "/export");
    if (contains_path(source, destination) || contains_path(destination, source))
        fail(destination, "source and output directories must not overlap");
    check_empty(destination);
    Site site = {0};
    site.about = load_page(source, "about.md", 1);
    discover(&site, source, "");
    if (site.count > 1) qsort(site.posts, site.count, sizeof(*site.posts), post_order);
    for (size_t i = 0; i < site.count; i++)
        for (size_t j = i + 1; j < site.count; j++)
            if (contains_path(site.posts[i].html, site.posts[j].html) ||
                contains_path(site.posts[j].html, site.posts[i].html))
                fail(site.posts[j].path, "conflicting output paths");
    for (size_t i = 0; i < site.count; i++)
        for (size_t j = 0; j < site.posts[i].meta.tag_count; j++) {
            char *path = tag_path(site.posts[i].meta.tags[j]); free(path);
        }
    size_t logo_size; char *logo = read_all(BLOG_PROJECT_DIR "/public/logo.svg", &logo_size);
    size_t css_size; char *css = read_all(BLOG_PROJECT_DIR "/public/code.css", &css_size);
    write_site(&site, destination, logo, logo_size, css, css_size);
    free(css);
    printf("Built %zu posts in %s\n", site.count, destination);
    free(logo); free_page(&site.about);
    for (size_t i = 0; i < site.count; i++) free_page(&site.posts[i]);
    free(site.posts); free(source); free(destination); return 0;
}
