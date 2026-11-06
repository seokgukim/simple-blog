#!/bin/sh
set -eu
dir=vendor/libyaml
# A completed fetch is reusable offline. Do not replace local dependency edits.
[ ! -f "$dir/.ready" ] || exit 0
if [ -e "$dir" ]; then
    echo "Incomplete dependency directory: $dir (move it aside before retrying)" >&2
    exit 1
fi
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
curl -fsSL --connect-timeout 15 --max-time 120 \
    https://github.com/yaml/libyaml/releases/download/0.2.5/yaml-0.2.5.tar.gz \
    -o "$tmp/yaml.tar.gz"
printf '%s  %s\n' c642ae9b75fee120b2d96c712538bd2cf283228d2337df2cf2988e3c02678ef4 "$tmp/yaml.tar.gz" | sha256sum -c - >/dev/null
tar xzf "$tmp/yaml.tar.gz" -C "$tmp"
mkdir -p "$tmp/dependency"
cp -R "$tmp/yaml-0.2.5/src" "$tmp/yaml-0.2.5/include" "$tmp/dependency/"
cp "$tmp/yaml-0.2.5/License" "$tmp/dependency/LICENSE"
touch "$tmp/dependency/.ready"
mkdir -p vendor
mv "$tmp/dependency" "$dir"
