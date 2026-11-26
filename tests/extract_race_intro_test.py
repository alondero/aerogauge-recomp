"""Extract the current translation and an unhooked race-intro reference."""
import re
import sys
from pathlib import Path

source, output = map(Path, sys.argv[1:3])
region = sys.argv[3]
if region == "jp":
    prefix = "jp_func_"
    entries = ("8000DDA0", "8000E144", "80019C94")
    helpers = (
        "8000E390", "8001FF64", "80020014", "80020048", "8002025C",
        "800218E8", "8006DE94", "80026470",
    )
else:
    prefix = "func_"
    entries = ("8000D708", "8000D97C", "800191FC")
    helpers = (
        "8000D670", "8000DE54", "8000DEDC", "8001F6B4", "8001F75C",
        "8001F790", "8001F998", "80020EFC", "8006CD94", "80025840",
    )

entry_names = [prefix + addr for addr in entries]
names = set(entry_names + [prefix + addr for addr in helpers])
pattern = re.compile(r"^RECOMP_FUNC void (\w+)\(.*?(?=^RECOMP_FUNC|\Z)", re.M | re.S)
bodies = {}
for path in source.glob("*.c"):
    for match in pattern.finditer(path.read_text(encoding="utf-8")):
        name = match[1]
        if name in names:
            if name in bodies:
                raise SystemExit(f"Duplicate definition for {name}")
            bodies[name] = match[0]

if missing := names - bodies.keys():
    raise SystemExit(f"Missing race intro functions {sorted(missing)}")

entry_pattern = re.compile(r"\b(" + "|".join(entry_names) + r")\b")
hook = re.compile(r"^ +extern void aero_intro_draw_\w+\([^\n]+\n", re.M)
reference = []
for name in entry_names:
    original, removed = hook.subn("", bodies[name])
    if removed != 1:
        raise SystemExit(f"Expected exactly one native entry adapter in {name}")
    reference.append(entry_pattern.sub(r"reference_\1", original))
for name in names - set(entry_names):
    if hook.search(bodies[name]):
        raise SystemExit(f"Unexpected intro hook in shared routine {name}")

output.write_text('#include "recomp.h"\n'
                  + (source / "funcs.h").read_text(encoding="utf-8")
                  + "".join(f"recomp_func_t reference_{name};\n" for name in entry_names)
                  + "\n\n".join(bodies[name] for name in sorted(bodies))
                  + "\n\n" + "\n\n".join(reference) + "\n",
                  encoding="utf-8")
