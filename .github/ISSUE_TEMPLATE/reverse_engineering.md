---
name: Reverse-engineering finding
about: Record a measured ROM, runtime, or renderer finding
title: "[RE] "
labels: ""
assignees: ""
---

Use this issue when you are trying to answer a question about the original
game, the port, guest memory, or the renderer before making a code change.
Examples include an input layout, a display-list boundary, a ROM data format,
or ownership of a guest-memory field.

This is a work record, not permanent documentation. Once the finding is
confirmed, move the lasting rule into the owning source comment, focused test,
or stable subsystem page, and link this issue or its pull request. Keep the
issue open only while the measurement or follow-up is still needed.

## Question

What are you trying to establish?

## Fixed inputs

- Port commit:
- ROM name, region, size, and hash:
- Host operating system and architecture:
- Renderer/backend, if relevant:
- Tool or capture method:

## Observation

Give the address, bytes, display-list range, log lines, or other evidence.
Include the command or exact steps needed to reproduce it.

~~~text
evidence or command
~~~

## Hypothesis

What do you think the observation means?

What would disprove it?

## Proposed repository change

- [ ] Issue record only
- [ ] Stable symbol or named table
- [ ] TOML hook or stub
- [ ] Generated output after regeneration
- [ ] Hand-written port code
- [ ] Dependency patch
- [ ] Regression test
- [ ] Stable reference or subsystem page

List the files you expect to change. Generated files must be identified as
generated and must not be edited by hand.

## Validation and uncertainty

- Tests or captures run:
- Tests skipped and why:
- Expected result:
- Actual result:
- Remaining uncertainty:
- Question for pull-request review:
