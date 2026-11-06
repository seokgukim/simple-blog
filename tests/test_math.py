#!/usr/bin/env python3
"""Real C-to-MathML behavior through the builder CLI, with no external tools."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from html.parser import HTMLParser

ROOT = Path(__file__).resolve().parents[1]
BIN = Path(os.environ.get("BLOG_TEST_BINARY", ROOT / "build/simple-blog"))


class Math(HTMLParser):
    def __init__(self, html):
        super().__init__()
        self.modes = []
        self.tags = []
        self.annotations = []
        self.annotation = False
        self.feed(html)

    def handle_starttag(self, tag, attrs):
        self.tags.append((tag, dict(attrs)))
        if tag == "math":
            self.modes.append(dict(attrs).get("display"))
        if tag == "annotation":
            self.annotation = True
            self.annotations.append("")

    def handle_endtag(self, tag):
        if tag == "annotation":
            self.annotation = False

    def handle_data(self, text):
        if self.annotation:
            self.annotations[-1] += text


class MathTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / "input"
        self.source.mkdir()
        (self.source / "about.md").write_text(
            "---\ntitle: Blog\nintroduction: Hello\n---\nAbout"
        )
        self.dest = self.root / "output"
        self.env = os.environ.copy()
        # Neither PNG tools nor any other executable is available at runtime.
        self.env.update(PATH="", BLOG_TECTONIC="/missing", BLOG_PDFTOPPM="/missing")

    def build(self, body, ok=True, name="post"):
        file = self.source / (name + ".md")
        file.parent.mkdir(parents=True, exist_ok=True)
        file.write_bytes(body.encode())
        result = subprocess.run(
            [str(BIN), str(self.source), str(self.dest)],
            cwd=ROOT,
            env=self.env,
            capture_output=True,
            text=True,
        )
        if ok:
            self.assertEqual(result.returncode, 0, result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout)
        return result

    def html(self, name="post"):
        return (self.dest / (name + ".html")).read_text()

    def test_inline_and_display_are_native_mathml_without_images_or_scripts(self):
        self.build("한국어$x^2$입니다.\n\n$$\\frac{1}{N} \\sum_{k=0}^{N-1} x_k$$")
        parsed = Math(self.html())
        self.assertEqual(parsed.modes, ["inline", "block"])
        self.assertIn("msup", [tag for tag, _ in parsed.tags])
        self.assertIn("mfrac", [tag for tag, _ in parsed.tags])
        self.assertEqual(parsed.annotations[0], "x^2")
        self.assertFalse(list(self.dest.rglob("*.png")))
        self.assertNotIn("<script", self.html())
        self.assertNotIn("<x-equation", self.html())
        self.assertIn("한국어", self.html())

    def test_math_can_touch_korean_text_without_spaces(self):
        self.build("$1$보다 큰 정수 $n_i$들의 곱을 $N$이라고 합니다. 수식$x$입니다.")
        self.assertEqual(Math(self.html()).modes, ["inline"] * 4)
        self.assertNotIn("$", self.html())

    def test_currency_and_escaped_dollars_remain_literal(self):
        self.build("The prices are $5 and $10. Escaped \\$x\\$ is literal.")
        self.assertEqual(Math(self.html()).modes, [])
        self.assertIn("$5 and $10", self.html())

    def test_same_formula_has_independent_inline_and_display_styles(self):
        self.build("$x$ and $x$\n\n$$x$$", name="nested/한 글")
        self.assertEqual(
            Math(self.html("nested/한 글")).modes, ["inline", "inline", "block"]
        )
        self.assertFalse(list(self.dest.rglob("*.png")))

    def test_code_escapes_and_image_alt_are_not_math(self):
        self.build(
            "`$inline$`\n\n```text\n$$block$$\n```\n\n"
            "    $indented$\n\n\\$escaped\\$\n\n"
            "![math $x$ in alt](https://example.com/image.png)"
        )
        self.assertEqual(Math(self.html()).modes, [])
        self.assertIn("<code>$inline$</code>", self.html())
        self.assertIn("$$block$$", self.html())

    def test_align_star_matrices_and_unicode_text(self):
        self.build(
            "$$\n\\begin{align*}x &= 1 \\\\ y &= 2\\end{align*}\n$$\n\n"
            "$$\\begin{pmatrix}1 & 2 \\\\ 3 & 4\\end{pmatrix}$$\n\n"
            "$\\text{한국어}$"
        )
        self.assertEqual(Math(self.html()).modes, ["block", "block", "inline"])
        self.assertEqual(self.html().count("<mtable"), 2)
        self.assertIn("한국어", self.html())

    def test_annotation_escapes_angle_brackets_and_ampersands(self):
        self.build("$x<y$ and $a\\&b$")
        self.assertEqual(Math(self.html()).annotations, ["x<y", "a\\&b"])
        self.assertIn("&lt;", self.html())

    def test_about_body_renders_but_metadata_is_not_math(self):
        (self.source / "about.md").write_text(
            '---\ntitle: "$Blog$"\nintroduction: Hello\n---\n$x$'
        )
        self.build("No math")
        self.assertEqual(Math(self.html("about")).modes, ["inline"])
        self.assertEqual(Math(self.html()).modes, [])
        self.assertIn("$Blog$", self.html("index"))

    def test_invalid_math_fails_before_output(self):
        result = self.build("$\\UndefinedBlogMathCommand$", ok=False)
        self.assertIn("LaTeX", result.stderr)
        self.assertFalse(self.dest.exists())

    def test_unsafe_mathml_extensions_are_rejected(self):
        for raw in (
            r"\href{javascript:alert(1)}{x}",
            r"\begin{svg}<script>alert(1)</script>\end{svg}",
            r"\href{https://example.com}{x}",
        ):
            with self.subTest(raw=raw):
                self.build("$" + raw + "$", ok=False)
                self.assertFalse(self.dest.exists())

    def test_oversized_math_is_rejected_before_output(self):
        self.build("$" + "x" * 65537 + "$", ok=False)
        self.assertFalse(self.dest.exists())


if __name__ == "__main__":
    unittest.main()
