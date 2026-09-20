"""Regression: the Adreno system driver rejects the OR spelling of the 16-bit byte swap.

The Adreno 750 system Vulkan driver returns VK_ERROR_UNKNOWN from vkCreateComputePipelines
for any compute shader containing ((i << 8) & 0xFF00) | ((i >> 8) & 0xFF). plume keeps the
pipeline object with a null handle after that failure and the RT64 present thread then
crashes the driver inside vkCmdBindPipeline. Patch 0023 therefore has to spell the helper
in lib/rt64/src/shaders/FbCommon.hlsli with an operator the driver accepts, and that
spelling has to produce the same value as the rejected one.

The device measurement and its reduction are recorded in docs/reference/renderer.md.

The patch-content and equivalence checks run anywhere. The compile check needs the patch
applied to the lib/rt64 submodule, which the supported build scripts do, and the dxc
submodule; it skips with a stated reason when either is missing.
"""
import os
import re
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHADERS = ROOT / "lib/rt64/src/shaders"
RT64_SRC = ROOT / "lib/rt64/src"
HEADER = SHADERS / "FbCommon.hlsli"
DXC_DIR = RT64_SRC / "contrib/dxc"
PATCH = ROOT / "patches/0023-rt64-adreno-endian-swap.patch"

REJECTED_SWAP = "((i << 8) & 0xFF00) | ((i >> 8) & 0xFF)"
ACCEPTED_SWAP = "((i << 8) & 0xFF00) ^ ((i >> 8) & 0xFF)"

OP_BITWISE_OR = 197
OP_BITWISE_XOR = 198


def rejected_swap(i):
    """The spelling the driver rejects."""
    return ((i << 8) & 0xFF00) | ((i >> 8) & 0xFF)


def accepted_swap(i):
    """The spelling that ships."""
    return ((i << 8) & 0xFF00) ^ ((i >> 8) & 0xFF)


def helper_body():
    """The EndianSwapUINT16 body as checked out, or None without the submodule."""
    if not HEADER.is_file():
        return None
    match = re.search(r"uint EndianSwapUINT16\(uint i\)\s*\{(.*?)\}", HEADER.read_text(encoding="utf-8"), re.S)
    return match.group(1) if match else None


def opcodes_of(data):
    words = struct.unpack(f"<{len(data) // 4}I", data)
    found, index = [], 5
    while index < len(words):
        word_count, opcode = words[index] >> 16, words[index] & 0xFFFF
        if word_count == 0:
            break
        found.append(opcode)
        index += word_count
    return found


def dxc_command():
    """dxc plus the library directory it loads its DLLs from, or None when absent."""
    if sys.platform == "darwin":
        binary = DXC_DIR / "bin/x64/dxc-macos"
    elif os.name == "nt":
        binary = DXC_DIR / "bin/x64/dxc.exe"
    else:
        binary = DXC_DIR / "bin/x64/dxc-linux"
    if not binary.is_file():
        return None
    return binary, DXC_DIR / "lib/x64"


class EquivalentSpellingTests(unittest.TestCase):
    def test_accepted_spelling_equals_the_rejected_one(self):
        # The two masked fields are disjoint, so XOR cannot differ from OR.
        for i in range(0x10000):
            self.assertEqual(accepted_swap(i), rejected_swap(i), f"mismatch for {i:#x}")
        for i in (0x00010000, 0x00FF0000, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF):
            self.assertEqual(accepted_swap(i), rejected_swap(i), f"mismatch for {i:#x}")


class PatchTests(unittest.TestCase):
    def test_patch_rewrites_the_rejected_spelling(self):
        lines = PATCH.read_text(encoding="utf-8").splitlines()
        removed = [line[1:] for line in lines if line.startswith("-") and not line.startswith("---")]
        added = [line[1:] for line in lines if line.startswith("+") and not line.startswith("+++")]
        self.assertIn(REJECTED_SWAP, " ".join(removed),
                      "patch 0023 no longer removes the spelling the Adreno driver rejects")
        self.assertIn(ACCEPTED_SWAP, " ".join(added),
                      "patch 0023 no longer adds the accepted spelling")


class CompiledHelperTests(unittest.TestCase):
    def test_compiled_helper_avoids_the_rejected_operator(self):
        tool = dxc_command()
        if tool is None:
            self.skipTest("the dxc submodule is not initialized")
        body = helper_body()
        if body is None:
            self.skipTest("the lib/rt64 submodule is not initialized")
        if "|" in body:
            self.skipTest("patch 0023 is not applied to the lib/rt64 submodule")

        binary, library = tool
        probe = (
            '#include "FbCommon.hlsli"\n'
            "RWBuffer<uint> gOutput : register(u1, space0);\n"
            "[numthreads(8, 8, 1)]\n"
            "void CSMain(uint2 coord : SV_DispatchThreadID) {\n"
            "    gOutput[coord.x] = EndianSwapUINT16(coord.x * 3u + 1u);\n"
            "}\n"
        )
        with tempfile.TemporaryDirectory(prefix="aero-endian-swap-") as temporary:
            hlsl = Path(temporary) / "EndianSwapProbe.hlsl"
            spv = Path(temporary) / "EndianSwapProbe.spv"
            hlsl.write_text(probe, encoding="utf-8")
            environment = dict(os.environ)
            for variable in ("LD_LIBRARY_PATH", "DYLD_LIBRARY_PATH"):
                environment[variable] = f"{library}{os.pathsep}{environment.get(variable, '')}"
            result = subprocess.run(
                [str(binary), "-I", str(RT64_SRC), "-I", str(SHADERS),
                 "-E", "CSMain", "-T", "cs_6_3", "-spirv",
                 "-fspv-target-env=vulkan1.0", "-fvk-use-dx-layout",
                 str(hlsl), "/Fo", str(spv)],
                capture_output=True, text=True, env=environment,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            found = opcodes_of(spv.read_bytes())

        self.assertNotIn(
            OP_BITWISE_OR, found,
            "the compiled swap contains OpBitwiseOr, which the Adreno driver rejects",
        )
        self.assertIn(OP_BITWISE_XOR, found, "the swap no longer compiles to a bitwise XOR")


if __name__ == "__main__":
    unittest.main()
