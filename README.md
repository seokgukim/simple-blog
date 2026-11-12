# simple-blog

![Live demo of the generated blog](app://c488f8875c9912648d187fff0a0c2d58c0cf/home/seokgukim/_projects/simple-blog/demo.gif?1794009153665)

`simple-blog` is a simple blog builder written in C.

Recursively reads Markdown from a local target directory and generates static UTF-8 HTML, with native MathML, build-time syntax highlighting, a small code/math stylesheet and no JavaScript.

The binary supports YAML headers like:

```yaml
title: <title>
date: <date>
tags: [<tag1>, <tag2>, ...]
```

And parse it down to generate html.

## Structure

```text
simple-blog/
├── src/
│   ├── main.c              # CLI, directory traversal, and site pages
│   ├── metadata.c/.h       # YAML front matter and schema validation
│   ├── render.c/.h         # Markdown rendering and URL validation
│   ├── math.c/.h           # LaTeX subset to allowlisted MathML
│   └── highlight.c/.h      # Small lexical code highlighter
├── public/
│   ├── logo.svg            # Landing-page logo, copied to output
│   └── code.css            # Code styling/token colors and display-math overflow
├── scripts/
│   ├── fetch-md4c.sh       # Fetch and verify the pinned Markdown parser
│   ├── fetch-yaml.sh       # Fetch and verify the pinned YAML parser
│   ├── fetch-itex.sh       # Pinned C LaTeX-to-MathML parser
│   ├── publish-cf.py       # Checked staging and Cloudflare Workers deployment
│   └── patches/            # MD4C rendering hooks and itex safety adjustments
├── vendor/                 # Downloaded dependencies (not tracked)
├── tests/
│   ├── smoke.sh            # End-to-end local build and overlap checks
│   ├── default-output.sh   # Project-local export from another working directory
│   ├── site.py             # Page, metadata, link, and failure tests
│   ├── test_math.py        # Actual C-to-MathML CLI integration tests
│   ├── test_highlight.py   # Highlighting, escaping and text-preservation tests
│   └── test_publish.py     # Deployment gates with a fake remote uploader
├── Makefile                # Build, test, and clean targets
├── .env.example            # Local source and Cloudflare Workers configuration
├── build/                  # Generated executable (not tracked)
└── export/                 # Default generated site (not tracked)
```

The builder renders `.md` files to matching `.html` paths, preserving nested directories.  
MD4C, libyaml and itex2MML are compiled into the binary; no separate Markdown, YAML or math executable or installed development library is needed.

### Site pages

The generated site has these pages:

- **Landing page** (`index.html`): the logo, an introduction, and the post with the newest YAML `date` (a linked title and date, not the entire post body). The introduction appears in the main content between the logo and “Recent post”.
- **About page** (`about.html`): generated from `about.md` at the root of `TARGET_DIR`; this file is reserved for the about page, not treated as a post. Its YAML `title` supplies the blog name and `introduction` supplies the landing-page introduction.
- **Tag listing** (`tags.html`) and per-tag pages (`tags/<tag-name>.html`).
- **Post listing** (`posts.html`) and one HTML page per Markdown post.

Every page has a centered header with the blog name and navigation links:

```text
       <title>
    About Post Tag
```

Link-preview metadata (`og:title`) uses the current page's title.

The visual reference is the simple Berkshire Hathaway website.  
All pages are static HTML with no JavaScript. The only stylesheet, `public/code.css`, adds monospace text, a light background, spacing, horizontal scrolling and syntax colors to code, plus margins/overflow handling for display math.  
Each page includes a mobile viewport tag. A media query in `public/code.css` handles screens up to 600px wide: long text wraps, images fit the page, and wide tables scroll horizontally. Alignment still uses HTML's legacy `align` attribute.

### Content rules

`TARGET_DIR/about.md` is required and must contain nonempty YAML fields:

```markdown
---
title: Seokgukim's Garage
introduction: A little space for SeokguKim
author: seokgukim
---

# About me

Your about-page content goes here.
```

- `author` is optional; when supplied, every page gets a copyright footer with the year at build time. Rebuild the site to update the year.
- Posts may omit front matter: the relative filename is the fallback title, the date is absent, and tags are empty. Supplied metadata must be valid.
- Dates must be calendar dates in `YYYY-MM-DD` format. Listings sort newest first, undated posts last, then by relative path to break ties. If all posts are undated, the first path is selected for the landing page.
- YAML supports quoted strings, multiline text, UTF-8, and flow/block tag lists. Duplicate fields, invalid types, empty values, and invalid dates are errors. Unknown fields are ignored. Duplicate tags are removed.
- Tag filenames preserve the original name: `tags/한국어.html`, `tags/problem-solving.html`, or `tags/two words.html`. Links are URL-encoded separately. Slashes, backslashes, control bytes and `%` are escaped in the filename to prevent traversal and collisions (`a/b` becomes `a%2Fb.html`). The escaped name must fit in 250 UTF-8 bytes before `.html`; names are never silently truncated. The separate `tags.html` listing leaves `tags/index.html` available for a tag literally named `index`.
- `index.html`, `about.html`, `posts.html`, `tags.html`, `tags/`, and `public/` are reserved output routes; conflicting Markdown paths are errors.
- Raw HTML in Markdown is displayed as text. Only relative, HTTP, HTTPS and mailto URLs are accepted; executable and other unsupported schemes are errors. This is deliberately not GitHub's full raw-HTML sanitization behavior.

### Planned responsibilities

As the implementation grows, split the code by responsibility rather than keeping everything in `main.c`:

- **Source**: discover and read posts from a local directory or S3-compatible storage.
- **Metadata**: parse YAML front matter, including `title`, `date`, and `tags`.
- **Rendering**: turn GitHub-flavored Markdown into static HTML and resolve links between generated pages.
- **Math**: convert inline and block LaTeX to safe native MathML in C.
- **Highlighting**: emit escaped code with CSS token spans at build time.
- **Site output**: write post pages and a linked index to the destination.

Still pending: S3 input, Markdown link rewriting, heading anchors, copying content assets other than the logo/code stylesheet, and strict GFM conformance testing.  
MD4C's GFM mode is enabled, but full GitHub compatibility is not claimed yet.

### Build and test

```sh
make
make test
./build/simple-blog ~/path/to/blog/dir
```

The default output is `<project_dir>/export/`.  
Make records the project path in the binary, so running it from a different working directory still exports to that path and reads the project's public logo/code stylesheet. Rebuild after moving the project.  
An optional second argument overrides the output for isolated builds and tests. The output must be new or empty, its parent directory must exist, and it must not overlap the input tree.  
Existing output is never deleted or overwritten.  
Input symlinks are not followed; `about.md` must be a regular file.  
Metadata, URL, route and math-expression checks run before output is written; an I/O failure can still leave partial output.

Requires a POSIX environment, GCC (tested with 14.2), Make and `patch`. The first build also needs network access, `curl`, `sha256sum`, and `tar` to fetch the pinned MD4C 0.5.2, libyaml 0.2.5 and itex2MML 1.6.1 distributions into ignored `vendor/`.  
The itex distribution includes generated C: no Flex, Bison, C++, TeX, Node, Poppler or external fonts are needed to build or run the site generator.  
Subsequent builds work offline with the downloaded files.  
MD4C files are checked against pinned hashes each time; cached libyaml and itex2MML directories are reused based on their `.ready` markers without rehashing their contents.  
`make test` additionally uses Python 3; the blog builder itself does not.  
Binaries stay in ignored `build/`.  
The C builder uses CLI arguments, not `.env`; the deployment wrapper reads `.env` for the source directory and Cloudflare configuration.

### Static code-quality checks

```sh
make check-static
```

Runs GCC's path-sensitive analyzer and strict compiler warnings on project C sources, treating findings as errors.  
Tested with GCC 14.2; no additional analyzer dependency is required. Vendor implementations are outside this check.  
The check **passes** for all five project translation units. Filename-size checks address the bounds findings; one documented, function-scoped suppression handles GCC's false positive on deliberate CLI error exits.  
See [docs/static-analysis.md](docs/static-analysis.md) for scope and resolutions.

### Formatting project-owned code

The Python files under `scripts/` and `tests/` were formatted with Black 25.1.0.  
Install that version in your own Python environment to reproduce the formatting check:

```sh
python3 -m pip install 'black==25.1.0'
python3 -m black --check scripts/*.py tests/*.py
```

Remove `--check` to format those files.  
Black is a development tool, not a runtime dependency or part of `make test`.  
Prettier is the preferred formatter for project-owned JavaScript and TypeScript, but the project currently has no such files or configured Prettier dependency.  
Do not format downloaded dependencies or generated build output.

### Native math and code highlighting

`$...$` and `$$...$$` become native **MathML elements inside HTML**, using vendored itex2MML C sources.  
Formulas are selectable and scale without rasterization; inline and display styles are independent. Korean-adjacent math, fractions, sums, matrices and `align*` are supported. Code and escaped dollars stay literal.

This supports a LaTeX subset, not a full TeX engine. Unsupported commands and unsafe MathML extensions fail before output.  
Modern browsers with native MathML are required; there is no JavaScript fallback.  
The project fetches the checksum-pinned itex2MML 1.6.1 distribution and compiles its generated C sources with a local safety patch.  
The upstream `vendor/itex2MML/README` describes the distribution and its GPL, MPL and LGPL triple license; consult the distribution for license terms.  
See [docs/dependencies.md](docs/dependencies.md) for source provenance, cache verification and third-party license notices.

Fenced code gets lightweight C-based lexical highlighting for C/C++, C#, Ruby, Lua, JavaScript/TypeScript, shell, JSON and Python (including common aliases such as `rb`, `cpp` and `cs`).  
Keywords, strings, comments and numbers receive CSS classes; code text remains copyable and escaped.  
Unknown/unlabelled blocks and inline code remain plain. This is not a full language parser: heredocs, regex literals, interpolation and other advanced syntax are not comprehensively classified.  
No highlight.js, Node, browser scripts or theme framework is used.

### Cloudflare Workers deployment

Cloudflare Workers **Static Assets** is supported for local directory posts:

```sh
cp .env.example .env  # then edit locally; never commit credentials
chmod 600 .env
make prepare-cf      # checks + fresh staged build + local-link validation; no upload
make deploy-cf       # same gates, then upload with pinned Wrangler 4.158.0
```

Configure `BLOG_SOURCE_DIR`, `CF_WORKER_NAME`, `CLOUDFLARE_ACCOUNT_ID` and `CLOUDFLARE_API_TOKEN` in `.env` or the environment.  
Preparation does not require Cloudflare credentials or Node.js. Deployment requires Node.js 24+ and npm/npx; these are not dependencies of the builder or generated site.  
Each deploy replaces the named Worker's active deployment, so use a separate Worker name for a public preview rather than an existing production Worker.  
Every Markdown file in the source is published; there is no draft filter.

The wrapper uploads only a fresh `build/cf-worker.*/site/`, never `export/`.  
It does not alter source posts or existing exports. Broken local links block publication; remote links and fragment targets are not checked.  
Preparation does not change the remote account or DNS; deployment can create or replace the named Worker.  
See [docs/cloudflare-workers.md](docs/cloudflare-workers.md) for account setup, credentials, preview and production deployment, and limits. S3 input and CI content sourcing remain future work.

### License

The project's own source code is available under the [MIT License](LICENSE).  
Distributed binaries include third-party components and may be subject to GPL or other license conditions, depending on the applicable component licenses and distribution choices.  
The applicable itex2MML license versions and binary redistribution conditions have not yet been confirmed; review them before distributing binaries.  
See [docs/dependencies.md](docs/dependencies.md) for the known upstream notices.
