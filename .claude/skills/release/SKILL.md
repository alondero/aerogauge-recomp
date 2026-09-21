---
name: release
description: User-invoked skill for cutting a new release of this project. Walks through tagging the merge commit, dispatching the Build & Release workflow, waiting for the binaries, and replacing the workflow's placeholder notes with the rich release-notes body. Deterministic steps live in .claude/skills/release/scripts/ — Claude fills in the variable parts (the notes body, the headline).
disable-model-invocation: true
---

# Release

Cut a new release of **AeroGauge: Recompiled**. The pattern is intentionally
narrow: this project's releases follow a fixed tag-and-build sequence, and
the only variable part is the release-notes body. The release scripts in
.claude/skills/release/scripts/ do everything that is deterministic; Claude
fills in the prose.

The expected output is a published GitHub release for the requested tag with
`draft: false, prerelease: false`, titled `AeroGauge Recompiled <version>`, the
workflow's three build artifacts (Linux, Windows, and Android) attached, and a
rich release-notes body replacing the `Automated build from commit ...`
placeholder.

## Prerequisites

This skill assumes `.github/workflows/build-release.yml` exists in the
repository. Verify with:

```bash
gh workflow list --repo alondero/aerogauge-recomp
```

If the output is empty, this repository does not have the workflow needed for
this release. Stop and record that gap in the release issue or pull request;
do not copy another project's workflow without a project-specific review.
The release scripts cannot run if the workflow is missing.

## When to invoke

The user says something like "create a release", "cut a v0.1.0", "ship what
we've got", "release 0.2.0", or references this skill by name (`/release`).

## Inputs

- **version** (required) — e.g. `v0.1.0`. Must match `v<MAJOR>.<MINOR>.<PATCH>`.
- **base tag** (optional, default: previous release tag) — for the diff
  ("Closed since v0.1.0"). If this is the first release, use the SHA of the
  very first commit on `main` (or omit the section — see
  `references/prior-release-format.md`).

## Flow

The skill runs five scripts in order. After each, Claude reads the output and
either proceeds or surfaces a diagnostic. The scripts are designed to be
re-runnable; if one fails partway, fix the upstream issue and re-run.

### 1. List changes since the base tag

```
./.claude/skills/release/scripts/list-changes.sh <base-tag>
```

Prints a structured summary of:

- merged PRs since `<base-tag>` (number, title, merge date)
- closed issues since `<base-tag>` (number, title)
- the merge commit at HEAD (where the new tag will land)

Use this to draft the "Closed since <prev-version>" section of the notes.
Read each PR's body via `gh pr view <N>` for the prose context.

For a first release with no previous tag, pass the SHA of the first commit on
`main` instead of a tag — the script accepts any `git rev-parse`-able ref.

### 2. Tag the merge commit and dispatch the workflow

```
./.claude/skills/release/scripts/tag-and-dispatch.sh <version>
```

Does, in order:

1. Find the merge commit at HEAD on `main`
2. `git tag <version> <merge-sha>`
3. `git push origin <version>`
4. `gh workflow run "Build & Release" --ref main \
        -f tag=<version> -f prerelease=false -f draft=false`

The script passes `--prerelease=false --draft=false` for a published release.
Those values avoid the `untagged-<id>` URL trap documented in
`references/gotchas.md`.

Prints the workflow run ID — needed by step 3.

### 3. Wait for the build

```
./.claude/skills/release/scripts/wait-for-build.sh <run-id>
```

`gh run watch <run-id> --exit-status --interval 30` — blocks until the run
finishes. Expected wall time: ~5 min for Linux, ~10-13 min for Windows, ~5 s
for the `release` job. Exits non-zero on build failure.

### 4. Verify the release is properly bound

```
./.claude/skills/release/scripts/verify-release.sh <version>
```

Checks, in order:

1. Git tag exists on origin (`git ls-remote --tags origin <version>`)
2. Release accessible via `gh api .../releases/tags/<version>` (not 404)
3. `html_url` ends with `releases/tag/<version>` (NOT `untagged-<id>`)
4. `isDraft: false`, `isPrerelease: false`
5. All expected assets uploaded
   (`aerogauge-recomp-{linux,windows}-x64.zip` and
   `aerogauge-recomp-android-arm64.apk`)

If any check fails, prints a diagnostic and exits non-zero. Common failures:

- workflow hasn't finished → re-run after a moment
- URL is `untagged-<id>` → the `--draft` trap; see `references/gotchas.md`

### 5. Update the notes

```
./.claude/skills/release/scripts/update-notes.sh <version> <path-to-notes.md>
```

`gh release edit <version> --title <title> --notes-file <path>` — replaces the
workflow's placeholder notes with the rich body **and** sets the human-facing
release title. The notes file should match the template in
`references/release-notes-template.md`.

The workflow always creates the release with `--title` set to the bare tag, so
without this step the release is titled `v0.1.0` instead of
`AeroGauge Recompiled v0.1.0`. The script applies the project prefix; export
`RELEASE_TITLE` to override it for a one-off.

## Variable parts (handled by Claude)

- **Release-notes body** — drafted from `references/release-notes-template.md`,
  filled in with the PR/issue summary from step 1 and prose from each PR body.
  Output goes in `.claude/skills/release/drafts/<version>-release-notes.md`
  for the user to review, then handed to step 5. The `drafts/` directory has
  a self-cleaning `.gitignore` so drafts never accidentally get committed.
- **Headline of the "What's working" section** — pick the single most
  user-visible change since the base tag. For a first release, describe the
  initial player path. Follow-up releases use the section structure in
  `references/prior-release-format.md`.
- **"What's not done yet" follow-ups** — any open issues referenced in the
  PRs that ship as known gaps. Name only limitations that still exist in the
  target release.

## Reference files

Read these when you need them — don't load them all upfront.

- `references/release-notes-template.md` — the skeleton with all required
  sections and placeholder text.
- `references/gotchas.md` — four release-flow failure modes: the `--draft`
  URL trap, merge-commit tagging, GitHub flag values versus body copy, and
  workflow placeholder notes. Read this before doing anything.
- `references/prior-release-format.md` — the inaugural-release caveat and
  the section structure for future notes.
