#!/usr/bin/env python3
"""Highlighting must preserve copyable code and never introduce active HTML."""
import unittest
from html.parser import HTMLParser

# Avoid Python's built-in site module; load the project's CLI test fixture.
import importlib.util
from pathlib import Path

spec = importlib.util.spec_from_file_location(
    "blog_site", Path(__file__).with_name("site.py")
)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class Code(HTMLParser):
    def __init__(self, html):
        super().__init__()
        self.inside = False
        self.text = []
        self.classes = []
        self.feed(html)

    def handle_starttag(self, tag, attrs):
        if tag == "pre":
            self.inside = True
            self.text.append("")
        if self.inside and tag == "span":
            self.classes.append(dict(attrs).get("class"))

    def handle_endtag(self, tag):
        if tag == "pre":
            self.inside = False

    def handle_data(self, text):
        if self.inside:
            self.text[-1] += text


class HighlightTest(unittest.TestCase):
    setUp = module.SiteTest.setUp
    put = module.SiteTest.put
    build = module.SiteTest.build
    html = module.SiteTest.html

    def test_c_highlight_preserves_multiline_code_and_escapes_html(self):
        raw = 'int main(void) {\n  /* multi\n     line */\n  puts("<script>&한국어");\n  return 42;\n}\n'
        self.put("post.md", "```c\n" + raw + "```")
        self.build()
        parsed = Code(self.html("post.html"))
        self.assertEqual(parsed.text, [raw])
        self.assertTrue(
            {"hl-keyword", "hl-comment", "hl-string", "hl-number"}
            <= set(parsed.classes)
        )
        self.assertFalse(module.HTML(self.html("post.html")).active)
        self.assertNotIn("<script>", self.html("post.html"))

    def test_languages_used_by_posts(self):
        samples = {
            "ruby": "def f; return 1; end",
            "rb": "class Foo; end",
            "lua": "local x = 1 -- comment",
            "csharp": "public class Foo {}",
            "typescript": "const x = true;",
            "cpp": "class Foo {};",
            "bash": 'if true; then echo "ok"; fi',
            "json": '{"ok": true}',
        }
        self.put(
            "post.md",
            "\n\n".join(
                "```" + lang + "\n" + code + "\n```" for lang, code in samples.items()
            ),
        )
        self.build()
        parsed = Code(self.html("post.html"))
        self.assertEqual(parsed.text, [code + "\n" for code in samples.values()])
        self.assertGreaterEqual(parsed.classes.count("hl-keyword"), len(samples))

    def test_unknown_language_and_inline_code_stay_plain(self):
        self.put(
            "post.md",
            "`return 42`\n\n```unknown\n<script>return 42</script>\n```\n\n"
            "    int plain = 42;\n",
        )
        self.build()
        parsed = Code(self.html("post.html"))
        self.assertEqual(parsed.classes, [])
        self.assertIn("<code>return 42</code>", self.html("post.html"))
        self.assertEqual(
            parsed.text, ["<script>return 42</script>\n", "int plain = 42;\n"]
        )

    def test_lexer_state_is_per_block_and_lua_long_strings_are_preserved(self):
        raw = 'local s = [=[line 1\n"한국어" -- not a comment\n]=]\n--[=[comment\nblock]=]\n'
        self.put(
            "post.md",
            "```lua\n"
            + raw
            + "```\n\n```c\n/* unfinished\n```\n\n```c\nreturn 1;\n```",
        )
        self.build()
        parsed = Code(self.html("post.html"))
        self.assertEqual(parsed.text, [raw, "/* unfinished\n", "return 1;\n"])
        self.assertEqual(parsed.classes.count("hl-comment"), 2)
        self.assertIn("hl-keyword", parsed.classes)


if __name__ == "__main__":
    unittest.main()
