#!/usr/bin/env python3
"""Behavior checks through the executable; Python is test-only."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from html.parser import HTMLParser
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]
BIN = Path(os.environ.get("BLOG_TEST_BINARY", ROOT / "build/simple-blog"))
ABOUT = '---\ntitle: "나의 블로그 & 기록"\nintroduction: 작은 공간\n---\n# 소개 본문\n'


class HTML(HTMLParser):
    def __init__(self, text):
        super().__init__(convert_charrefs=True)
        self.links = []
        self.active = []
        self.feed(text)

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if tag in ("style", "script") or any(
            k == "style" or k.lower().startswith("on") for k in attrs
        ):
            self.active.append((tag, attrs))
        for name in ("href", "src"):
            if name in attrs:
                self.links.append(attrs[name])


class SiteTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / "input"
        self.source.mkdir()
        self.dest = self.root / "output"
        self.put("about.md", ABOUT)

    def put(self, rel, text):
        path = self.source / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(text.encode("utf-8"))

    def build(self, ok=True):
        p = subprocess.run(
            [str(BIN), str(self.source), str(self.dest)],
            cwd=ROOT,
            capture_output=True,
            text=True,
        )
        if ok:
            self.assertEqual(p.returncode, 0, p.stderr)
        else:
            self.assertNotEqual(p.returncode, 0, p.stdout)
        return p

    def html(self, rel):
        return (self.dest / rel).read_text(encoding="utf-8")

    def check_links(self):
        for path in self.dest.rglob("*.html"):
            parsed = HTML(path.read_text(encoding="utf-8"))
            self.assertFalse(parsed.active, str(path))
            for href in parsed.links:
                url = urlsplit(href)
                if url.scheme or url.netloc or not url.path:
                    continue
                target = (path.parent / unquote(url.path)).resolve()
                self.assertTrue(target.is_relative_to(self.dest), href)
                self.assertTrue(target.is_file(), f"{path}: missing {href}")

    def test_tags_are_deduplicated_and_link_to_matching_posts(self):
        self.put(
            "nested/한 글.md",
            "---\ntitle: First\ndate: 2025-01-01\n"
            'tags: [한국어, "a/b", 한국어, "a?b"]\n---\nFirst body\n',
        )
        self.put(
            "second.md",
            "---\ntitle: Second\ndate: 2026-01-01\n"
            "tags:\n  - 한국어\n---\nSecond body\n",
        )
        self.build()
        index = self.html("tags.html")
        self.assertEqual(index.count(">한국어</a>"), 1)
        self.assertIn(">a/b</a>", index)
        self.assertIn(">a?b</a>", index)
        tag_pages = list((self.dest / "tags").glob("*.html"))
        self.assertEqual(len(tag_pages), 3)
        counts = []
        for path in tag_pages:
            links = HTML(path.read_text()).links
            counts.append(
                sum(
                    "second.html" in link or "%ED%95%9C%20%EA%B8%80.html" in link
                    for link in links
                )
            )
        self.assertEqual(sorted(counts), [1, 1, 2])
        self.assertEqual(self.html("nested/한 글.html").count(">한국어</a>"), 1)
        self.check_links()

    def test_tag_filenames_preserve_names_and_escape_only_unsafe_bytes(self):
        self.put(
            "post.md",
            '---\ntags: [한국어, "two words", "C#", index, "a/b", '
            '"a%2Fb", "../outside", "a?b", "a\\\\b"]\n---\nBody',
        )
        self.build()
        expected = {
            "한국어.html",
            "two words.html",
            "C#.html",
            "index.html",
            "a%2Fb.html",
            "a%252Fb.html",
            "..%2Foutside.html",
            "a?b.html",
            "a%5Cb.html",
        }
        self.assertEqual({p.name for p in (self.dest / "tags").iterdir()}, expected)
        links = HTML(self.html("tags.html")).links
        self.assertIn("tags/two%20words.html", links)
        self.assertIn("tags/C%23.html", links)
        self.assertIn("tags/a%252Fb.html", links)
        self.assertIn("tags/index.html", links)
        self.assertIn('href="../tags.html"', self.html("tags/index.html"))
        self.check_links()

    def test_tag_name_length_is_checked_before_output(self):
        self.put("post.md", '---\ntags: ["' + ("/" * 84) + '"]\n---\nBody')
        self.build(ok=False)
        self.assertFalse(self.dest.exists())

    def test_recent_uses_date_not_filename_or_discovery_order(self):
        self.put("z.md", "---\ntitle: Old\ndate: 2024-01-01\n---\nOld body")
        self.put("b.md", "---\ntitle: New B\ndate: 2025-01-01\n---\nB body")
        self.put("a.md", "---\ntitle: New A\ndate: 2025-01-01\n---\nA body")
        self.put("undated.md", "No front matter")
        self.build()
        links = HTML(self.html("index.html")).links
        self.assertIn("a.html", links)
        self.assertNotIn("b.html", links)
        self.assertNotIn("z.html", links)
        links = HTML(self.html("posts.html")).links
        self.assertLess(links.index("a.html"), links.index("b.html"))
        self.assertLess(links.index("b.html"), links.index("z.html"))
        self.assertLess(links.index("z.html"), links.index("undated.html"))
        self.check_links()

    def test_embed_metadata_uses_page_title_and_logo(self):
        self.put("nested/post.md", '---\ntitle: "Post & title"\ntags: [Example]\n---\nPost body')
        self.build()
        for path, title in (("index.html", "나의 블로그 &amp; 기록"),
                            ("posts.html", "Posts"), ("tags.html", "Tags"),
                            ("tags/Example.html", "Example"),
                            ("nested/post.html", "Post &amp; title")):
            text = self.html(path)
            self.assertIn(f'<meta property="og:title" content="{title}">', text)
            self.assertIn('<meta property="og:image" content="', text)
            self.assertIn('public/logo.svg">', text)

    def test_footer_uses_about_author_on_every_page(self):
        from datetime import datetime

        self.put("about.md", '---\ntitle: Blog\nintroduction: Hello\nauthor: "A & B"\n---\nAbout')
        self.put("nested/post.md", "---\ntags: [test]\n---\nPost")
        self.build()
        for page in self.dest.rglob("*.html"):
            self.assertIn(f"<hr>\n<footer>© {datetime.now().year} A &amp; B</footer>", page.read_text(), str(page))

    def test_empty_blog_has_all_navigation_targets(self):
        self.build()
        self.assertIn("No posts yet.", self.html("index.html"))
        self.check_links()

    def test_yaml_bom_crlf_quotes_and_multiline_intro(self):
        self.put(
            "about.md",
            '\ufeff---\r\ntitle: "블로그: & < >"\r\n'
            "introduction: >-\r\n  첫 줄\r\n  둘째 줄\r\n---\r\n소개",
        )
        self.put("post.md", "---\ntitle: 'It''s a post'\ntags: []\n---")
        self.build()
        self.assertIn("블로그: &amp; &lt; &gt;", self.html("about.html"))
        self.assertIn("첫 줄 둘째 줄", self.html("index.html"))
        self.assertIn("It's a post", self.html("post.html"))

    def test_bad_metadata_fails_before_output(self):
        for fields in (
            "title: [bad]",
            "tags: not-a-list",
            "date: 2025-02-29",
            "title: one\ntitle: two",
            "tags: [broken",
            'title: "\\0"',
            "tags: [&a [*a]]",
        ):
            with self.subTest(fields=fields):
                self.put("bad.md", f"---\n{fields}\n---\nbody")
                result = self.build(ok=False)
                self.assertIn("bad.md", result.stderr)
                self.assertFalse(self.dest.exists())

    def test_missing_about_fails_before_output(self):
        (self.source / "about.md").unlink()
        self.assertIn("about.md", self.build(ok=False).stderr)
        self.assertFalse(self.dest.exists())

    def test_missing_blog_introduction_fails_before_output(self):
        self.put("about.md", "---\ntitle: Blog\n---\nBody")
        self.assertIn("introduction", self.build(ok=False).stderr)
        self.assertFalse(self.dest.exists())

    def test_existing_output_is_not_modified(self):
        self.dest.mkdir()
        sentinel = self.dest / "index.html"
        sentinel.write_text("KEEP")
        self.build(ok=False)
        self.assertEqual(sentinel.read_text(), "KEEP")
        self.assertEqual(list(self.dest.iterdir()), [sentinel])

    def test_reserved_route_fails_before_output(self):
        for rel in (
            "index.md",
            "posts.md",
            "tags.md",
            "tags.html/bad.md",
            "tags/bad.md",
            "public/bad.md",
            "about.html/bad.md",
        ):
            with self.subTest(rel=rel):
                self.put(rel, "# conflict")
                self.build(ok=False)
                self.assertFalse(self.dest.exists())
                (self.source / rel).unlink()

    def test_file_directory_collision_fails_before_output(self):
        self.put("one.md", "# one")
        self.put("one.html/nested.md", "# nested")
        self.build(ok=False)
        self.assertFalse(self.dest.exists())

    def test_source_symlinks_are_not_followed(self):
        outside = self.root / "secret.md"
        outside.write_text("DO NOT COPY")
        (self.source / "linked.md").symlink_to(outside)
        (self.source / "loop").symlink_to(self.source, target_is_directory=True)
        self.build()
        self.assertFalse((self.dest / "linked.html").exists())
        self.check_links()

    def test_raw_html_is_text_and_gfm_tables_use_no_styles(self):
        self.put(
            "post.md",
            "<script>alert(1)</script>\n\n"
            '<b style="color:red" onclick="bad()">text</b>\n\n'
            "| a | b |\n| :- | -: |\n| 1 | 2 |\n\n- [x] done\n",
        )
        self.build()
        page = self.html("post.html")
        self.assertIn("&lt;script&gt;", page)
        self.assertIn("<table>", page)
        self.check_links()

    def test_script_urls_are_rejected_before_output(self):
        for url in (
            "javascript:alert(1)",
            "JaVaScRiPt:alert(1)",
            "javascript&#58;alert(1)",
            "java&#x09;script:alert(1)",
            "javascript&colon;alert(1)",
            "data:text/html,bad",
        ):
            with self.subTest(url=url):
                self.put("bad.md", f"[click]({url})")
                self.build(ok=False)
                self.assertFalse(self.dest.exists())

    def test_stylesheet_is_shared_and_scoped_to_code_and_math(self):
        self.put("nested/post.md", "Inline `코드`\n\n```c\nint main(void) {}\n```")
        self.build()
        css = (self.dest / "public/code.css").read_text()
        self.assertEqual(css, (ROOT / "public/code.css").read_text())
        self.assertIn("overflow-x: auto", css)
        # No page-wide selectors, external imports, webfonts or JS highlighter.
        selectors = [
            rule.split("{", 1)[0].strip() for rule in css.split("}") if rule.strip()
        ]
        self.assertEqual(
            set(selectors),
            {
                "code",
                "pre",
                "pre code",
                'math[display="block"]',
                "pre code .hl-keyword",
                "pre code .hl-string",
                "pre code .hl-comment",
                "pre code .hl-number",
            },
        )
        for page in self.dest.rglob("*.html"):
            parsed = HTML(page.read_text())
            styles = [href for href in parsed.links if href.endswith("code.css")]
            self.assertEqual(len(styles), 1, str(page))
            self.assertEqual(
                (page.parent / styles[0]).resolve(), self.dest / "public/code.css"
            )
        self.check_links()

    def test_short_filenames_and_nested_markdown_routes(self):
        self.put("a", "Not Markdown")
        self.put("ab", "Not Markdown")
        self.put(".md", "No basename")
        self.put("a.md", "Shortest post name")
        self.put("nested/한.md", "Nested Korean post")
        self.build()
        self.assertIn("Shortest post name", self.html("a.html"))
        self.assertIn("Nested Korean post", self.html("nested/한.html"))
        for name in ("a", "ab", ".md", ".html"):
            self.assertFalse((self.dest / name).exists(), name)
        self.check_links()

    def test_normal_url_entities_are_preserved(self):
        self.put("post.md", "[link](https://example.com/?a=1&amp;b=2)")
        self.build()
        self.assertIn(
            "https://example.com/?a=1&b=2", HTML(self.html("post.html")).links
        )


if __name__ == "__main__":
    unittest.main()
