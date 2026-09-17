<!-- Keep the summary and evidence concrete. The pull request is where the
human maintainer reviews support and architecture decisions. -->

## Summary

What changed, and why is it the smallest useful change?

## Scope

- [ ] Documentation-only change.
- [ ] Runtime or player behavior change.
- [ ] Generated input or generated output change.
- [ ] Dependency patch or renderer boundary change.

Affected seam and owner:

Relevant issue or design question:

Questions or trade-offs for review:

## Verification

- Operating system:
- Compiler and toolchain:
- Graphics backend:
- Release or commit tested:
- ROM region and hash, if ROM-backed:
- Exact commands run:
- Test results:
- Manual checks or captures:

## Generated and dependency state

- [ ] No generated files were edited by hand.
- [ ] Generated inputs were changed and output was regenerated.
- [ ] Generated output was freshly created for verification.
- [ ] Dependency patches still apply through the supported scripts.
- [ ] Upstream comparison was updated when dependency behavior changed.

Generated files changed:

Dependency patches changed:

## Documentation and limitations

- Documentation files changed:
- User-facing claim changed:
- Known limitations:
- Checks not run and why:
- Logs, screenshots, or captures:

## Review checklist

- [ ] Guest/host ownership and thread boundaries are documented where needed.
- [ ] Fixed guest addresses include units, byte order, and evidence.
- [ ] Comments explain purpose or invariants rather than repeating code.
- [ ] No private-session links, absolute machine paths, ROM bytes, or
      unexplained foreign issue references were added.
- [ ] python -B scripts/check_docs.py passes when documentation changed.
- [ ] git diff --check passes.

Recommended next step:

<!-- Do not include private AI-session links, absolute local paths, or claims
about a run that was not performed. -->
