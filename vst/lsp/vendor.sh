#!/usr/bin/env bash
# Re-creates lsp-vendor.tar.gz: the LSP code this plugin is built from, at the commits in UPSTREAM.txt.
# Maintainers only (the build never downloads anything; it unpacks the committed tarball).
#   vst/lsp/vendor.sh            clone the pinned commits into a temporary folder and pack them
#   vst/lsp/vendor.sh <dir>      use existing clones in <dir> (checked out at the pinned commits)
# The tarball holds, per repository: COPYING / COPYING.LESSER, the whole include/ tree, the .cpp files listed in
# sources.txt, and for the plugin repository its include/, src/main/plug/, src/main/meta/, README.md, CHANGELOG.
# Nothing in it is changed. A new LSP release: edit UPSTREAM.txt, run this, rebuild, run the host test.
set -euo pipefail
cd "$(dirname "$0")"
HERE="$PWD"
SRC="${1:-}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
if [ -z "$SRC" ]; then
  SRC="$TMP/clones"; mkdir -p "$SRC"
  while read -r repo sha; do
    case "$repo" in ''|\#*) continue ;; esac
    git -C "$SRC" clone -q "https://github.com/lsp-plugins/$repo.git"
    git -C "$SRC/$repo" checkout -q "$sha"
  done < UPSTREAM.txt
fi
OUT="$TMP/lsp"; mkdir -p "$OUT"
while read -r repo sha; do
  case "$repo" in ''|\#*) continue ;; esac
  [ -d "$SRC/$repo" ] || { echo "missing $SRC/$repo" >&2; exit 1; }
  if [ -d "$SRC/$repo/.git" ]; then
    have=$(git -C "$SRC/$repo" rev-parse HEAD)
    [ "$have" = "$sha" ] || { echo "$repo is at $have, UPSTREAM.txt pins $sha" >&2; exit 1; }
  fi
  dst="$OUT/$repo"; mkdir -p "$dst"
  cp "$SRC/$repo"/COPYING* "$dst/" 2>/dev/null || true
  cp -r "$SRC/$repo/include" "$dst/"
  case "$repo" in
    lsp-plugins-*-*)   # the plugin itself (not lsp-plugins-shared)
      if [ "$repo" != lsp-plugins-shared ]; then
        mkdir -p "$dst/src/main"
        cp -r "$SRC/$repo/src/main/plug" "$SRC/$repo/src/main/meta" "$dst/src/main/"
        cp "$SRC/$repo/README.md" "$SRC/$repo/CHANGELOG" "$dst/"
      fi ;;
  esac
done < UPSTREAM.txt
grep -v '^#' sources.txt | awk 'NF {print $1}' | while read -r f; do
  mkdir -p "$OUT/$(dirname "$f")"
  cp "$SRC/$f" "$OUT/$f"
done
# reproducible archive: fixed order, owner and times
tar --sort=name --owner=0 --group=0 --numeric-owner --mtime='2026-01-01 00:00Z' -C "$TMP" -cf - lsp | gzip -n -9 > "$HERE/lsp-vendor.tar.gz"
sha256sum lsp-vendor.tar.gz | tee lsp-vendor.sha256
