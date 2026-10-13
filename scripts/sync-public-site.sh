#!/usr/bin/env bash
# Mirror the public landing + screenshots into funfunpayer/SamNPlayer-site.
# Required once the app repo is private (raw.githubusercontent URLs 404).
#
# Usage (as funfunpayer, from this private checkout):
#   ./scripts/sync-public-site.sh

set -euo pipefail

SITE_REPO="${SITE_REPO:-funfunpayer/SamNPlayer-site}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DIR="$(mktemp -d)"
cleanup() { rm -rf "$DIR"; }
trap cleanup EXIT

[[ -d "$ROOT/website" ]] || { echo "missing website/" >&2; exit 1; }
[[ -d "$ROOT/docs/media" ]] || { echo "missing docs/media/" >&2; exit 1; }

echo "Cloning $SITE_REPO …"
gh repo clone "$SITE_REPO" "$DIR/site" -- --depth 1

# Landing page files at repo root (keep remote README/SETUP/LICENSE/git)
for f in index.html styles.css script.js; do
  cp -a "$ROOT/website/$f" "$DIR/site/$f"
done
rm -rf "$DIR/site/fonts" "$DIR/site/media"
cp -a "$ROOT/website/fonts" "$DIR/site/fonts"
cp -a "$ROOT/website/media" "$DIR/site/media"

mkdir -p "$DIR/site/docs/media"
cp -a "$ROOT/docs/media/." "$DIR/site/docs/media/"

# Prefer the curated public README from docs/site-README.md when present
if [[ -f "$ROOT/docs/site-README.md" ]]; then
  cp -a "$ROOT/docs/site-README.md" "$DIR/site/README.md"
fi
if [[ -f "$ROOT/docs/site-SETUP.md" ]]; then
  cp -a "$ROOT/docs/site-SETUP.md" "$DIR/site/SETUP.md"
fi

cd "$DIR/site"
git add -A
if git diff --cached --quiet; then
  echo "No site changes to push."
  exit 0
fi
git commit -m "site: sync landing + media from private SamNPlayer"
git push origin HEAD
echo "Synced → https://github.com/$SITE_REPO"
echo "Enable Pages: Settings → Pages → Deploy from branch main / (root)"
