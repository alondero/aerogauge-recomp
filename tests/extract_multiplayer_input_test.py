"""Extract the ROM controller reader and its real pad-unpacking closure."""
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
pending = ["jp_func_8000982C" if japanese else "func_800092C4"]
natives = {"osContGetReadData_recomp"}
seen = set()
while pending:
    name = pending.pop()
    if name in seen:
        continue
    seen.add(name)
    if name not in natives:
        pending.extend(re.findall(r"\b(\w+)\(rdram, ctx\);", functions[name]))
destination.write_text('#include "recomp.h"\n' +
    "\n".join(f"void {name}(uint8_t*, recomp_context*);" for name in sorted(seen)) + "\n" +
    "\n".join(functions[name] for name in sorted(seen - natives)), encoding="utf-8")
