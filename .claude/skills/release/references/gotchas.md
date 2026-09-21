# Release-flow gotchas

This page records failure modes in the AeroGauge release workflow. Read it
before cutting a tag.

## Draft releases and release URLs

GitHub gives a draft release a temporary `untagged-<id>` URL. A release that
must be published should be created with `draft=false`. If a review is needed,
create the release with the final tag and change the draft state deliberately.

The workflow defaults both `draft` and `prerelease` to true. The release
script passes false for both when it is asked to publish a release:

`.claude/skills/release/scripts/tag-and-dispatch.sh <version>`

## Tag the merge commit

Tag the commit that landed on `main`, not an arbitrary feature commit. This
keeps the source, dependency pins, and release notes aligned.

The release script checks that local `main` matches `origin/main` and that the
commit has a second parent. If the target tag must be cut from a non-merge
commit, follow the manual fallback printed by the script and record that
exception in the pull request.

## GitHub flags and release wording

The GitHub `prerelease` flag controls the badge and filtering in the release
list. Text such as "pre-release quality" is only release-note wording. Choose
the flag and the wording separately, then check both with:

`.claude/skills/release/scripts/verify-release.sh <version>`

## Replace workflow placeholder notes

The workflow creates the release with a short automated note. That note is
not sufficient for players. Draft the complete body from
`release-notes-template.md`, then run:

`.claude/skills/release/scripts/update-notes.sh <version> <notes-file>`

The script refuses empty notes, the automated placeholder, and the known
unrendered template tokens.

## Quick verification

After the build finishes, use
`.claude/skills/release/scripts/wait-for-build.sh <run-id>` and then
`verify-release.sh`. Check the tag, release URL, draft and prerelease flags,
and all expected platform archives (Linux, Windows, and Android). If the
workflow gains another asset, add it to `EXPECTED_ASSETS` in
`verify-release.sh`; `scripts/check_docs.py` fails when the two disagree.
