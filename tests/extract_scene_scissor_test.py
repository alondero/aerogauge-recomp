"""Extract the ROM's scissor builder, including configured hooks, into build/."""
import re
import sys
from pathlib import Path

source, destination = map(Path, sys.argv[1:])
matches = []
for path in source.glob("*.c"):
    matches.extend(re.findall(
        r"RECOMP_FUNC void func_800227E4\(.*?(?=RECOMP_FUNC void |\Z)",
        path.read_text(encoding="utf-8"), re.S))
if len(matches) != 1:
    raise SystemExit("Expected exactly one generated scene scissor builder")
destination.write_text('#include "recomp.h"\n'
    'void osVirtualToPhysical_recomp(uint8_t*, recomp_context*);\n' + matches[0],
    encoding="utf-8")
