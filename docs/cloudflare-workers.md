# Cloudflare Workers: static assets

Publish a fresh local build as a Cloudflare Worker with static assets.  
The site has no Worker script or browser JavaScript; Wrangler is the uploader.  
This is **Workers**, not Pages: Pages project names and branch preview settings do not apply.

## Setup

1. Identify the Cloudflare account and a Worker name.
2. Create an API token scoped to that account with **Workers Scripts: Edit**; Cloudflare may require additional permissions for account features or routes you configure separately.
3. Install Node.js **24 or newer** and npm/npx on the publishing machine.
4. Copy `.env.example` to `.env`, restrict access with `chmod 600 .env`, and edit it locally.

```dotenv
BLOG_SOURCE_DIR="~/path/to/blog"
CF_WORKER_NAME="simple-blog-preview"
CLOUDFLARE_ACCOUNT_ID="your-32-character-account-id"
CLOUDFLARE_API_TOKEN="your-private-token"
```

Never commit or share `.env` or your token.  
The environment overrides `.env`, even with empty values.  
The file parser accepts literal `KEY=value` assignments, optional matching quotes, and whole-line comments; it does not execute shell commands, interpolate variables, or process escape sequences.  
Source paths support `~/`; relative paths are relative to the project directory.  
An existing `.env` from the Pages flow must be updated with `CF_WORKER_NAME`; the old `CF_PAGES_PROJECT` and `CF_PAGES_BRANCH` settings are not used.

## Prepare locally

```sh
make prepare-cf
```

This runs `make check-static` and `make test`, builds the source in a fresh `build/cf-worker.<random>/site/`, and validates required output files, local links, and Workers Free static-asset limits of 20,000 files and 25 MiB per file.  
It prints the staged path and does not upload anything; it does not require Node or Cloudflare credentials.  
Inspect the staged site before publishing.  
The source directory and `export/` are not changed, and staging directories remain available until `make clean`.

Broken local links block preparation and deployment; external URLs and fragment targets are not checked.  
Markdown `.md` links are not rewritten and content images are not copied by the current builder.

## Deploy preview, then production

```sh
make deploy-cf
# After inspecting a separate preview, target the intended production Worker:
CF_WORKER_NAME=your-production-worker make deploy-cf
```

Each invocation reruns checks and creates a new staging site, then invokes pinned Wrangler 4.158.0 with `wrangler deploy --assets` and an explicit Worker name and compatibility date.  
The Worker receives only static assets, with no application Worker script.  
Wrangler gets credentials through environment variables rather than command arguments and preserves existing dashboard variables with `--keep-vars`.  
Wrangler and its transitive npm dependencies are downloaded through npm; only the Wrangler direct version is pinned.

**Deploying updates the named Worker's active deployment immediately.**  
Use a separate Worker name for preview; unlike Pages branches, a different branch name does not automatically make a Worker deployment a preview.  
An existing Worker may have code, bindings, routes, triggers, or dashboard configuration; do not target it until you understand how deploying an assets-only Worker affects those settings.  
The wrapper does not import your existing Worker configuration and should not be used to deploy over an application Worker with required bindings or routes.  
Preview Workers and `workers.dev` URLs are public unless protected separately.  
All Markdown files in the source directory are published, including undated posts; there is no draft/private filter.

The wrapper runs Wrangler from an isolated staging directory, so an unrelated root-level Wrangler configuration is not loaded.  
Custom domains, routes, Access policies, and production rollback are outside this wrapper and should be configured and verified separately.  
A failed upload might have reached Cloudflare before a network error; check the dashboard before retrying.  
After the first real deployment, verify the URL, HTML redirects, Korean and escaped tag paths, MathML, code blocks, assets, and custom-domain behavior.

Cloudflare Workers Static Assets can redirect `.html` requests to extensionless URLs by default and serves static 404 responses according to its asset routing settings.  
The wrapper does not configure an SPA fallback or custom 404 handling.

## Verification and references

`tests/test_publish.py` exercises the real C builder with temporary files and a fake uploader, so it does not require Cloudflare credentials or publish content.  
An actual authenticated upload and hosted routing validation remain operator steps.

- [Workers Static Assets: get started](https://developers.cloudflare.com/workers/static-assets/get-started/)
- [Static asset routing and 404s](https://developers.cloudflare.com/workers/static-assets/routing/static-site-generation/)
- [Workers limits](https://developers.cloudflare.com/workers/platform/limits/)
- [Wrangler configuration](https://developers.cloudflare.com/workers/wrangler/configuration/)
