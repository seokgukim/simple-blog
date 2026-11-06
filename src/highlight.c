#include "highlight.h"
#include <ctype.h>
#include <string.h>

typedef enum { PLAIN, C, RUBY, LUA, SHELL, JSON, SCRIPT, CSHARP, PYTHON } Language;
static int word_in(const char *words, const char *text, size_t n) {
    for (const char *p = words; *p;) {
        const char *end = strchr(p, ' ');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len == n && !memcmp(p, text, n)) return 1;
        p += len; if (*p) p++;
    }
    return 0;
}
static Language language(const char *s, size_t n) {
    if (word_in("c h cpp c++ cc cxx hpp", s, n)) return C;
    if (word_in("cs csharp c#", s, n)) return CSHARP;
    if (word_in("js javascript ts typescript", s, n)) return SCRIPT;
    if (word_in("rb ruby", s, n)) return RUBY;
    if (word_in("lua", s, n)) return LUA;
    if (word_in("sh bash shell", s, n)) return SHELL;
    if (word_in("json", s, n)) return JSON;
    if (word_in("py python", s, n)) return PYTHON;
    return PLAIN;
}
static const char *keywords(Language lang) {
    switch (lang) {
    case C: return "alignas alignof asm auto bool break case catch char class const constexpr consteval constinit continue decltype default delete do double else enum explicit export extern false float for friend goto if inline int long mutable namespace new noexcept nullptr operator private protected public register reinterpret_cast requires return short signed sizeof static static_assert static_cast struct switch template this thread_local throw true try typedef typename union unsigned using virtual void volatile wchar_t while _Bool _Atomic _Thread_local";
    case CSHARP: return "abstract as async await base bool break byte case catch char checked class const continue decimal default delegate do double else enum event explicit extern false finally fixed float for foreach get goto if implicit in int interface internal is lock long namespace new null object operator out override params partial private protected public readonly record ref required return sbyte sealed set short sizeof stackalloc static string struct switch this throw true try typeof uint ulong unchecked unmanaged unsafe ushort using value var virtual void volatile when where while yield";
    case SCRIPT: return "abstract any as async await boolean break case catch class const constructor continue debugger declare default delete do else enum export extends false finally for from function get if implements import in infer instanceof interface keyof let namespace never new null number of private protected public readonly return set static string super switch symbol this throw true try type typeof undefined unknown var void while with yield";
    case RUBY: return "BEGIN END alias and begin break case class def defined do else elsif end ensure false for if in module next nil not or redo rescue retry return self super then true undef unless until when while yield";
    case LUA: return "and break do else elseif end false for function goto if in local nil not or repeat return then true until while";
    case SHELL: return "if then else elif fi for while until do done case esac in function select time true false";
    case JSON: return "true false null";
    case PYTHON: return "False None True and as assert async await break class continue def del elif else except finally for from global if import in is lambda nonlocal not or pass raise return try while with yield match case";
    default: return "";
    }
}
static void escaped(FILE *out, const char *s, size_t size) {
    for (size_t i = 0; i < size; i++) {
        switch (s[i]) {
        case '&': fputs("&amp;", out); break;
        case '<': fputs("&lt;", out); break;
        case '>': fputs("&gt;", out); break;
        case 0: fputs("\xef\xbf\xbd", out); break;
        default: fputc((unsigned char)s[i], out); break;
        }
    }
}
/* Lua's [=[...]=] strings/comments may span any number of Markdown callbacks. */
static size_t lua_long_end(const char *s, size_t n, size_t start) {
    if (start >= n || s[start] != '[') return start;
    size_t p = start + 1;
    while (p < n && s[p] == '=') p++;
    if (p == n || s[p] != '[') return start;
    size_t equals = p - start - 1;
    for (size_t i = p + 1; i < n; i++) {
        if (s[i] != ']') continue;
        size_t j = i + 1;
        while (j < n && s[j] == '=') j++;
        if (j < n && s[j] == ']' && j - i - 1 == equals) return j + 1;
    }
    return n;
}
void highlight_html(FILE *out, const char *s, size_t n, const char *label, size_t label_size) {
    Language lang = language(label, label_size);
    if (lang == PLAIN) { escaped(out, s, n); return; }
    int slash_comments = lang == C || lang == CSHARP || lang == SCRIPT;
    int hash_comments = lang == RUBY || lang == SHELL || lang == PYTHON;
    for (size_t i = 0; i < n;) {
        size_t end = i + 1;
        const char *kind = NULL;
        if ((hash_comments && s[i] == '#') ||
            (slash_comments && s[i] == '/' && end < n && s[end] == '/') ||
            (lang == LUA && s[i] == '-' && end < n && s[end] == '-')) {
            kind = "comment";
            if (lang == LUA && (end = lua_long_end(s, n, i + 2)) > i + 2) { /* long comment */ }
            else { end = i; while (end < n && s[end] != '\n') end++; }
        } else if (slash_comments && s[i] == '/' && end < n && s[end] == '*') {
            kind = "comment"; end = i + 3 < n ? i + 3 : n;
            while (end < n && !(s[end - 1] == '*' && s[end] == '/')) end++;
            if (end < n) end++;
        } else if (lang == LUA && (end = lua_long_end(s, n, i)) > i) {
            kind = "string";
        } else if (s[i] == '"' || s[i] == '\'' ||
                   (s[i] == '`' && (lang == SCRIPT || lang == RUBY || lang == SHELL)) ||
                   (lang == CSHARP && s[i] == '@' && i + 1 < n && s[i+1] == '"')) {
            kind = "string";
            int verbatim = s[i] == '@';
            size_t start = i + (size_t)verbatim;
            char quote = s[start]; end = start + 1;
            int triple = lang == PYTHON && end + 1 < n && s[end] == quote && s[end+1] == quote;
            if (triple) end += 2;
            while (end < n) {
                if (!verbatim && s[end] == '\\') { end += end + 1 < n ? 2 : 1; continue; }
                if (s[end] == quote) {
                    if (triple) {
                        if (end + 2 < n && s[end+1] == quote && s[end+2] == quote) { end += 3; break; }
                    } else if (verbatim && end + 1 < n && s[end+1] == quote) { end += 2; continue; }
                    else { end++; break; }
                }
                end++;
            }
        } else if (isdigit((unsigned char)s[i])) {
            kind = "number"; end = i + 1;
            while (end < n && (isalnum((unsigned char)s[end]) || s[end] == '.' || s[end] == '_' ||
                ((s[end] == '+' || s[end] == '-') && strchr("eEpP", s[end-1])))) end++;
        } else if (isalpha((unsigned char)s[i]) || s[i] == '_') {
            end = i + 1;
            while (end < n && (isalnum((unsigned char)s[end]) || s[end] == '_')) end++;
            if (word_in(keywords(lang), s + i, end - i)) kind = "keyword";
        } else end = i + 1;
        if (kind) fprintf(out, "<span class=\"hl-%s\">", kind);
        escaped(out, s + i, end - i);
        if (kind) fputs("</span>", out);
        i = end;
    }
}
