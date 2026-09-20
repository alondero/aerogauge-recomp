"""Regression: the Adreno system driver rejects the OR spelling of the 16-bit byte swap.

The Adreno 750 system Vulkan driver returns VK_ERROR_UNKNOWN from vkCreateComputePipelines
for a compute shader containing ((i << 8) & 0xFF00) | ((i >> 8) & 0xFF). plume leaves the
pipeline handle null after that failure and the RT64 present thread then crashes the driver
inside vkCmdBindPipeline. Patch 0023 has to replace that expression with one the driver
accepts, so this test checks the shipped patch directly: the expression it removes must
still compile to OpBitwiseOr, the expression it adds must compile to OpBitwiseXor, and the
two must agree on every 16-bit input.

The device measurement and its reduction are recorded in docs/reference/renderer.md.

The patch and equivalence checks run anywhere. The expression checks need the dxc submodule.
The integration check needs patch 0023 applied to the lib/rt64 submodule, which the
supported build scripts do; a header that is present but still using the rejected spelling
is a failure, not a skip.
"""
import os
import platform
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

# The build compiles the framebuffer shaders both with and without optimizations, so a
# spelling that only survives under one of them would still be a regression.
OPTIMIZATION_LEVELS = (None, "-O0")

# The expression is the whole body, so the probe needs no includes and no descriptor sets.
# The operand is named `i` to match the parameter name in FbCommon.hlsli.
EXPRESSION_PROBE = """
RWBuffer<uint> gOutput : register(u1, space0);

[numthreads(8, 8, 1)]
void CSMain(uint2 coord : SV_DispatchThreadID) {
    uint i = coord.x * 3u + 1u;
    gOutput[coord.x] = %s;
}
"""

HEADER_PROBE = (
    '#include "FbCommon.hlsli"\n'
    "RWBuffer<uint> gOutput : register(u1, space0);\n"
    "[numthreads(8, 8, 1)]\n"
    "void CSMain(uint2 coord : SV_DispatchThreadID) {\n"
    "    gOutput[coord.x] = EndianSwapUINT16(coord.x * 3u + 1u);\n"
    "}\n"
)


def squash(text):
    """Collapse runs of whitespace so expressions compare structurally."""
    return " ".join(text.split())


def rejected_swap(i):
    """The spelling the driver rejects."""
    return ((i << 8) & 0xFF00) | ((i >> 8) & 0xFF)


def accepted_swap(i):
    """The spelling that ships."""
    return ((i << 8) & 0xFF00) ^ ((i >> 8) & 0xFF)


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


def patch_sides():
    """The lines patch 0023 removes and adds."""
    lines = PATCH.read_text(encoding="utf-8").splitlines()
    removed = [line[1:] for line in lines if line.startswith("-") and not line.startswith("---")]
    added = [line[1:] for line in lines if line.startswith("+") and not line.startswith("+++")]
    return removed, added


def returned_expression(lines):
    """The expression of the `return ...;` statement among patch lines, or None."""
    for line in lines:
        match = re.search(r"return\s+(.+?);", line)
        if match:
            return squash(match.group(1))
    return None


def patch_expressions():
    """The swap expressions patch 0023 removes and adds, in that order."""
    removed, added = patch_sides()
    return returned_expression(removed), returned_expression(added)


def helper_body():
    """The EndianSwapUINT16 body as checked out, or None without the submodule."""
    if not HEADER.is_file():
        return None
    match = re.search(
        r"uint EndianSwapUINT16\(uint i\)\s*\{(.*?)\}", HEADER.read_text(encoding="utf-8"), re.S
    )
    return match.group(1) if match else None


def dxc_command():
    """dxc plus the library directory it loads from, or None when unavailable.

    RT64 selects these with CMAKE_SYSTEM_PROCESSOR, and the submodule ships both
    x64 and arm64 builds, so an unconditional x64 path would run the wrong ELF
    class on an arm64 host.
    """
    machine = platform.machine().lower()
    if machine in ("x86_64", "amd64"):
        architecture = "x64"
    elif machine in ("aarch64", "arm64"):
        architecture = "arm64"
    else:
        return None

    if sys.platform == "darwin":
        binary = DXC_DIR / f"bin/{architecture}/dxc-macos"
    elif os.name == "nt":
        binary = DXC_DIR / f"bin/{architecture}/dxc.exe"
    else:
        binary = DXC_DIR / f"bin/{architecture}/dxc-linux"
    if not binary.is_file():
        return None
    return binary, DXC_DIR / f"lib/{architecture}"


def dxc_environment(library):
    """Windows resolves dxcompiler.dll from the executable directory, not from these."""
    environment = dict(os.environ)
    if os.name == "nt":
        return environment
    variable = "DYLD_LIBRARY_PATH" if sys.platform == "darwin" else "LD_LIBRARY_PATH"
    environment[variable] = f"{library}{os.pathsep}{environment.get(variable, '')}"
    return environment


