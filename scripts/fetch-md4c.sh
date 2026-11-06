#!/bin/sh
# Fetch the pinned MD4C sources without replacing existing, different files.
set -eu
version=release-0.5.2
dir=vendor/md4c
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
while read -r hash file; do
    if [ -e "$dir/$file" ]; then
        printf '%s  %s\n' "$hash" "$dir/$file" | sha256sum -c - >/dev/null || {
            echo "Existing dependency file differs: $dir/$file" >&2; exit 1;
        }
    else
        url="https://raw.githubusercontent.com/mity/md4c/$version/src/$file"
        if [ "$file" = LICENSE.md ]; then
            url="https://raw.githubusercontent.com/mity/md4c/$version/LICENSE.md"
        fi
        curl -fsSL "$url" -o "$tmp/$file"
        printf '%s  %s\n' "$hash" "$tmp/$file" | sha256sum -c - >/dev/null
    fi
done <<'FILES'
d30937367d5413e7eaa218b1640b8946ff76fd34d97152f6979fd96169d5d0fc LICENSE.md
69d9362cd3b61261c3ed9d9f226ae1c66a3209f9db03805b1d114841ee3a6c1f entity.c
7d021cb683c0e7643df6ce0db51d75f8cbc313be9ec754cb4dc18c40ebaa400b entity.h
0c41f76eeecd7a58ef1a1041aefc83971389586bc01593f9f273294a170e047c md4c-html.c
e29c5e190884b2e36c5f2131b880b52ab0556091374cd3a3133145d0aa224814 md4c-html.h
52e39843b96b64d0281bf795128bcc88c9c4dc52d7436737e9f4306648dc4590 md4c.c
4efd19bf7ec270691d5b4189f496886e421768a814b5e817eb945aa85e859f18 md4c.h
FILES
mkdir -p "$dir"
for file in "$tmp"/*; do
    [ -f "$file" ] || continue
    cp -n "$file" "$dir/"
done
