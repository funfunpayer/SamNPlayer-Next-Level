#!/usr/bin/env bash
# Import SamNPlayer Free `main` with its full history into SamNPlayer Next (gate N1).
#
# Run from a clone of SamNPlayer-Next-Level on a machine that can read both
# private repos. It never force-pushes and never touches Free: it creates a
# branch, merges Free main (unrelated histories, because this repo started with
# its own README commit), and pushes that branch for a reviewed PR.
#
#   scripts/import-free-history.sh [free-ref]     # default: main
#
# Only `main` is imported — no old feature branches, tags or `--mirror`.
set -euo pipefail

FREE_URL="${FREE_URL:-https://github.com/funfunpayer/SamNPlayer.git}"
FREE_REF="${1:-main}"
BRANCH="${BRANCH:-next/import-free}"

if [ -n "$(git status --porcelain)" ]; then
	echo "working tree not clean" >&2
	exit 1
fi

git remote get-url free >/dev/null 2>&1 || git remote add free "$FREE_URL"
git fetch --no-tags free "$FREE_REF"
if [ "$(git rev-parse --is-shallow-repository)" = true ]; then
	git fetch --unshallow --no-tags free "$FREE_REF"
fi

# Quick history audit; stop on anything that looks like a secret or media blob.
hits="$(git log FETCH_HEAD --format= --name-only | sort -u |
	grep -iE '\.(pem|key|p12|pfx|env|mp4|mkv|mov|webm|onnx|pt)$' || true)"
keys="$(git log FETCH_HEAD -p -G 'PRIVATE KEY-----|ghp_[A-Za-z0-9]{20}|AKIA[0-9A-Z]{16}' --format='%h %s' || true)"
if [ -n "$hits$keys" ]; then
	printf 'history audit found candidates — stop and ask the Owner:\n%s\n%s\n' "$hits" "$keys" >&2
	exit 2
fi
echo "audit: $(git rev-list --count FETCH_HEAD) commits, no key files or media blobs"

FREE_SHA="$(git rev-parse FETCH_HEAD)"
git switch -c "$BRANCH"
if ! git merge --allow-unrelated-histories --no-edit -m "Import SamNPlayer Free ${FREE_REF} (${FREE_SHA}) with full history" "$FREE_SHA"; then
	# Only README.md is expected to conflict: Free's README wins; the Next
	# plan lives in docs/next/.
	conflicts="$(git diff --name-only --diff-filter=U)"
	if [ "$conflicts" != "README.md" ]; then
		echo "unexpected conflicts: $conflicts" >&2
		exit 3
	fi
	git checkout --theirs README.md
	git add README.md
	git commit --no-edit
fi

git push -u origin "$BRANCH"
echo "pushed $BRANCH — open a PR into main; CI must be green before merge (gate N1)"