def compile_shader(tool, source, extra_options=()):
    """Compile one shader source with the flags the Android build uses. Returns the SPIR-V."""
    binary, library = tool
    with tempfile.TemporaryDirectory(prefix="aero-endian-swap-") as temporary:
        hlsl = Path(temporary) / "Probe.hlsl"
        spv = Path(temporary) / "Probe.spv"
        hlsl.write_text(source, encoding="utf-8")
        arguments = [str(binary), "-I", str(RT64_SRC), "-I", str(SHADERS),
                     "-E", "CSMain", "-T", "cs_6_3", "-spirv",
                     "-fspv-target-env=vulkan1.0", "-fvk-use-dx-layout"]
        arguments.extend(extra_options)
        arguments.extend([str(hlsl), "/Fo", str(spv)])
        result = subprocess.run(arguments, capture_output=True, text=True,
                                env=dxc_environment(library))
        if result.returncode != 0:
            raise AssertionError(f"dxc failed: {result.stderr.strip()}")
        return spv.read_bytes()


class EquivalentSpellingTests(unittest.TestCase):
    def test_accepted_spelling_equals_the_rejected_one(self):
        # The two masked fields are disjoint, so XOR cannot differ from OR.
        for i in range(0x10000):
            self.assertEqual(accepted_swap(i), rejected_swap(i), f"mismatch for {i:#x}")
        for i in (0x00010000, 0x00FF0000, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF):
            self.assertEqual(accepted_swap(i), rejected_swap(i), f"mismatch for {i:#x}")


class PatchTests(unittest.TestCase):
    def test_patch_replaces_the_rejected_expression(self):
        removed, added = patch_sides()
        self.assertEqual(returned_expression(removed), REJECTED_SWAP,
                         "patch 0023 no longer removes the expression the driver rejects")
        self.assertEqual(returned_expression(added), ACCEPTED_SWAP,
                         "patch 0023 no longer adds the expected expression")


class CompiledSpellingTests(unittest.TestCase):
    """Compile the expressions patch 0023 removes and adds, and check their opcodes.

    This is the check with teeth: it fails if the spelling the patch actually ships still
    compiles to the operator the driver refuses, at either optimization level the build uses.
    """

    def setUp(self):
        tool = dxc_command()
        if tool is None:
            self.skipTest("the dxc submodule has no build for this host architecture")
        self.removed, self.added = patch_expressions()
        if self.removed is None or self.added is None:
            self.fail("patch 0023 no longer contains a removable and an added swap expression")
        self.tool = tool

    def test_rejected_expression_compiles_to_bitwise_or(self):
        for option in OPTIMIZATION_LEVELS:
            label = option or "-O3 (default)"
            found = opcodes_of(compile_shader(
                self.tool, EXPRESSION_PROBE % self.removed, (option,) if option else ()))
            self.assertIn(OP_BITWISE_OR, found,
                          f"the expression patch 0023 removes did not emit OpBitwiseOr at {label}")
            self.assertNotIn(OP_BITWISE_XOR, found,
                             f"the expression patch 0023 removes emitted OpBitwiseXor at {label}")

    def test_accepted_expression_compiles_to_bitwise_xor(self):
        for option in OPTIMIZATION_LEVELS:
            label = option or "-O3 (default)"
            found = opcodes_of(compile_shader(
                self.tool, EXPRESSION_PROBE % self.added, (option,) if option else ()))
            self.assertIn(OP_BITWISE_XOR, found,
                          f"the expression patch 0023 adds did not emit OpBitwiseXor at {label}")
            self.assertNotIn(OP_BITWISE_OR, found,
                             f"the expression patch 0023 adds still emits OpBitwiseOr at "
                             f"{label}, which the Adreno driver rejects")


class SubmoduleIntegrationTests(unittest.TestCase):
    """The submodule has to be patched, because that is what the build compiles."""

    def setUp(self):
        body = helper_body()
        if body is None:
            self.skipTest("the lib/rt64 submodule is not initialized")
        if REJECTED_SWAP in squash(body):
            self.fail("lib/rt64/src/shaders/FbCommon.hlsli still uses the expression the "
                      "Adreno driver rejects; apply patches/0023-rt64-adreno-endian-swap.patch "
                      "(the supported build scripts do this) before running this test")
        tool = dxc_command()
        if tool is None:
            self.skipTest("the dxc submodule has no build for this host architecture")
        self.tool = tool

    def test_patched_helper_avoids_the_rejected_operator(self):
        found = opcodes_of(compile_shader(self.tool, HEADER_PROBE))
        self.assertNotIn(OP_BITWISE_OR, found,
                         "the compiled helper contains OpBitwiseOr, which the Adreno driver "
                         "rejects")
        self.assertIn(OP_BITWISE_XOR, found, "the helper no longer compiles to a bitwise XOR")


if __name__ == "__main__":
    unittest.main()
