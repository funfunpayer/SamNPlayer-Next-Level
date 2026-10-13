#!/usr/bin/env bash
# Publish built binaries to the *public* showcase repo so anyone can
# download without access to the private application source.
#
# Usage (as repo owner, after the private SamNPlayer release exists):
#   ./scripts/publish-public-release.sh v0.5.8
#
# Requires: gh auth as funfunpayer (or an account that can release on
# both funfunpayer/SamNPlayer and funfunpayer/SamNPlayer-site).

set -euo pipefail

TAG="${1:?usage: $0 vX.Y.Z}"
APP_REPO="${APP_REPO:-funfunpayer/SamNPlayer}"
SITE_REPO="${SITE_REPO:-funfunpayer/SamNPlayer-site}"
DIR="$(mktemp -d)"
cleanup() { rm -rf "$DIR"; }
trap cleanup EXIT

echo "Downloading $TAG from $APP_REPO …"
gh release download "$TAG" -R "$APP_REPO" -D "$DIR"

ASSETS=(
  SamNPlayer-gui-linux-amd64
  SamNPlayer-gui-windows-amd64.exe
  SamNPlayer-cli-linux-amd64
  SamNPlayer-cli-windows-amd64.exe
  checksums.txt
)

for f in "${ASSETS[@]}"; do
  test -f "$DIR/$f" || { echo "missing asset: $f"; exit 1; }
done

NOTES="SamNPlayer ${TAG} — public binaries (application source is private).
Checksums: see checksums.txt."

if gh release view "$TAG" -R "$SITE_REPO" >/dev/null 2>&1; then
  echo "Release $TAG already exists on $SITE_REPO — uploading assets…"
  gh release upload "$TAG" -R "$SITE_REPO" --clobber \
    "$DIR/SamNPlayer-gui-linux-amd64" \
    "$DIR/SamNPlayer-gui-windows-amd64.exe" \
    "$DIR/SamNPlayer-cli-linux-amd64" \
    "$DIR/SamNPlayer-cli-windows-amd64.exe" \
    "$DIR/checksums.txt"
else
  echo "Creating $TAG on $SITE_REPO …"
  gh release create "$TAG" -R "$SITE_REPO" \
    --title "$TAG" \
    --notes "$NOTES" \
    "$DIR/SamNPlayer-gui-linux-amd64" \
    "$DIR/SamNPlayer-gui-windows-amd64.exe" \
    "$DIR/SamNPlayer-cli-linux-amd64" \
    "$DIR/SamNPlayer-cli-windows-amd64.exe" \
    "$DIR/checksums.txt"
fi

echo "Public downloads: https://github.com/${SITE_REPO}/releases/tag/${TAG}"
