#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
project="$tmp/project with spaces"
mkdir -p "$project"
cp -R "$root/Makefile" "$root/src" "$root/scripts" "$root/public" "$root/vendor" "$project/"
(cd "$project" && make --no-print-directory build/simple-blog)
mkdir "$tmp/input"
printf '%s\n' '---' 'title: Default export test' 'introduction: Hello' '---' 'About body' > "$tmp/input/about.md"
# Even an unrelated working directory must use the project output and logo.
(cd / && "$project/build/simple-blog" "$tmp/input")
test -f "$project/export/index.html"
cmp "$project/public/logo.svg" "$project/export/public/logo.svg"
grep -q 'Default export test' "$project/export/about.html"
# A second run must not overwrite the first export.
if (cd / && "$project/build/simple-blog" "$tmp/input" 2>/dev/null); then
    echo 'Existing export unexpectedly overwritten' >&2
    exit 1
fi
echo 'default export test passed'
