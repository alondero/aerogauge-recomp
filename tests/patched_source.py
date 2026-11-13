"""Replay the build's dependency manifest without changing a developer checkout."""
import subprocess
from pathlib import Path
from scripts.dependency_patches import patch_plan

ROOT = Path(__file__).resolve().parents[1]
PLUME = ROOT / "lib/rt64/src/contrib/plume"


def series_for(platform):
    return {directory.relative_to(ROOT).as_posix(): patches
            for directory, patches in patch_plan(ROOT, platform).items()}


def method(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def patched_file(submodule, path, tree):
    target = Path(tree) / path
    target.parent.mkdir(parents=True, exist_ok=True)
    exists = subprocess.check_output(
        ["git", "ls-tree", "--name-only", "HEAD", "--", path], cwd=submodule)
    if exists:
        target.write_bytes(subprocess.check_output(["git", "show", f"HEAD:{path}"], cwd=submodule))
    else:
        target.unlink(missing_ok=True)
    # Use Android's composed series; Windows-only ABI patches do not alter these fixtures.
    for patch in patch_plan(ROOT, "android")[submodule]:
        if f"+++ b/{path}\n" in patch.read_text(encoding="utf-8"):
            result = subprocess.run(["git", "apply", "--ignore-whitespace", f"--include={path}", str(patch)],
                                    cwd=tree, capture_output=True, text=True)
            if result.returncode:
                raise RuntimeError(f"{patch.name}: {result.stderr}")
    return target.read_text(encoding="utf-8")


def patched_plume_vulkan(tree):
    return patched_file(PLUME, "plume_vulkan.cpp", tree)
