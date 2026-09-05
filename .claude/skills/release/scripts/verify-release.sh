#!/usr/bin/env bash
# verify-release.sh — sanity-check a release is properly bound and complete.
# Runs five checks and prints pass/fail per check; exits non-zero if any fails.
#
# Usage: verify-release.sh <version>
#   version: e.g. v0.1.0

set -uo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 <version>" >&2
  exit 2
fi

VERSION="$1"
EXPECTED_ASSETS=(
  "aerogauge-recomp-linux-x64.zip"
  "aerogauge-recomp-windows-x64.zip"
)

PASS=0
FAIL=0

note_pass() { echo "  ✓ $1"; PASS=$((PASS+1)); }
note_fail() { echo "  ✗ $1"; FAIL=$((FAIL+1)); }

# Derive OWNER/REPO from gh repo view so forks / staging repos work too.
# `gh repo view` defaults to the current directory's repo.
if ! OWNER_REPO="$(gh repo view --json nameWithOwner --jq '.nameWithOwner' 2>/dev/null)"; then
  echo "error: failed to determine repo from gh CLI (is gh authenticated?)." >&2
  exit 1
fi

echo "Verifying ${VERSION} on ${OWNER_REPO}:"
echo

# Check 1: git tag on origin
if TAG_SHA="$(git ls-remote --tags origin "${VERSION}" 2>/dev/null | awk '{print $1}')" \
   && [[ -n "${TAG_SHA}" ]]; then
  note_pass "tag ${VERSION} exists on origin (${TAG_SHA:0:7})"
else
  note_fail "tag ${VERSION} NOT on origin"
fi

# Single API call for everything else. Capture exit status so we can fail loudly
# on auth / rate-limit / network errors instead of false-positive passing.
RELEASE_JSON=""
if ! RELEASE_JSON="$(gh api "repos/${OWNER_REPO}/releases/tags/${VERSION}" 2>&1)"; then
  note_fail "release API call failed: ${RELEASE_JSON}"
  echo
  echo "Result: ${PASS} passed, ${FAIL} failed"
  exit 1
fi

# Check 2: release accessible (jq parses cleanly = present)
if [[ "$(jq -r '.message // empty' <<< "${RELEASE_JSON}")" == "Not Found" ]]; then
  note_fail "release not found via /releases/tags/${VERSION} (workflow may not have finished)"
  echo
  echo "Result: ${PASS} passed, ${FAIL} failed"
  exit 1
fi
note_pass "release accessible via tag-keyed API"

# Check 3: html_url is properly tag-bound (not the untagged-<id> synthetic URL)
HTML_URL="$(jq -r '.html_url' <<< "${RELEASE_JSON}")"
if [[ "${HTML_URL}" == *"releases/tag/${VERSION}" ]]; then
  note_pass "html_url is properly tag-bound: ${HTML_URL}"
elif [[ "${HTML_URL}" == *"untagged-"* ]]; then
  note_fail "html_url is the synthetic 'untagged-<id>' pattern — the --draft trap"
  note_fail "  url: ${HTML_URL}"
  echo "         See references/gotchas.md — release was created as a draft."
else
  note_fail "html_url unexpected: ${HTML_URL:-<empty>}"
fi

# Check 4: draft and prerelease flags (jq handles bool cleanly, no regex)
IS_DRAFT="$(jq -r '.draft' <<< "${RELEASE_JSON}")"
IS_PRERELEASE="$(jq -r '.prerelease' <<< "${RELEASE_JSON}")"
if [[ "${IS_DRAFT}" == "false" ]]; then
  note_pass "draft: false"
else
  note_fail "draft: ${IS_DRAFT:-<unset>} (expected false to match prior releases)"
fi
if [[ "${IS_PRERELEASE}" == "false" ]]; then
  note_pass "prerelease: false"
else
  note_fail "prerelease: ${IS_PRERELEASE:-<unset>} (expected false — 'Pre-release quality' is body copy only)"
fi

# Check 5: assets uploaded. jq walks .assets[] reliably — no field-order
# assumptions (JSON objects are unordered per RFC 8259), no regex, no second
# API call.
ASSETS_JSON="$(jq -c '.assets // []' <<< "${RELEASE_JSON}")"
for expected in "${EXPECTED_ASSETS[@]}"; do
  ASSET_ENTRY="$(jq -c --arg name "${expected}" '.[] | select(.name == $name)' <<< "${ASSETS_JSON}")"
  if [[ -n "${ASSET_ENTRY}" && "${ASSET_ENTRY}" != "null" ]]; then
    SIZE="$(jq -r '.size' <<< "${ASSET_ENTRY}")"
    SIZE_MB="$(awk "BEGIN { printf \"%.0f\", ${SIZE:-0}/1024/1024 }")"
    note_pass "asset uploaded: ${expected} (${SIZE_MB} MB)"
  else
    note_fail "asset missing: ${expected}"
  fi
done

echo
echo "Result: ${PASS} passed, ${FAIL} failed"

if [[ ${FAIL} -gt 0 ]]; then
  exit 1
fi
exit 0
