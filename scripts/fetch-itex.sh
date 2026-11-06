#!/bin/sh
set -eu
dir=vendor/itex2MML
[ ! -f "$dir/.ready" ] || exit 0
if [ -e "$dir" ]; then
    echo "Incomplete dependency directory: $dir (move it aside before retrying)" >&2
    exit 1
fi
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
# Official 1.6.1 distribution; reject upstream archive changes.
curl -fsSL --connect-timeout 15 --max-time 120 \
    https://golem.ph.utexas.edu/~distler/blog/files/itexToMML.tar.gz \
    -o "$tmp/itex.tar.gz"
printf '%s  %s\n' 8af5a7443df87f74e0cdf2bce27dd78396566cbcffaf15efe1e87325fa6b15d3 "$tmp/itex.tar.gz" | sha256sum -c - >/dev/null
mkdir -p "$tmp/dependency"
tar xzf "$tmp/itex.tar.gz" -C "$tmp/dependency" --strip-components=1
touch "$tmp/dependency/.ready"
mkdir -p vendor
mv "$tmp/dependency" "$dir"
