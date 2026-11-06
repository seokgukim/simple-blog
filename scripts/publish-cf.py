#!/usr/bin/env python3
"""Prepare a fresh site for Cloudflare Workers static assets. No upload by default."""
import argparse
from html.parser import HTMLParser
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]
WRANGLER = "wrangler@4.158.0"


def configuration():
    """Read literal KEY=value entries, never shell code or interpolation."""
    values = {}
    dotenv = ROOT / ".env"
    if dotenv.exists():
        for number, line in enumerate(
            dotenv.read_text(encoding="utf-8").splitlines(), 1
        ):
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            key, separator, value = line.partition("=")
            key, value = key.strip(), value.strip()
            if not separator or not re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", key):
                raise ValueError(f"invalid .env assignment on line {number}")
            if value.startswith(('"', "'")):
                if len(value) < 2 or value[-1] != value[0]:
                    raise ValueError(f"unclosed .env quote on line {number}")
                value = value[1:-1]
            values[key] = value
    return {**values, **os.environ}


class Links(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.links = []

    def handle_starttag(self, tag, attrs):
        for key, value in attrs:
            if key in ("href", "src") and value:
                self.links.append(value)


def validate_site(site):
    for name in (
        "index.html",
        "about.html",
        "posts.html",
        "tags.html",
        "public/logo.svg",
        "public/code.css",
    ):
        if not (site / name).is_file():
            raise ValueError(f"missing required output: {name}")
    files = list(site.rglob("*"))
    if sum(path.is_file() for path in files) > 20000:
        raise ValueError("site exceeds the Workers Free plan's 20,000-file limit")
    for path in files:
        if path.is_symlink():
            raise ValueError("staged site must not contain symlinks")
        if not path.is_file():
            continue
        if path.stat().st_size > 25 * 1024 * 1024:
            raise ValueError(f"Workers asset exceeds 25 MiB: {path.relative_to(site)}")
        if path.suffix != ".html":
            continue
        parser = Links()
        parser.feed(path.read_text(encoding="utf-8"))
        for link in parser.links:
            url = urlsplit(link)
            if url.scheme or url.netloc or not url.path:
                continue
            base = site if url.path.startswith("/") else path.parent
            target = (base / unquote(url.path).lstrip("/")).resolve()
            if not target.is_relative_to(site):
                raise ValueError(f"{path.name}: link escapes site: {link}")
            # Workers serves extensionless HTML and directory index pages.
            candidates = (target, target / "index.html", Path(str(target) + ".html"))
            if not any(p.is_file() for p in candidates):
                raise ValueError(f"{path.name}: broken local link: {link}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--deploy", action="store_true", help="upload after all checks pass"
    )
    args = parser.parse_args()
    env = configuration()
    if args.deploy:
        for key in (
            "CLOUDFLARE_API_TOKEN",
            "CLOUDFLARE_ACCOUNT_ID",
            "CF_WORKER_NAME",
        ):
            if not env.get(key, "").strip():
                raise ValueError(f"set {key} before deployment")
        if not re.fullmatch(r"[0-9a-fA-F]{32}", env["CLOUDFLARE_ACCOUNT_ID"]):
            raise ValueError("CLOUDFLARE_ACCOUNT_ID must be 32 hexadecimal characters")
        if not re.fullmatch(
            r"[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?", env["CF_WORKER_NAME"]
        ):
            raise ValueError(
                "CF_WORKER_NAME must be a lowercase Worker name of at most 63 characters"
            )
        try:
            node = subprocess.run(
                ["node", "--version"],
                capture_output=True,
                text=True,
                check=True,
                cwd=ROOT,
                env=env,
            )
        except (OSError, subprocess.CalledProcessError) as error:
            raise ValueError(
                "deployment requires Node.js 24 or newer and npm/npx"
            ) from error
        version = re.fullmatch(r"v(\d+)\.\d+\.\d+\s*", node.stdout)
        if not version or int(version.group(1)) < 24:
            raise ValueError("deployment requires Node.js 24 or newer and npm/npx")
    source = env.get("BLOG_SOURCE_DIR", "")
    if not source:
        raise ValueError("set BLOG_SOURCE_DIR to the publishable Markdown directory")
    source = Path(source).expanduser()
    if not source.is_absolute():
        source = ROOT / source
    if not source.is_dir():
        raise ValueError("BLOG_SOURCE_DIR must be an existing directory")
    subprocess.run(["make", "check-static", "test"], cwd=ROOT, check=True)
    (ROOT / "build").mkdir(exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix="cf-worker.", dir=ROOT / "build")) / "site"
    subprocess.run(
        [str(ROOT / "build/simple-blog"), str(source), str(stage)], cwd=ROOT, check=True
    )
    validate_site(stage)
    print(f"Prepared: {stage}", flush=True)
    if args.deploy:
        subprocess.run(
            [
                "npx",
                "--yes",
                WRANGLER,
                "deploy",
                "--assets",
                str(stage),
                "--name=" + env["CF_WORKER_NAME"],
                "--compatibility-date=2025-04-01",
                "--keep-vars",
            ],
            cwd=stage.parent,
            check=True,
            stdin=subprocess.DEVNULL,
            env={**env, "CI": "true", "WRANGLER_SEND_METRICS": "false"},
        )
        print(f"Deployed Worker: {env['CF_WORKER_NAME']}")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f"Cloudflare Workers: {error}", file=sys.stderr)
        sys.exit(1)
