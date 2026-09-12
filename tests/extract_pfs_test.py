"""Extract the original recompiled PFS call closure for a ROM-backed host test.

Output stays in build/: never commit ROM-derived C. Missing bodies/native dependencies
fail the build rather than silently substituting filesystem behavior.
"""
import re
import sys
from pathlib import Path

source, destination = map(Path, sys.argv[1:])
functions = {}
for path in source.glob("*.c"):
    for match in re.finditer(r"RECOMP_FUNC void (\w+)\(.*?(?=RECOMP_FUNC void |\Z)", path.read_text(encoding="utf-8"), re.S):
        functions[match[1]] = match[0]
pending = ["func_8006B440", "func_8006F040", "func_8006CDE0", "func_8006EC1C", "func_8006CFA0"]
seen = set()
natives = {"aero_pak_read", "aero_pak_write", "aero_pak_status", "osCreateMesgQueue_recomp",
           "osRecvMesg_recomp", "osSendMesg_recomp", "osGetCount_recomp"}
while pending:
    name = pending.pop()
    if name in seen:
        continue
    seen.add(name)
    if name in natives:
        continue
    body = functions[name]
    pending.extend(re.findall(r"\b(\w+)\(rdram, ctx\);", body))
destination.write_text('#include "recomp.h"\n' +
    "\n".join(f"void {name}(uint8_t*, recomp_context*);" for name in sorted(seen)) + "\n" +
    "\n".join(functions[name] for name in sorted(seen - natives)), encoding="utf-8")
