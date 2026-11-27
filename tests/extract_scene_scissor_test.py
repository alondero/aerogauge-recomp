"""Extract the live scissor builder and its unhooked ROM reference into build/."""
import re
import sys
from pathlib import Path

source, destination = map(Path, sys.argv[1:3])
name = "jp_func_80023238" if len(sys.argv) > 3 and sys.argv[3] == "jp" else "func_800227E4"
functions = {}
for path in source.glob("*.c"):
    for match in re.finditer(
            r"RECOMP_FUNC void (\w+)\(.*?(?=RECOMP_FUNC void |\Z)",
            path.read_text(encoding="utf-8"), re.S):
        functions[match[1]] = match[0]
if name not in functions:
    raise SystemExit("Expected exactly one generated scene scissor builder")

signature, body = functions[name].split("{", 1)
hook = ("extern void aero_build_scene_viewport(uint8_t*, recomp_context*); "
        "aero_build_scene_viewport(rdram, ctx); return;")
prologue = body.split("    // 0x", 1)[0]
if hook not in prologue or body.count(hook) != 1:
    raise SystemExit(f"Missing scene viewport entry replacement for {name}; regenerate RecompiledFuncs")
reference = signature.replace(name, "rom_" + name) + "{" + body.replace(hook, "", 1)

destination.write_text('#include "recomp.h"\n'
    'void osVirtualToPhysical_recomp(uint8_t*, recomp_context*);\n'
    + functions[name] + "\n" + reference,
    encoding="utf-8")
