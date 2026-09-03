#!/usr/bin/env bash
# tag-and-dispatch.sh — tag the merge commit at HEAD on main, push the tag,
# and dispatch the Build & Release workflow with --prerelease=false --draft=false
# (the values that produce a properly-bound release).
#
# IMPORTANT: dispatches against the TAG itself, not main, so CI's checkout is
# pinned to the immutable ref. This eliminates the source/binary-mismatch risk
# where CI would otherwise build whatever HEAD was on main at dispatch time.
# The workflow file pins actions/checkout@v4 to ${{ inputs.tag }} as well
# (defense in depth).
#
# Usage: tag-and-dispatch.sh <version>
#   version: e.g. v0.1.0 (must match v<MAJOR>.<MINOR>.<PATCH>)

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 <version>" >&2
  exit 2
fi

VERSION="$1"

# Validate version shape
if ! [[ "${VERSION}" =~ ^v[0-9]+\.[0-9]+\.[0-9]+(-[a-zA-Z0-9.]+)?$ ]]; then
  echo "error: '${VERSION}' does not match v<MAJOR>.<MINOR>.<PATCH>" >&2
  exit 1
fi

# Make sure we're on main
CURRENT_BRANCH="$(git rev-parse --abbrev-ref HEAD)"
if [[ "${CURRENT_BRANCH}" != "main" ]]; then
  echo "error: must be on 'main' (currently on '${CURRENT_BRANCH}')." >&2
  echo "       Switch with: git checkout main" >&2
  exit 1
fi

# Sync local main with origin/main before tagging — tag stale local commits
# and the tag's history diverges from what collaborators see.
git fetch origin main
HEAD_SHA="$(git rev-parse HEAD)"
REMOTE_SHA="$(git rev-parse origin/main)"
if [[ "${HEAD_SHA}" != "${REMOTE_SHA}" ]]; then
  echo "error: local main (${HEAD_SHA:0:7}) != origin/main (${REMOTE_SHA:0:7})." >&2
  echo "       Run: git pull --ff-only origin main" >&2
  exit 1
fi

# HEAD must be a merge commit. Check via the second parent — O(1), standard.
# For inaugural releases where main's HEAD is a squash commit, the escape
# hatch is to tag HEAD directly and dispatch manually.
if ! git rev-parse -q --verify "HEAD^2" >/dev/null 2>&1; then
  echo "error: HEAD (${HEAD_SHA:0:7}) is not a merge commit." >&2
  echo "       Past releases tag at a merge commit. For the inaugural release," >&2
  echo "       tag HEAD manually and dispatch with:" >&2
  echo "         git tag ${VERSION} HEAD && git push origin ${VERSION}" >&2
  echo "         gh workflow run 'Build & Release' --ref ${VERSION} \\" >&2
  echo "           -f tag=${VERSION} -f prerelease=false -f draft=false" >&2
  exit 1
fi

# Refuse if tag already exists locally OR on origin
if git rev-parse -q --verify "refs/tags/${VERSION}" >/dev/null; then
  echo "error: tag '${VERSION}' already exists locally." >&2
  echo "       If you want to re-cut, delete first: git tag -d ${VERSION}" >&2
  exit 1
fi
if git ls-remote --tags origin "${VERSION}" 2>/dev/null | grep -q "${VERSION}"; then
  echo "error: tag '${VERSION}' already exists on origin." >&2
  exit 1
fi

echo "→ Tagging ${VERSION} at ${HEAD_SHA}"
git tag "${VERSION}" "${HEAD_SHA}"

echo "→ Pushing ${VERSION} to origin"
git push origin "${VERSION}"

echo "→ Dispatching Build & Release workflow against ${VERSION} (prerelease=false, draft=false)"
# Dispatch against the TAG, not main. With the workflow's checkout also pinned
# to ${{ inputs.tag }}, the build source is unambiguously the tagged commit.
gh workflow run "Build & Release" --ref "${VERSION}" \
  -f "tag=${VERSION}" \
  -f "prerelease=false" \
  -f "draft=false"

# Find the dispatched run. gh workflow run is fire-and-forget; poll the run
# list with backoff. Sort by createdAt (newest first) so we pick the just-
# dispatched run, not whatever happened to finish most recently. Exit non-zero
# on timeout — orchestrators must NOT proceed with an unknown run ID.
RUN_ID=""
for _attempt in $(seq 1 15); do
  RUN_ID="$(gh run list --workflow="Build & Release" --limit 20 \
              --json databaseId,status,createdAt \
              --jq 'sort_by(.createdAt) | reverse | .[] | select(.status=="in_progress" or .status=="queued") | .databaseId' \
              2>/dev/null | head -n 1 || true)"
  if [[ -n "${RUN_ID}" ]]; then
    break
  fi
  sleep 2
done

if [[ -z "${RUN_ID}" ]]; then
  echo "error: timed out locating dispatched run. List manually:" >&2
  echo "         gh run list --workflow='Build & Release' --limit 5" >&2
  exit 1
fi

echo
echo "✓ Workflow dispatched."
echo "  Version:    ${VERSION}"
echo "  Tag SHA:    ${HEAD_SHA}"
echo "  Run ID:     ${RUN_ID}"
echo
echo "Next: ./scripts/wait-for-build.sh ${RUN_ID}"
