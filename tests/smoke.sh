#!/bin/sh
set -eu
bin=$(cd "$(dirname "$0")/.." && pwd)/build/simple-blog
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/in/nested"
printf '%s\n' '---' 'title: "나의 블로그 & 기록"' 'introduction: >-' '  작은 공간입니다.' '---' '# 소개 본문' > "$tmp/in/about.md"
printf '%s\n' '---' 'title: Older' 'date: 2024-01-01' '---' 'Older body' > "$tmp/in/older.md"
printf '%s\n' '---' 'title: 안녕하세요' 'date: 2025-05-30' 'tags: [blog]' '---' '# 한국어' '' '~~취소~~' > "$tmp/in/nested/post.md"
"$bin" "$tmp/in" "$tmp/out"
grep -q '<meta charset="utf-8">' "$tmp/out/nested/post.html"
grep -q '안녕하세요' "$tmp/out/nested/post.html"
grep -q '<h1>한국어</h1>' "$tmp/out/nested/post.html"
grep -q '<del>취소</del>' "$tmp/out/nested/post.html"
grep -q 'nested/post.html' "$tmp/out/index.html"
! grep -q 'older.html' "$tmp/out/index.html"
grep -q 'older.html' "$tmp/out/posts.html"
grep -q '<h1>소개 본문</h1>' "$tmp/out/about.html"
! grep -q '<li><a href="about.html"' "$tmp/out/posts.html"
cmp public/logo.svg "$tmp/out/public/logo.svg"
for page in "$tmp/out/index.html" "$tmp/out/about.html" "$tmp/out/posts.html" "$tmp/out/nested/post.html"; do
    grep -q 'align="center"' "$page"
    grep -q '나의 블로그 &amp; 기록' "$page"
    grep -q '작은 공간입니다.' "$page"
    grep -q '>About</a>' "$page"
    grep -q '>Post</a>' "$page"
    grep -q '>Tag</a>' "$page"
done
grep -q 'href="../about.html"' "$tmp/out/nested/post.html"
# Overlapping trees must be rejected before creating or truncating output.
if "$bin" "$tmp/in" "$tmp/in/generated" 2>/dev/null; then
    echo 'output inside source unexpectedly accepted' >&2; exit 1
fi
test ! -e "$tmp/in/generated"
printf 'keep\n' > "$tmp/index.html"
if "$bin" "$tmp/in" "$tmp" 2>/dev/null; then
    echo 'output containing source unexpectedly accepted' >&2; exit 1
fi
grep -qx 'keep' "$tmp/index.html"
ln -s "$tmp/in" "$tmp/alias"
if "$bin" "$tmp/in" "$tmp/alias/generated" 2>/dev/null; then
    echo 'overlap through symlink unexpectedly accepted' >&2; exit 1
fi
test ! -e "$tmp/in/generated"
echo 'smoke test passed'
