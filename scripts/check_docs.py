"""Check repository Markdown without needing a ROM or build.

Historical notes and the documentation audit are included, so a missing local
script or broken link cannot hide in the research path.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path, PurePosixPath, PureWindowsPath
from urllib.parse import unquote, urlsplit


ROOT = Path(__file__).resolve().parent.parent
EXCLUDED: set[Path] = set()
ROOT_MARKDOWN = [
    ROOT / "README.md",
    ROOT / "BUILDING.md",
    ROOT / "CONTRIBUTING.md",
    ROOT / "CLAUDE.md",
    ROOT / ".github" / "pull_request_template.md",
]
REQUIRED = ROOT_MARKDOWN + [
    ROOT / "docs" / "index.md",
    ROOT / "docs" / "architecture.md",
    ROOT / "docs" / "testing.md",
    ROOT / "docs" / "debugging.md",
    ROOT / "docs" / "configuration.md",
    ROOT / "docs" / "glossary.md",
    ROOT / "docs" / "reference" / "rom.md",
    ROOT / "docs" / "reference" / "runtime.md",
    ROOT / "docs" / "reference" / "renderer.md",
    ROOT / "docs" / "reference" / "audio.md",
    ROOT / "docs" / "investigations" / "index.md",
    ROOT / "docs" / "decisions" / "index.md",
    ROOT / ".github" / "ISSUE_TEMPLATE" / "bug_report.md",
    ROOT / ".github" / "ISSUE_TEMPLATE" / "reverse_engineering.md",
    ROOT / ".github" / "ISSUE_TEMPLATE" / "feature_request.md",
]

LINK_RE = re.compile(r"\[[^\]\n]+\]\(([^)\n]+)\)")
SCRIPT_RE = re.compile(
    r"(?<![\w./-])"
    r"((?:scripts|tests|tools)/[\w./-]+\.(?:py|ps1|sh|gdb|toml)"
    r"|build\.(?:ps1|sh))"
)
DRIVE_PATH_RE = re.compile(r"(?<![\w])(?:[A-Za-z]:[\\/]|\\\\)")
POSIX_PATH_RE = re.compile(r"(?<![\w:])/(?:Users|home|mnt|private|var)/")
PRIVATE_URL_RE = re.compile(
    r"(?i)(?:claude\.ai|chat\.openai\.com|chatgpt\.com/share|openai\.com/share)"
)
PLACEHOLDER_URL_RE = re.compile(
    r"(?i)https?://(?:example\.(?:com|org|net)|localhost(?:[:/)]|$)|"
    r"github\.com/\.\.\.)"
)

STANDALONE_TESTS = [
    "tests/test_aspect_overscan.cpp",
    "tests/test_draw_distance.cpp",
    "tests/test_viewproj_decompose.cpp",
    "tests/test_warp_gating.cpp",
    "tests/test_audio_oversize_guard.cpp",
    "tests/test_savestate_roundtrip.ps1",
    "tests/test_savestate_hotkey.ps1",
]


def markdown_files() -> list[Path]:
    files = []
    for path in ROOT_MARKDOWN:
        if path.is_file():
            files.append(path)
    for path in (ROOT / ".github").rglob("*.md"):
        files.append(path)
    for path in (ROOT / "docs").rglob("*.md"):
        if path not in EXCLUDED:
            files.append(path)
    return sorted(set(files))


def line_number(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def local_link_target(raw_target: str) -> str | None:
    target = raw_target.strip()
    if target.startswith("<"):
        close = target.find(">")
        if close < 0:
            return target
        target = target[1:close]
    else:
        target = target.split()[0] if target.split() else ""
    if not target or target.startswith("#"):
        return None
    parsed = urlsplit(target)
    if parsed.scheme or parsed.netloc:
        return None
    return unquote(parsed.path)


def is_absolute_machine_path(target: str) -> bool:
    return (
        PureWindowsPath(target).drive != ""
        or PureWindowsPath(target).is_absolute()
        or PurePosixPath(target).is_absolute()
    )


def heading_anchors(text: str) -> set[str]:
    anchors: set[str] = set()
    counts: dict[str, int] = {}
    for line in text.splitlines():
        match = re.match(r"^\s{0,3}#{1,6}\s+(.+?)\s*#*\s*$", line)
        if not match:
            continue
        title = re.sub(r"<[^>]+>", "", match.group(1)).lower()
        slug = re.sub(r"[^\w\s-]", "", title, flags=re.UNICODE)
        slug = re.sub(r"\s+", "-", slug).strip("-")
        if not slug:
            continue
        index = counts.get(slug, 0)
        counts[slug] = index + 1
        anchors.add(slug if index == 0 else f"{slug}-{index}")
    anchors.update(re.findall(r"(?:id|name)=[\"']([^\"']+)[\"']", text, flags=re.IGNORECASE))
    return anchors


def main() -> int:
    errors: list[str] = []

    for path in REQUIRED:
        if not path.is_file():
            errors.append(f"missing required document: {path.relative_to(ROOT)}")

    files = markdown_files()
    for path in files:
        text = path.read_text(encoding="utf-8")
        rel = path.relative_to(ROOT)

        for match in LINK_RE.finditer(text):
            raw_target = match.group(1)
            target_text = raw_target.strip()
            if target_text.startswith("<"):
                close = target_text.find(">")
                if close < 0:
                    target_text = target_text[1:]
                else:
                    target_text = target_text[1:close]
            else:
                target_text = target_text.split()[0] if target_text.split() else ""
            parsed_target = urlsplit(target_text)
            target = local_link_target(raw_target)
            if target is None:
                continue
            if is_absolute_machine_path(target):
                errors.append(
                    f"{rel}:{line_number(text, match.start())}: absolute local link: {raw_target}"
                )
                continue
            resolved = (path.parent / target).resolve()
            if not resolved.exists():
                errors.append(
                    f"{rel}:{line_number(text, match.start())}: broken local link: {raw_target}"
                )
            elif parsed_target.fragment and resolved.suffix.lower() in {".md", ".markdown"}:
                target_anchors = heading_anchors(resolved.read_text(encoding="utf-8"))
                fragment = unquote(parsed_target.fragment)
                if fragment not in target_anchors:
                    errors.append(
                        f"{rel}:{line_number(text, match.start())}: broken local anchor: {raw_target}"
                    )

        for pattern, label in (
            (DRIVE_PATH_RE, "machine-specific drive or UNC path"),
            (POSIX_PATH_RE, "machine-specific POSIX path"),
            (PRIVATE_URL_RE, "private-session URL"),
            (PLACEHOLDER_URL_RE, "placeholder URL"),
        ):
            for match in pattern.finditer(text):
                errors.append(
                    f"{rel}:{line_number(text, match.start())}: {label}: {match.group(0)}"
                )

        for match in SCRIPT_RE.finditer(text):
            referenced = (ROOT / match.group(1)).resolve()
            if not referenced.is_file():
                errors.append(
                    f"{rel}:{line_number(text, match.start())}: "
                    f"missing referenced script: {match.group(1)}"
                )

    cmake_files = [ROOT / "CMakeLists.txt"]
    cmake_files.extend(sorted((ROOT / "cmake").rglob("*.cmake")))
    cmake = "\n".join(path.read_text(encoding="utf-8") for path in cmake_files)
    testing = (ROOT / "docs" / "testing.md").read_text(encoding="utf-8")
    ctest_names = re.findall(
        r"add_test\s*\(\s*NAME\s+([A-Za-z0-9_]+)", cmake, flags=re.IGNORECASE
    )
    for name in ctest_names:
        if name not in testing:
            errors.append(f"docs/testing.md does not document CTest name: {name}")

    for test_path in STANDALONE_TESTS:
        if Path(test_path).name not in testing:
            errors.append(f"docs/testing.md does not document standalone test: {test_path}")

    if errors:
        print("Documentation check failed:")
        for error in errors:
            print(f"- {error}")
        return 1

    print(f"Documentation check passed ({len(files)} Markdown files; no ROM required).")
    print(f"Checked {len(ctest_names)} CTest names against docs/testing.md.")
    print(f"Checked {len(STANDALONE_TESTS)} standalone test references against docs/testing.md.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
