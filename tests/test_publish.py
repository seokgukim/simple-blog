#!/usr/bin/env python3
"""CLI deployment checks: real builder/filesystem, fake Make and remote uploader."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class PublishTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="blog publish ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "scripts").mkdir()
        shutil.copy(ROOT / "scripts/publish-cf.py", self.root / "scripts")
        (self.root / "build").mkdir()
        shutil.copy(ROOT / "build/simple-blog", self.root / "build")
        self.source = self.root / "posts 한글"
        self.source.mkdir()
        (self.source / "about.md").write_text(
            "---\ntitle: Blog\nintroduction: Hello\n---\nAbout", encoding="utf-8"
        )
        (self.source / "a.md").write_text("Math $x^2$ and text.", encoding="utf-8")
        (self.root / "export").mkdir()
        (self.root / "export/old.html").write_text("preserve me")
        tools = self.root / "tools"
        tools.mkdir()
        self.log = self.root / "calls.jsonl"
        node = tools / "node"
        node.write_text(
            f"#!{sys.executable}\nimport os\nprint(os.environ.get('TEST_NODE_VERSION', 'v24.0.0'))\n"
        )
        node.chmod(0o755)
        for name in ("make", "npx"):
            tool = tools / name
            tool.write_text(
                f"#!{sys.executable}\n"
                "import json, os, pathlib, sys\n"
                "name = pathlib.Path(sys.argv[0]).name\n"
                "if name == 'npx' and os.environ.get('CLOUDFLARE_API_TOKEN') != 'fake-token-do-not-print':\n"
                "    sys.exit(9)\n"
                "if name == 'npx' and pathlib.Path.cwd() != pathlib.Path(sys.argv[sys.argv.index('--assets') + 1]).parent:\n"
                "    sys.exit(8)\n"
                "with open(os.environ['CALL_LOG'], 'a') as f:\n"
                "    f.write(json.dumps([name, sys.argv[1:]]) + '\\n')\n"
                "sys.exit(int(os.environ.get(name.upper() + '_EXIT', '0')))\n"
            )
            tool.chmod(0o755)
        self.env = {
            k: v
            for k, v in os.environ.items()
            if not k.startswith(
                ("CLOUDFLARE_", "CF_PAGES_", "CF_WORKER_", "BLOG_SOURCE")
            )
        }
        self.env.update(
            PATH=str(tools) + os.pathsep + self.env["PATH"],
            CALL_LOG=str(self.log),
            BLOG_SOURCE_DIR=str(self.source),
        )

    def run_publish(self, *args):
        return subprocess.run(
            [sys.executable, str(self.root / "scripts/publish-cf.py"), *args],
            cwd="/tmp",
            env=self.env,
            capture_output=True,
            text=True,
        )

    def calls(self):
        return (
            [json.loads(line) for line in self.log.read_text().splitlines()]
            if self.log.exists()
            else []
        )

    def test_prepare_needs_no_credentials_and_preserves_export(self):
        result = self.run_publish()
        self.assertEqual(result.returncode, 0, result.stderr)
        sites = list((self.root / "build").glob("cf-worker.*/site"))
        self.assertEqual(len(sites), 1)
        self.assertIn("<math", (sites[0] / "a.html").read_text())
        self.assertEqual((self.root / "export/old.html").read_text(), "preserve me")
        self.assertEqual([name for name, _ in self.calls()], ["make"])
        self.assertIn("Prepared:", result.stdout)

    def configure_upload(self):
        self.env.update(
            CLOUDFLARE_ACCOUNT_ID="a" * 32,
            CLOUDFLARE_API_TOKEN="fake-token-do-not-print",
            CF_WORKER_NAME="test-blog-preview",
        )

    def test_upload_uses_fresh_output_and_explicit_pinned_command(self):
        self.configure_upload()
        result = self.run_publish("--deploy")
        self.assertEqual(result.returncode, 0, result.stderr)
        calls = self.calls()
        command = next(args for name, args in calls if name == "npx")
        self.assertEqual(command[:3], ["--yes", "wrangler@4.158.0", "deploy"])
        self.assertIn("--assets", command)
        site = Path(command[command.index("--assets") + 1])
        self.assertTrue((site / "a.html").is_file())
        self.assertNotEqual(site, self.root / "export")
        self.assertIn("--name=test-blog-preview", command)
        self.assertIn("--compatibility-date=2025-04-01", command)
        self.assertIn("--keep-vars", command)
        self.assertNotIn("pages", command)
        self.assertNotIn(
            "fake-token-do-not-print",
            result.stdout + result.stderr + self.log.read_text(),
        )

    def test_deployment_rejects_node_below_24_before_checks(self):
        self.configure_upload()
        self.env["TEST_NODE_VERSION"] = "v22.20.0"
        result = self.run_publish("--deploy")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Node.js 24", result.stderr)
        self.assertEqual(self.calls(), [])

    def test_preparation_does_not_require_node_24(self):
        self.env["TEST_NODE_VERSION"] = "v22.20.0"
        result = self.run_publish()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_missing_credentials_fail_before_checks_or_upload(self):
        result = self.run_publish("--deploy")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("CLOUDFLARE_", result.stderr)
        self.assertEqual(self.calls(), [])

    def test_worker_name_is_required_and_validated_before_checks(self):
        self.configure_upload()
        for name in ("", "-bad", "bad-", "invalid_name", "a" * 64):
            with self.subTest(name=name):
                self.env["CF_WORKER_NAME"] = name
                result = self.run_publish("--deploy")
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("CF_WORKER_NAME", result.stderr)
                self.assertEqual(self.calls(), [])

    def test_failed_checks_prevent_upload(self):
        self.configure_upload()
        self.env["MAKE_EXIT"] = "1"
        result = self.run_publish("--deploy")
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("npx", [name for name, _ in self.calls()])
        self.assertEqual(list((self.root / "build").glob("cf-worker.*")), [])

    def test_bad_content_prevents_upload(self):
        self.configure_upload()
        (self.source / "a.md").write_text("---\ndate: invalid\n---\nBad")
        result = self.run_publish("--deploy")
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("npx", [name for name, _ in self.calls()])

    def test_broken_local_link_prevents_upload(self):
        self.configure_upload()
        (self.source / "a.md").write_text("[missing](missing.html)")
        result = self.run_publish("--deploy")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("missing.html", result.stderr)
        self.assertNotIn("npx", [name for name, _ in self.calls()])

    def test_site_root_and_clean_url_links_are_accepted(self):
        (self.source / "a.md").write_text("[about](/about) [index](/) [post](a.html)")
        result = self.run_publish()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_link_outside_site_prevents_upload(self):
        self.configure_upload()
        (self.source / "a.md").write_text("[outside](../../outside.html)")
        result = self.run_publish("--deploy")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("escapes site", result.stderr)
        self.assertNotIn("npx", [name for name, _ in self.calls()])

    def test_malformed_dotenv_fails_without_echoing_values(self):
        (self.root / ".env").write_text('CLOUDFLARE_API_TOKEN="private-unclosed')
        result = self.run_publish()
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("private-unclosed", result.stdout + result.stderr)
        self.assertEqual(self.calls(), [])

    def test_upload_failure_is_not_reported_as_success(self):
        self.configure_upload()
        self.env["NPX_EXIT"] = "1"
        result = self.run_publish("--deploy")
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("Deployed:", result.stdout)
        self.assertEqual((self.root / "export/old.html").read_text(), "preserve me")

    def test_dotenv_is_literal_and_environment_takes_precedence(self):
        self.configure_upload()
        del self.env["BLOG_SOURCE_DIR"]
        marker = self.root / "executed"
        (self.root / ".env").write_text(
            f'BLOG_SOURCE_DIR="{self.source}"\nCF_WORKER_NAME=from-file\n'
            f'IGNORED="$(touch {marker})"\n'
        )
        result = self.run_publish("--deploy")
        self.assertEqual(result.returncode, 0, result.stderr)
        command = next(args for name, args in self.calls() if name == "npx")
        self.assertIn("--name=test-blog-preview", command)
        self.assertFalse(marker.exists())


if __name__ == "__main__":
    unittest.main()
