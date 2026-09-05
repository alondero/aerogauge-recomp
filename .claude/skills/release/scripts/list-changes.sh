#!/usr/bin/env bash
# list-changes.sh — print merged PRs, closed issues, and the merge-commit at HEAD
# since <base-tag>. Output is structured Markdown for the release-notes draft.
#
# Usage: list-changes.sh <base-tag> [head-ref]
#   base-tag: e.g. v0.1.0 — or any rev-parseable ref for the inaugural release
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

BASE_DATE_ISO="$(git log -1 --format='%cI' "${BASE_TAG}")"   # committer date, ISO 8601 with TZ
HEAD_SHA="$(git rev-parse "${HEAD_REF}")"
HEAD_SHORT="$(git rev-parse --short "${HEAD_SHA}")"
HEAD_SUBJECT="$(git log -1 --format='%s' "${HEAD_REF}")"
HEAD_DATE="$(git log -1 --format='%ai' "${HEAD_REF}")"

echo "## Change summary: ${BASE_TAG} → ${HEAD_REF}"
echo
echo "**Base:** \`${BASE_TAG}\` (${BASE_DATE_ISO})"
echo "**Head:** \`${HEAD_SHORT}\` — ${HEAD_SUBJECT} (${HEAD_DATE})"
echo

# Commits since base
echo "### Commits since ${BASE_TAG}"
echo
echo '```'
git log --no-merges --oneline "${BASE_TAG}..${HEAD_REF}" || true
echo '```'
echo

# Merged PRs since base — use GitHub API with the JSON `mergedAt` field
# (ISO 8601 with TZ) and filter in jq. Compared to gh's date-string search
# (`merged:>=YYYY-MM-DD`), this:
#   - handles TZ correctly (ISO 8601 string compare is lexicographic and
#     TZ-stable; date-string search treats times as midnight UTC)
#   - handles same-day cutoffs correctly (no PR is re-attributed across
#     a release cut at 3 PM and another at 6 PM)
#   - covers both regular-merge and squash-merge PRs
#   - handles deleted-author accounts via `// "ghost"` (was a real bug:
#     jq's `\(.author.login)` crashed on null authors)
echo "### Merged PRs since ${BASE_TAG}"
echo
PRS="$(gh pr list --state merged --base main --limit 200 --json number,title,mergedAt,author \
       --jq --arg base "${BASE_DATE_ISO}" \
       '[.[] | select(.mergedAt >= $base)] | sort_by(.mergedAt) | .[] | "- **PR #\(.number)** (\(.mergedAt[:10])) — \(.title) — _@\(.author.login // "ghost")_"')"
if [[ -n "${PRS}" ]]; then
  echo "${PRS}"
else
  echo "_None merged to main since \`${BASE_TAG}\`._"
fi
echo

# Closed issues since base — same pattern as PRs (jq select on closedAt).
# Note: `--limit 100` is a soft cap; for repos with >100 closed issues in a
# release window, paginate manually (rare for a single release cycle).
echo "### Closed issues since ${BASE_TAG}"
echo
ISSUES="$(gh issue list --state closed --limit 200 --json number,title,closedAt \
          --jq --arg base "${BASE_DATE_ISO}" \
          '[.[] | select(.closedAt >= $base)] | sort_by(.closedAt) | .[] | "- **#\(.number)** (\(.closedAt[:10])) — \(.title)"')"
if [[ -n "${ISSUES}" ]]; then
  echo "${ISSUES}"
else
  echo "_None closed since \`${BASE_TAG}\`._"
fi
echo

# Tag target recommendation — show the merge commit at HEAD (the canonical tag
# landing point). For squash-merge repos where HEAD isn't a true merge commit,
# print the most recent merge commit on the path instead.
echo "### Tag target recommendation"
echo
TAG_TARGET="$(git log --merges --first-parent -n 1 --format='%H %s' "${HEAD_REF}" || true)"
echo "Most recent merge commit on \`${HEAD_REF}\`:"
echo
echo '```'
echo "${TAG_TARGET:-<no merge commits on this ref — inaugural release>}"
echo '```'
echo
if [[ -n "${TAG_TARGET}" ]]; then
  echo "Tag \`<new-version>\` should point at \`$(echo "${TAG_TARGET}" | awk '{print $1}' | cut -c1-7)\`."
else
  echo "No merge commits on this ref — for the inaugural release, tag HEAD (or the"
  echo "first commit on main) directly. See references/prior-release-format.md."
fi
