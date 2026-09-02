#!/usr/bin/env bash
# list-changes.sh — print merged PRs, closed issues, and the merge-commit at HEAD
# since <base-tag>. Output is structured Markdown for the release-notes draft.
#
# Usage: list-changes.sh <base-tag> [head-ref]
#   base-tag: e.g. v0.1.0 — or the first-commit SHA for the inaugural release
#   head-ref: defaults to HEAD (use 'origin/main' to read remote state)

set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 ]]; then
  echo "Usage: $0 <base-tag> [head-ref]" >&2
  exit 2
fi

BASE_TAG="$1"
HEAD_REF="${2:-HEAD}"

# Sanity: base ref must exist (a tag or any rev-parseable SHA)
if ! git rev-parse -q --verify "${BASE_TAG}^{commit}" >/dev/null 2>&1 \
   && ! git rev-parse -q --verify "${BASE_TAG}" >/dev/null 2>&1; then
  echo "error: base ref '${BASE_TAG}' does not exist locally" >&2
  echo "       For the inaugural release, pass the first-commit SHA instead of a tag." >&2
  exit 1
fi

BASE_DATE="$(git log -1 --format='%ai' "${BASE_TAG}")"
HEAD_SHA="$(git rev-parse "${HEAD_REF}")"
HEAD_SHORT="$(git rev-parse --short "${HEAD_SHA}")"
HEAD_SUBJECT="$(git log -1 --format='%s' "${HEAD_REF}")"
HEAD_DATE="$(git log -1 --format='%ai' "${HEAD_REF}")"

echo "## Change summary: ${BASE_TAG} → ${HEAD_REF}"
echo
echo "**Base:** \`${BASE_TAG}\` (${BASE_DATE})"
echo "**Head:** \`${HEAD_SHORT}\` — ${HEAD_SUBJECT} (${HEAD_DATE})"
echo

# Commits since base
echo "### Commits since ${BASE_TAG}"
echo
echo '```'
git log --no-merges --oneline "${BASE_TAG}..${HEAD_REF}" || true
echo '```'
echo

# Merged PRs since base (uses gh)
echo "### Merged PRs since ${BASE_TAG}"
echo
PRS="$(gh pr list --state merged --base main --search "merged:>=${BASE_DATE%% *}" \
       --json number,title,mergedAt,author \
       --jq '.[] | "- **PR #\(.number)** (\(.mergedAt[:10])) — \(.title) — _@\(.author.login)_"')"
if [[ -n "${PRS}" ]]; then
  echo "${PRS}"
else
  echo "_None found via \`gh pr list\` — try \`gh pr list --state merged --base main --limit 30\` and filter manually._"
fi
echo

# Closed issues since base
echo "### Closed issues since ${BASE_TAG}"
echo
ISSUES="$(gh issue list --state closed --search "closed:>=${BASE_DATE%% *}" \
          --json number,title,closedAt \
          --jq '.[] | "- **#\(.number)** (\(.closedAt[:10])) — \(.title)"')"
if [[ -n "${ISSUES}" ]]; then
  echo "${ISSUES}"
else
  echo "_None closed since base ref._"
fi
echo

# Tag target recommendation
echo "### Tag target recommendation"
echo
# Find the most recent merge commit on main reachable from HEAD
TAG_TARGET="$(git log --merges --first-parent -n 1 --format='%H %s' "${HEAD_REF}" || true)"
echo "Most recent merge commit on \`${HEAD_REF}\`:"
echo
echo '```'
echo "${TAG_TARGET:-<no merge commits on this ref>}"
echo '```'
echo
if [[ -n "${TAG_TARGET}" ]]; then
  echo "Tag \`<new-version>\` should point at \`$(echo "${TAG_TARGET}" | awk '{print $1}' | cut -c1-7)\`."
else
  echo "No merge commits on this ref — for the inaugural release, tag HEAD (or the"
  echo "first commit on main) directly. See references/prior-release-format.md."
fi
