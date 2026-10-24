"""Extract the ROM's damage block and complete standings updater, with hooks."""
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
damage_name, rank_name, projection = (
    ("jp_func_80058308", "jp_func_8001D720", "jp_func_8001DDB0") if japanese else
    ("func_80058114", "func_8001CF48", "func_8001D598"))
start, stop = ("80058D2C", "80058DA4") if japanese else ("80058AD8", "80058B4C")
damage = functions[damage_name]
begin = damage.index(f"    // 0x{start}:")
end = damage.index(f"L_{stop}:", begin)
body = damage[begin:end]
# The zero-damage USA branch preloads t4 in its delay slot. The repeated load
# before its target is harmless and is included verbatim with the arithmetic.
if body.count("aero_cheat_damage(rdram, ctx);") != 1:
    raise SystemExit("Missing damage hook; regenerate translated output")
if damage.index("aero_haptics_race_tick(rdram, ctx);") > begin:
    raise SystemExit("Collision haptics must observe damage before the cheat")
rank = functions[rank_name]
if rank.count("aero_cheat_standings(rdram, ctx);") != 1:
    raise SystemExit("Missing standings hook; regenerate translated output")
calls = set(re.findall(r"\b(\w+)\(rdram, ctx\);", rank))
if calls != {projection, "aero_cheat_standings"}:
    raise SystemExit(f"Standings call boundary changed: {calls}")
destination.write_text(
    '#include "recomp.h"\n'
    f'void {projection}(uint8_t*, recomp_context*);\n'
    'void test_rom_damage(uint8_t* rdram, recomp_context* ctx) {\n'
    '    int c1cs = 0;\n' + body + f'\nL_{stop}:;\n}}\n' + rank,
    encoding="utf-8")
