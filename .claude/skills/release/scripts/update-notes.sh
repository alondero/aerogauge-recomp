#!/usr/bin/env bash
# update-notes.sh — replace the workflow's placeholder notes with the rich body.
#
# Usage: update-notes.sh <version> <notes-file>
#   version:    e.g. v0.1.0
#   notes-file: path to a Markdown file matching references/release-notes-template.md

set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "Usage: $0 <version> <notes-file>" >&2
  exit 2
fi

VERSION="$1"
NOTES_FILE="$2"

if [[ ! -f "${NOTES_FILE}" ]]; then
  echo "error: notes file '${NOTES_FILE}' not found" >&2
  exit 1
fi

if ! [[ "${VERSION}" =~ ^v[0-9]+\.[0-9]+\.[0-9]+(-[a-zA-Z0-9.]+)?$ ]]; then
  echo "error: '${VERSION}' does not match v<MAJOR>.<MINOR>.<PATCH>" >&2
  exit 1
fi

# Refuse to push an empty file — easy mistake and gh release edit accepts it.
if [[ ! -s "${NOTES_FILE}" ]]; then
  echo "error: notes file '${NOTES_FILE}' is empty." >&2
  exit 1
fi

# Refuse to push notes that look like the workflow placeholder. Check the
# whole file (not just the first 2 lines) — headers or blank lines shouldn't
# let a placeholder body sneak past.
if grep -q "Automated build from commit" "${NOTES_FILE}"; then
  echo "error: notes file appears to be the workflow placeholder." >&2
  echo "       Use a body drafted from references/release-notes-template.md." >&2
  exit 1
fi

# Refuse unrendered template placeholders. Easy to ship a draft that still
# has literal <PLACEHOLDER>, <PREV_VERSION>, <NEW_VERSION>, <RUN_ID>,
# <SHORT_SHA>, <YYYY-MM-DD> tokens.
if grep -E -q "<(PLACEHOLDER|PREV_VERSION|NEW_VERSION|RUN_ID|SHORT_SHA|YYYY-MM-DD)>" "${NOTES_FILE}"; then
  echo "error: notes file still has unrendered template placeholders:" >&2
  echo "       <PLACEHOLDER>, <PREV_VERSION>, <NEW_VERSION>, <RUN_ID>," >&2
  echo "       <SHORT_SHA>, <YYYY-MM-DD> must all be replaced with real values." >&2
  exit 1
fi

echo "→ Updating ${VERSION} notes from ${NOTES_FILE}"
gh release edit "${VERSION}" --notes-file "${NOTES_FILE}"

echo
echo "✓ Notes updated. Verify with:"
echo "    gh release view ${VERSION} --json body --jq '.body' | head -n 30"
