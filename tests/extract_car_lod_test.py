"""Extract the real car visibility/model/animation call closure, including hooks."""
import re
import sys
from pathlib import Path

source, destination = map(Path, sys.argv[1:3])
japanese = len(sys.argv) > 3 and sys.argv[3] == "jp"
functions = {}
for path in source.glob("*.c"):
    for match in re.finditer(r"RECOMP_FUNC void (\w+)\(.*?(?=RECOMP_FUNC void |\Z)",
                             path.read_text(encoding="utf-8"), re.S):
        functions[match[1]] = match[0]
pending = (["jp_func_80007980", "jp_func_8005A38C", "jp_func_8005A660"] if japanese
           else ["func_80007538", "func_8005A034", "func_8005A2D4"])
natives = {"aero_car_lod_visibility", "aero_car_lod_model",
           "aero_car_lod_mesh_mode", "aero_car_lod_animation_mode"}
seen = set()
while pending:
    name = pending.pop()
    if name in seen:
        continue
    seen.add(name)
    if name not in natives:
        pending.extend(re.findall(r"\b(\w+)\(rdram, ctx\);", functions[name]))
if not natives <= seen:
    raise SystemExit("Missing car LOD hooks; regenerate RecompiledFuncs")
destination.write_text('#include "recomp.h"\n' +
    "\n".join(f"void {name}(uint8_t*, recomp_context*);" for name in sorted(seen)) + "\n" +
    "\n".join(functions[name] for name in sorted(seen - natives)), encoding="utf-8")
