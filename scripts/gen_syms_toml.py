#!/usr/bin/env python3
"""Generate the runtime-addressed whole-ROM syms.toml + us.toml for AeroGauge (USA).

Model: same ROM+syms N64Recomp mode as the Automobili Lamborghini port this stack was
cloned from (the drmario64 template model) — ONE contiguous .text section at
rom=0x1000 / vram=0x80000400 (the ROM-header entrypoint), functions listed with
name/vram/size, absolute addressing, no relocs.

Unlike the Lamborghini port there is NO pre-existing splat disassembly to source
function boundaries from, so this script derives them from the ROM itself:

  * Measured code extent (2026-07-10 density scan, scripts history): CPU text is
    contiguous at ROM 0x1000..~0x7F4C0. Every jal located inside that window targets
    inside it (zero out-of-window targets), after which instruction decoding turns to
    data/RSP-ucode noise. The tail (last ~0x100) is the CP0 exception handler.
  * Function STARTS = the real entrypoint trampoline (0x80000400), its static jr
    targets (see BOOT_EXTRA below), every in-window jal target, plus every
    `addiu $sp,$sp,-N` prologue that immediately follows a function terminator
    (jr + delay slot, or nop padding) — the standard IDO function-boundary signal.
    Sizes span to the next start (oversize is harmless; branches stay internal).
  * PRE-STUBS: functions containing CP0/cache instructions (the libultra kernel layer
    ultramodern replaces wholesale) and functions whose branches escape their derived
    range (mis-split shared-tail code — the Lamborghini SPLIT_MERGES class, to be
    grown case-by-case as the port needs them) are emitted as `stubs` so the whole-ROM
    recompile succeeds. force_stub.txt adds hand-curated entries on top (one name per
    line, '#' comments) — the iteration loop for recompiler errors.

Usage:  python scripts/gen_syms_toml.py    (from the repo root; reads the ROM +
        force_stub.txt, writes aerogauge.syms.toml + aerogauge.us.toml)
"""
import struct
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
ROM_FILE = REPO / "AeroGauge (USA).z64"
OUT_SYMS = REPO / "aerogauge.syms.toml"
OUT_CFG = REPO / "aerogauge.us.toml"
FORCE_STUB = REPO / "force_stub.txt"

ENTRY = 0x80000400           # ROM header bytes 0x08-0x0B
SECTION_ROM = 0x1000
CODE_ROM_END = 0x7F4C0       # end of contiguous CPU text (density scan; see docstring)


def rom_to_vram(off):
    return ENTRY + (off - SECTION_ROM)


def vram_to_rom(v):
    return SECTION_ROM + (v - ENTRY)


# Canonical libultra names, keyed by vram (the Lamborghini port's LIBULTRA_NAMES mechanism).
# Naming a function canonically makes N64Recomp route it away from recompilation
# (lib/N64ModernRuntime/N64Recomp/src/symbol_lists.cpp):
#   * reimplemented_funcs -> call sites renamed `<name>_recomp`, librecomp provides the native.
#   * ignored_funcs       -> body skipped, call sites renamed `<name>_recomp`, WE must provide
#                            the symbol in src/libultra_stubs.c.
# Every entry must be verified from the ROM bytes before it lands here (CLAUDE.md: source is
# ground truth) — record the evidence in the comment.
LIBULTRA_NAMES = {
    # __osInitialize_common (size 0x290, byte-verified 2026-07-11): stores __osFinalrom=1 to
    # 0x801AC110, then jal 0x80078840 (__osGetSR) -> __osSetSR(sr|CU1 0x20000000) ->
    # __osSetFpcCsr(0x01000800) -> busy-probes PIF RAM 0x1FC007FC via __osSiRawReadIo /
    # __osSiDeviceBusy (raw SI_STATUS 0xA4800018 — the first boot-smoke MMIO fault) and writes
    # the PIF terminate-boot byte (|8) via __osSiRawWriteIo. Identical shape and size to the
    # Lamborghini port's func_80073B40. reimplemented -> librecomp __osInitialize_common_recomp
    # (= ultramodern osInitialize()), collapsing the whole CP0+SI/PIF init subtree.
    0x80070290: "__osInitialize_common",
    # Thread + message-queue kernel (all byte-verified 2026-07-11; all reimplemented -> librecomp
    # natives = ultramodern's native scheduler, replacing the ROM's cooperative run-queue whose
    # __osDispatchThread/eret CP0 tail can never run recompiled — the threads=0 boot stall).
    # Shared plumbing observed: __osDisableInt=0x80070860, __osRestoreInt=0x80070880,
    # __osEnqueueThread=0x80070FCC, __osEnqueueAndYield=0x80070ECC, __osPopThread=0x80071014,
    # __osRunQueue=0x80094878, __osRunningThread=0x80094880, __osThreadTail=0x80094870,
    # __osActiveQueue=0x8009487C, __osCleanupThread=0x800711A0.
    #
    # osCreateThread (0x150): pri->t+4, state=1(STOPPED)->t+0x10(h), entry pc->t+0x11C,
    # $sp pair->t+0xF0 (stack-0x10), $ra=__osCleanupThread->t+0x100, FPCSR 0x01000800->t+0x12C,
    # DisableInt/RestoreInt bracket linking t into __osActiveQueue. Boot body 0x800653F0 uses it
    # to create the pri-10 boot thread (entry 0x80065454).
    0x80067BB0: "osCreateThread",
    # osStartThread (0x150): DisableInt; state@+0x10 dispatch (1=STOPPED/8=WAITING) -> state=2
    # (RUNNABLE), __osEnqueueThread(&__osRunQueue); priority compare vs __osRunningThread ->
    # running->state=2 + __osEnqueueAndYield(&__osRunQueue); RestoreInt.
    0x80067D00: "osStartThread",
    # osSetThreadPri (0xE0): DisableInt; if(t==0) t=__osRunningThread; t->pri@+4=pri; requeue via
    # 0x800707F0+__osEnqueueThread; yield if run-queue head outranks running. Boot thread calls it
    # (0x80070520 from 0x80065454) to demote itself to the idle thread after starting main (id 6).
    0x80070520: "osSetThreadPri",
    # osCreateMesgQueue (0x30, leaf): mq->mtqueue=mq->fullqueue=&__osThreadTail(0x80094870),
    # validCount=0@+8, first=0@+0xC, msgCount=a2@+0x10, msg=a1@+0x14. Byte-for-byte canonical.
    0x80065E30: "osCreateMesgQueue",
    # osSendMesg (0x150): DisableInt; while validCount@+8 >= msgCount@+0x10: NOBLOCK->-1 or
    # state=8 + __osEnqueueAndYield(&mq->fullqueue @mq+4); msg[(first+validCount)%msgCount]=a1
    # (div-by-zero break guard 0x0007000D); validCount++; __osPopThread(&mq->mtqueue)->osStartThread.
    0x800686D0: "osSendMesg",
    # osRecvMesg (0x280 derived span; body ~0x140, tail is two unrelated struct-copy loops at
    # 0x800661D0 absorbed by the boundary scan — they are unreferenced by jal and harmlessly not
    # emitted): DisableInt; while validCount==0: NOBLOCK->-1 or state=8 + __osEnqueueAndYield(mq);
    # *a1=msg[first]; first=(first+1)%msgCount; validCount--; __osPopThread(&mq->fullqueue)
    # -> osStartThread.
    0x80066090: "osRecvMesg",
    # osJamMesg (0x150): same head as osSendMesg but first=(first-1+msgCount)%msgCount stored back
    # @+0xC, msg[first]=a1 (front-insert).
    0x8006FD30: "osJamMesg",
    # osSetEventMesg (0x70): DisableInt; __osEventStateTab=0x801AAB10; tab[event*8].mq=a1,
    # .msg=a2; RestoreInt. (This libultra vintage has no PRENMI special case.)
    0x8006AE00: "osSetEventMesg",
    # VI API cluster, all operating on __osViNext=*(0x80094C54) / __osViCurr=*(0x80094C50),
    # canonical OSViContext layout (state@0, retraceCount@2, framep@4, modep@8, control@0xC,
    # msgq@0x10, msg@0x14):
    # osViSetEvent (0x70): next->msgq=a0, next->msg=a1, next->retraceCount=a2.
    0x8006FBC0: "osViSetEvent",
    # osViSetMode (0x70): next->modep=a0, next->state=1(MODE_SET), next->control=modep->ctrl@+4.
    0x8006FB50: "osViSetMode",
    # osViSwapBuffer (0x50): next->framep=a0, next->state|=0x10(FRAMEBUFFER_SET).
    0x8006FE80: "osViSwapBuffer",
    # osViGetCurrentFramebuffer (0x40): return __osViCurr->framep@+4.
    0x8006FCB0: "osViGetCurrentFramebuffer",
    # osViGetNextFramebuffer (0x40): return __osViNext->framep@+4.
    0x8006FCF0: "osViGetNextFramebuffer",
    # osViBlack (0x70): next->state |= 0x20 (VI_STATE_BLACK) if a0 else &= ~0x20.
    0x8006CC60: "osViBlack",
    # --- second batch (byte-verified 2026-07-11, first-fault: osCreatePiManager ->
    #     osGetThreadPri dereferencing __osRunningThread=0 -> SIGSEGV) ---
    # osGetThreadPri (0x10-ish leaf at the head of the derived span): if(a0==0)
    # a0=__osRunningThread(0x80094880); return a0->pri@+4. Native scheduler doesn't mirror the
    # running thread in RDRAM -> recompiled body faults (same as Lambo's func_8007E550).
    0x80077FA0: "osGetThreadPri",
    # osCreatePiManager (0x180): guard flag __osPiDevMgr@0x80094840; two osCreateMesgQueue
    # (0x801BD2D0/D2E8); __osPiCreateAccessQueue(0x80078C40); osSetEventMesg; osGetThreadPri(0);
    # osSetThreadPri bracket; osCreateThread+osStartThread of __osDevMgrMain.
    0x80070600: "osCreatePiManager",
    # osPiStartDma (0xE0): bails -1 unless __osPiDevMgr@0x80094840; OSIoMesg hdr.type=11/12
    # (OS_MESG_TYPE_DMAREAD/WRITE by direction), pri@+2, retQueue@+4, dram@+8, dev@+0xC,
    # size@+0x10; jal 0x80070830 (osPiGetCmdQueue) then send/jam. Game wraps it in the
    # synchronous loader 0x80065690 (osInvalDCache -> osPiStartDma -> osRecvMesg 0x801A4580).
    0x80065F10: "osPiStartDma",
    # osVirtualToPhysical (0x80): KSEG0 (0x80000000<=a<0xA0000000) and KSEG1 (<0xC0000000)
    # range checks -> & 0x1FFFFFFF, else __osProbeTLB. jal'd by __osSiRawStartDma,
    # osAiSetNextBuffer, __osViSwapContext.
    0x80067EB0: "osVirtualToPhysical",
    # osInvalDCache (0xB0) / osWritebackDCache (0x80): CACHE-op loops (currently auto-stubbed);
    # canonical call sites in __osSiRawStartDma (writeback before WR64B, inval after RD64B) and
    # the game loader 0x80065690. Native no-ops (unified host memory).
    0x80065E60: "osInvalDCache",
    0x80078670: "osWritebackDCache",
    # osGetCount (0x10 leaf): mfc0 $v0,Count; jr $ra. Was auto-stubbed (cop0) = garbage return;
    # the native derives it from the host clock.
    0x80076D10: "osGetCount",
    # osSetTime (0x74): DisableInt; __osBaseCounter@0x801BD340 = osGetCount(); 64-bit time-base
    # store via the 0x8007B830 leaf; RestoreInt.
    0x80076F24: "osSetTime",
    # AI (audio interface) trio:
    # osAiSetFrequency (0x160): dacRate float math from osClockRate@0x80094828; writes
    # AI_DACRATE(0xA4500010), AI_BITRATE(0xA4500014), AI_CONTROL(+0x08)=1; returns clock/dac.
    0x80067990: "osAiSetFrequency",
    # osAiSetNextBuffer (0xB0): 0x2000-boundary quirk flag @0x80092E50; __osAiDeviceBusy
    # (0x800713B0); osVirtualToPhysical -> AI_DRAM_ADDR(0xA4500000) + AI_LEN(+4).
    0x80067F30: "osAiSetNextBuffer",
    # osAiGetLength (leaf): return AI_LEN @0xA4500004.
    0x80067FE0: "osAiGetLength",
    # osCreateViManager (byte-verified 2026-07-11; the third boot first-fault): guard flag
    # @0x80093680; __osTimerServicesInit(0x80076D20); osCreateMesgQueue(0x801BC060, 5 deep);
    # osSetEventMesg registrations; jal __osViInit(0x80077120) — the ROM's VI-context init
    # whose MMIO tail (poll VI_CURRENT 0xA4400010, zero VI_CONTROL, __osViSwapContext
    # 0x80077FD0) faults recompiled. __osViInit's ONLY caller is this function (verified in
    # RecompiledFuncs), so routing the manager natively collapses it — no Lambo-style
    # hand-translated __osViInit_recomp needed. VI globals for reference: __osViCurr=0x80094C50,
    # __osViNext=0x80094C54, contexts @0x80094BF0, modes PAL/MPAL/NTSC @0x80094CA0/4CF0/4D40.
    0x8006F7F0: "osCreateViManager",
    # osSetTimer (byte-verified 2026-07-11; fourth boot first-fault): OSTimer fill — next/prev=0,
    # interval@+8/+0xC (stack args 0x30/0x34), value@+0x10/+0x14 = countdown(a2:a3) or interval
    # if zero — then __osInsertTimer(0x80076F98), which walks __osTimerList@0x80094BE0. The list
    # head is 0 (its init lived in __osTimerServicesInit, collapsed by the native
    # osCreateViManager) -> recompiled __osInsertTimer faults. reimplemented -> librecomp native.
    0x80074160: "osSetTimer",
    # RSP task interface (byte-verified 2026-07-11; fifth boot first-fault was
    # osSpTaskLoad -> __osSpSetStatus writing SP_STATUS 0xA4040010 raw). SP plumbing seen:
    # __osSpSetStatus=0x800786F0, __osSpGetStatus=0x80078350, __osSpSetPc=0x80078700 (SP_PC
    # 0xA4080000), __osSpRawStartDma=0x80078740 (SP_MEM/DRAM_ADDR + RD/WR_LEN),
    # __osSpDeviceBusy=0x800787D0 (status & 0x1C) — all collapse once the four osSpTask* and
    # osDpSetNextBuffer are routed (their only callers).
    # osSpTaskLoad (0x190): _VirtualToPhysicalTask(0x8006FED0); clears OS_TASK_YIELDED; restores
    # yield-data ptr; osWritebackDCache(task,0x40); __osSpSetStatus(0x2B00); __osSpSetPc(0x04001000)
    # retry loop; __osSpRawStartDma(WRITE, DMEM 0x04000FC0, task, 0x40) retry loop; DMAs boot ucode.
    0x8006FFEC: "osSpTaskLoad",
    # osSpTaskStartGo (0x44): wait !__osSpDeviceBusy; __osSpSetStatus(0x125 =
    # CLR_HALT|CLR_BROKE|SET_INTR_BREAK).
    0x8007017C: "osSpTaskStartGo",
    # osSpTaskYield (0x20): __osSpSetStatus(0x400 = SET_SIG0, the yield request).
    0x80070270: "osSpTaskYield",
    # osSpTaskYielded (0x80): __osSpGetStatus & 0x100 (SIG0) -> yielded; manages the
    # OS_TASK_YIELDED bit in task->flags.
    0x8006FC30: "osSpTaskYielded",
    # osDpSetNextBuffer (0xB0): __osDpDeviceBusy(0x80078800, DPC_STATUS 0xA410000C); xbus set +
    # poll; osVirtualToPhysical -> DPC_START(0xA4100000)/DPC_END(+4).
    0x800701C0: "osDpSetNextBuffer",
    # osContInit (byte-verified 2026-07-11; sixth boot first-fault, via its __osSiRawStartDma
    # of the PIF status frame @0x801BAB90): init guard @0x80092E60; canonical 500ms PIF
    # power-up wait (osGetTime vs 0x0165A0BC = 23,437,500 ticks, osSetTimer + osRecvMesg for
    # the remainder); __osMaxControllers=4 @0x801BABD1; status-frame stage (0x8006B120) ->
    # SI write+read DMA with osRecvMesg waits -> __osContGetInitData parse (0x8006B050);
    # __osContLastCmd=0 @0x801BABD0; __osSiCreateAccessQueue (0x800740A0). reimplemented ->
    # ultramodern native (input callbacks), collapsing the whole PIF/joybus subtree.
    0x8006AEE0: "osContInit",
    # osGetTime (byte-verified 2026-07-11): DisableInt; osGetCount(); 64-bit
    # __osCurrentTime(0x801BD330/34) + (count - __osBaseCounter(0x801BD338)); RestoreInt.
    # NOTE: this retires the force_stub.txt entry for func_8006C800 — the recompiler's
    # `trunc.l.d` error came from a prologue-less float-conversion leaf at 0x8006C890 that the
    # boundary scan absorbs into this span; routing the name skips emission of the whole span.
    0x8006C800: "osGetTime",
    # osPfsInitPak (byte-verified 2026-07-11; seventh boot first-fault via its
    # __osPfsGetStatus(0x800742F0) -> __osSiRawStartDma of the per-channel status frame
    # @0x801BD350): __osSiGetAccess(0x800740F0); __osPfsGetStatus; pfs->queue@+4, channel@+8,
    # status=0@+0, +0x65=0; __osPfsSelectBank(0x8007521C); __osContRamRead(0x80075290) of the
    # ID area. The game's pak scan (func_80026384) probes all channels at boot. ignored ->
    # librecomp pak.cpp osPfsInitPak_recomp returns PFS_ERR_NOPACK ("no pak"), which cleanly
    # gates the whole recompiled PFS suite (its __osContRamRead/Write users are only reachable
    # after a successful InitPak). TODO(aerogauge): real Controller-Pak persistence — port the
    # Lambo .mpk image + joybus answer machinery if save support needs it.
    0x8006B440: "osPfsInitPak",
    # osContStartReadData (byte-verified 2026-07-11; eighth boot first-fault — the main game
    # loop 0x800658FC polls pads each frame): __osSiGetAccess(0x800740F0); if
    # __osContLastCmd(0x801BABD0)!=1 stage read frames via __osPackReadData(0x8006B354) + SI
    # write DMA @0x801BAB90 + osRecvMesg; SI read DMA; __osContLastCmd=1;
    # __osSiRelAccess(0x80074134). reimplemented -> ultramodern poll_input + send_si.
    0x8006B220: "osContStartReadData",
    # osContGetReadData (0xA8, directly after): walks the PIF read frames @0x801BAB90 for
    # __osMaxControllers(0x801BABD1) channels, error bits from rx byte -> errno, fills the
    # 6-byte-stride OSContPad array (button u16, stick s8 x2, errno).
    0x8006B2AC: "osContGetReadData",
    # --- EEPROM family (byte-verified 2026-07-11; ninth first-fault, the first RT64 windowed
    #     run: game save-load 0x80061D00 -> osEepromProbe -> __osEepStatus(0x800778B8) ->
    #     __osSiRawStartDma(0x80074240) -> __osSiDeviceBusy(0x8007AAA0) raw SI_STATUS read).
    #     All reimplemented -> librecomp eep.cpp natives backed by the real save file
    #     (requires game.save_type = Eep4k in main.cpp). Shared SI plumbing identified:
    #     __osSiGetAccess=0x800740F0, __osSiRelAccess=0x80074134, __osSiRawStartDma=0x80074240
    #     (busy check; dcache writeback/inval of the 64-byte PIF frame; osVirtualToPhysical ->
    #     SI_DRAM_ADDR 0xA4800000, PIF_RAM 0x1FC007C0 -> RD64B/WR64B), __osEepStatus=0x800778B8
    #     (zeroes 4 channel slots to reach channel 4, cmd frame FF 01 03 00 = tx1/rx3/
    #     REQUEST_STATUS, 0xFE terminator, write+read DMA pair @0x801BAC30).
    # osEepromProbe (0xA0): __osSiGetAccess; __osEepStatus; type bits (status & 0xC000):
    # 0x8000 -> 1 (EEPROM_TYPE_4K), 0xC000 -> 2 (16K), else 0; __osSiRelAccess. Game caller:
    # the save loader 0x80061D00 (accepts any nonzero type).
    0x8006DE40: "osEepromProbe",
    # osEepromRead (0x21C): GetAccess; __osEepStatus; 4K path builds cmd 04 (read block) via
    # the channel-skip frame, write+read DMA pair, 16K half handled by the helper
    # 0x8006E0FC (only caller is this body -> collapses). Game callers 0x80061D54 + inside
    # osEepromLongRead.
    0x8006DEE0: "osEepromRead",
    # osEepromWrite (0x1EC): GetAccess; __osEepStatus TWICE (second is the write-in-progress
    # poll); type check -> cmd 05 (write block) frame @0x801BAC30, write+read DMA pair.
    # Only direct caller is osEepromLongWrite's loop @0x8006E22C.
    0x800775C0: "osEepromWrite",
    # osEepromLongRead (0x90): loops osEepromRead(0x8006E2F0's jal @0x8006E31C) one 8-byte
    # block at a time, buffer+8/address+1 per pass. Game callers 0x80062038/0x8006205C.
    0x8006E2F0: "osEepromLongRead",
    # osEepromLongWrite (0xF0): loops osEepromWrite(@0x8006E22C) per 8-byte block, then
    # osSetTimer(0x80074160) with 0x89544 counts (~12 ms EEPROM write latency) + osRecvMesg
    # between blocks. Game callers 0x80061EAC..0x800624E0 (9 sites, the save writer).
    0x8006E200: "osEepromLongWrite",
    # --- Rumble-motor family (byte-verified 2026-07-11, same raw-SI crash class; all
    #     reimplemented -> ultramodern input.cpp natives, which self-guard on
    #     pfs->status & PFS_MOTOR_INITIALIZED and answer via the set_rumble callback) ---
    # osMotorInit (0x2E4, no jal callers -- reached indirectly): pfs->queue=a0@+4,
    # channel=a2@+8, status=0@+0, +0x65=0x80; fills a 32-byte 0xFE probe block and
    # __osContRamWrite(0x80077260)s it to pak address 0x400, re-probes with 0x80 -- the
    # canonical rumble-pak detect. Writes the per-channel init table @0x80093670 that
    # osMotorStart/Stop check.
    0x8006E83C: "osMotorInit",
    # osMotorStop (0x1A0): bails 5 (PFS_ERR_INVALID) unless table[pfs->channel]@0x80093670;
    # __osSiGetAccess; __osContLastCmd=3 @0x801BABD0; __osSiRawStartDma WRITE of the prebuilt
    # per-channel 0x40 motor frame @0x801BAC70+chan*0x40, then READ @0x801BD350. Game caller
    # 0x80063970 (rumble dispatcher 0x80063930, flag bit 0x400).
    0x8006E380: "osMotorStop",
    # osMotorStart (0x1A0): byte-identical shape to osMotorStop but writes the start frame
    # table @0x801BAD70+chan*0x40. Game caller 0x80063954 (same dispatcher, flag bit 0x800).
    0x8006E520: "osMotorStart",
}

# Game/libultra functions we replace with hand-written natives in src/ for ENHANCEMENTS
# (not OS routing). These are emitted into the us.toml `[patches] ignored` array: N64Recomp
# skips the body but does NOT rename call sites, so callers reference the bare name and the
# linker resolves it to our native (src/aero_draw_distance.cpp etc.). Unlike LIBULTRA_NAMES,
# these names are NOT in N64Recomp's built-in reimplemented/ignored/renamed sets
# (symbol_lists.cpp), so the toml array is the only routing mechanism.
NATIVE_NAMES = {
    # guPerspectiveF (ROM 0x8006BA60, byte-verified 2026-07-11): jal 0x8006C330
    # (guMtxIdentF), fovy cvt.d.s * double @0x80098D20 (== 3.1415926/180.0,
    # ROM-byte-exact), /2.0f, jal 0x8006AC80 (cosf) / 0x80066D50 (sinf) -> cot,
    # (n+f)/(n-f) & 2nf/(n-f) matrix terms, perspNorm `c.le.d 2.0` + `sh` store at
    # 0x8006BBB4..0x8006BC88. ALL 11 call sites pass far=500.0 (0x43FA immediates
    # at 9 scene setups; camera structs +0x10 at 0x8001F59C/0x80020748) -- the
    # game's entire draw-distance limit. Replaced by src/aero_draw_distance.cpp
    # to scale the far plane (issue: pop-in).
    0x8006BA60: "guPerspectiveF",
}

# Hand-authored [[patches.hook]] / [[patches.instruction]] blocks appended verbatim to the
# generated aerogauge.us.toml. The generator must carry them (Lambo lesson: blocks edited
# straight into the toml were silently dropped by the next regen).
PATCH_BLOCKS = """
# Developer warp menu (issue #3, src/aero_warp.c). Hooked at the entry of the top-level
# per-frame scene driver func_80015C8C (called every frame from the main game thread's
# loop func_800658FC): it copies the scene request 0x8013FF84 into the current scene
# 0x8013FF80 and jump-tables (0x800969C0, 10 scenes) to the active scene's runner. The
# entry hook lets a pending warp perform the menu's own race-launch stores (race-param
# block 0x8013FF90 + RACE confirm side effects) before this frame's request is consumed.
# (NOT func_80015FD0 — that is merely the RACE scene's runner, case 5 of the table, so a
# hook there goes silent in every other scene.)
[[patches.hook]]
func = "func_80015C8C"
before_vram = 0x80015C8C
text = "extern void aero_warp_tick(uint8_t*, recomp_context*); aero_warp_tick(rdram, ctx);"

# Widescreen 1P-HUD speedometer pinning (issue #1, src/aero_hud_widescreen.c). Live GDB
# attribution (scratchpad/ATTRIBUTION.md) proved the bottom-right speedometer is drawn
# EXCLUSIVELY by func_80018CF0, a small per-object handler reached from the 2D dispatcher
# func_80022408. It reads the DL write cursor from the builder held in a0 (== ctx->r4 ==
# 0x8016C508), draws, then writes the advanced cursor back at 0x80018D58. A matched pair of
# before_vram hooks brackets it: the entry emits the RT64 RIGHT rect-align + wide scissor
# (saving the holder from ctx->r4); the reset -- placed AFTER the cursor writeback at
# 0x80018D58, before the epilogue -- pops the scissor and clears the alignment through the
# saved holder. func_8003A190 (centred DAMAGE) and the other HUD groups are off this path,
# so they are not bracketed. At 4:3 / non-Expand RT64 leaves the tagged rects put, so the
# bracket is a no-op with no config gate.
[[patches.hook]]
func = "func_80018CF0"
before_vram = 0x80018CF0
text = "extern void aero_ws_speedo_pin(uint8_t*, recomp_context*); aero_ws_speedo_pin(rdram, ctx);"

[[patches.hook]]
func = "func_80018CF0"
before_vram = 0x80018D5C
text = "extern void aero_ws_speedo_reset(uint8_t*, recomp_context*); aero_ws_speedo_reset(rdram, ctx);"
"""

# Boot-chain starts that are NOT jal targets (verified from the entry disassembly):
#   0x80000400  entry trampoline: clears the DMA table then `jr $t2` -> 0x800653f0
#   0x80000450  own `addiu $sp,-0x38` prologue right after the trampoline pad
#               (also caught by the prologue scan; listed for explicitness)
#   0x800653f0  the static jr target -- N64Recomp emits the trampoline's `jr $t2` as a
#               runtime get_function() lookup, so this MUST be a registered entry.
BOOT_EXTRA = [0x80000400, 0x80000450, 0x800653F0]

# Function starts reached ONLY through function pointers (jr $reg): invisible to both the
# jal-target scan and the prologue scan when the target is a prologue-less leaf. The runtime
# error loop for these is `Failed to find function at 0x...` from librecomp's get_function()
# — verify the address begins right after a function terminator in the ROM bytes, then add it.
INDIRECT_STARTS = [
    # 2026-07-11 boot smoke: indirect call after audio init; bytes at 0x80002028 are a
    # prologue-less leaf starting right after the previous function's `jr $ra` + delay slot.
    0x80002028,
    # 2026-07-11 windowed run: `Failed to find function at 0x8000DFD0` ~14 s in (game
    # advancing past the boot states). Verified head right after `jr $ra` + nop padding at
    # 0x8000DFBC; missed by the prologue scan because the `addiu $sp` sits at +8 (two
    # scheduling-hoisted loads precede it), and no jal targets it.
    0x8000DFD0,
    # 2026-07-11 batch (scratchpad indirect_scan.py, after the next fault moved to
    # 0x80009D70): scanned the whole ROM DATA region for aligned words pointing into
    # .text, kept those that (a) are not already derived starts, (b) sit right after a
    # function terminator (jr + delay slot, or jr + nop padding), and (c) are NOT
    # branch-reachable from earlier inside their containing derived span -- filter (c)
    # rejects switch-dispatcher case blocks whose addresses live in .data jump tables
    # (e.g. 0x80026598, the shared `jr $ra` default case of the 0x80026578 dispatcher,
    # 28 table slots) and which N64Recomp already handles as jump tables. Survivors are
    # game object/callback pointers, all in the same low-.text module:
    0x80009D70,  # 288 refs -- the per-object callback in the entity spawn records
    0x8000E090,  # same hoisted-load head shape as 0x8000DFD0
    0x8000F920,
    0x8000FAFC,  # prologue-less arg-spill leaf
    0x8000FCD0,
    # 2026-07-11 second batch (scratchpad la_scan.py, after the next fault moved to
    # 0x80007BB0): same head + branch-reachability filters, but the pointer source is
    # lui/addiu (`la`) pairs in .text -- callbacks materialized in registers and stored
    # into runtime structs, invisible to both the jal scan and the data-word scan.
    # Deliberately EXCLUDED from the same scan's output: 0x800708A0/0x800708B0 ($k0-using
    # exception-handler entries, referenced only by the vector installer) and 0x8007BED0
    # (the exception-vector code blob, referenced as a memcpy source) -- kernel artifacts
    # that must stay un-emitted, same class as the CP0 auto-stubs.
    0x800013A0,
    0x80007BB0,  # the fault site; registered by the 0x80018438..0x80018A9C callback module
    0x80007D08,
    0x80007FA0,
    0x8000D148,
    0x80018E90,
    0x8005C95C,
    0x8007A4E8,  # _Printf-style proc pointer, stored at struct+0x28 beside func_8007A75C
]


def main():
    if not ROM_FILE.exists():
        sys.exit(f"missing ROM: {ROM_FILE}")
    rom = ROM_FILE.read_bytes()

    def word(off):
        return struct.unpack(">I", rom[off:off + 4])[0]

    lo, hi = SECTION_ROM, CODE_ROM_END
    vlo, vhi = rom_to_vram(lo), rom_to_vram(hi)

    # --- pass 1: function starts ------------------------------------------------
    starts = set(BOOT_EXTRA) | set(INDIRECT_STARTS)
    for off in range(lo, hi, 4):
        w = word(off)
        if (w >> 26) == 3:  # jal
            t = 0x80000000 | ((w & 0x03FFFFFF) << 2)
            if vlo <= t < vhi:
                starts.add(t)
    # prologue after a terminator: addiu $sp,$sp,-N preceded by (jr xx + delay) or nop pad
    def is_jr(w):
        return (w >> 26) == 0 and (w & 0x3F) == 8
    for off in range(lo + 8, hi, 4):
        w = word(off)
        if (w >> 16) == 0x27BD and (w & 0x8000):  # addiu $sp,$sp,-N
            if word(off - 4) == 0 or is_jr(word(off - 8)):
                starts.add(rom_to_vram(off))

    starts = sorted(starts)

    # --- pass 2: sizes + pre-stub analysis ---------------------------------------
    funcs = []          # (name, vram, size)
    auto_stubs = set()
    for i, v in enumerate(starts):
        end_v = starts[i + 1] if i + 1 < len(starts) else vhi
        size = end_v - v
        name = LIBULTRA_NAMES.get(v) or NATIVE_NAMES.get(v) or "func_%08X" % v
        cop0 = False
        branch_out = False
        for off in range(vram_to_rom(v), vram_to_rom(end_v), 4):
            w = word(off)
            op = w >> 26
            if op == 0x10 or op == 0x2F:      # COP0 (mfc0/mtc0/eret/tlb*) or CACHE
                cop0 = True
            # relative branches: beq/bne/blez/bgtz + likely forms + REGIMM bltz/bgez(al)(l)
            is_branch = op in (4, 5, 6, 7, 0x14, 0x15, 0x16, 0x17)
            if op == 1 and ((w >> 16) & 0x1F) in (0, 1, 2, 3, 0x10, 0x11, 0x12, 0x13):
                is_branch = True
            if is_branch:
                imm = struct.unpack(">h", struct.pack(">H", w & 0xFFFF))[0]
                tgt = rom_to_vram(off) + 4 + imm * 4
                if not (v <= tgt < end_v):
                    branch_out = True
        if cop0 or branch_out:
            auto_stubs.add(name)
        funcs.append((name, v, size))

    # entrypoint gets renamed by N64Recomp itself (vram==ENTRY && rom==SECTION_ROM)

    # --- force_stub.txt (hand-curated error-loop additions) ----------------------
    force = set()
    if FORCE_STUB.exists():
        for line in FORCE_STUB.read_text().splitlines():
            line = line.split("#", 1)[0].strip()
            if line:
                force.add(line)
    known = {n for n, _, _ in funcs}
    unknown_force = force - known
    if unknown_force:
        print(f"WARNING: force_stub.txt names not in the function map: {sorted(unknown_force)}")
    # Canonically-named functions are routed (reimplemented/ignored) by N64Recomp itself and
    # are never emitted -- listing one as a stub too would make the recompiler hard-error.
    stubs = sorted(((auto_stubs | force) & known)
                   - set(LIBULTRA_NAMES.values()) - set(NATIVE_NAMES.values()))

    # --- emit syms.toml -----------------------------------------------------------
    # encoding pinned: Windows text-mode default is cp1252, which mangles em-dashes in
    # comments into bytes N64Recomp's toml parser rejects (Lambo PR#48 lesson).
    with OUT_SYMS.open("w", newline="\n", encoding="utf-8") as f:
        f.write("# Autogenerated by scripts/gen_syms_toml.py -- DO NOT EDIT BY HAND.\n")
        f.write("# Runtime-addressed whole-ROM symbols for ultramodern/librecomp.\n")
        f.write("# Source: jal-target + prologue scan of the ROM (no splat project exists).\n")
        f.write("[[section]]\n")
        f.write('name = ".text"\n')
        f.write(f"rom = 0x{SECTION_ROM:X}\n")
        f.write(f"vram = 0x{ENTRY:08X}\n")
        f.write(f"size = 0x{hi - lo:X}\n\n")
        f.write("functions = [\n")
        for name, v, size in funcs:
            f.write(f'    {{ name = "{name}", vram = 0x{v:08x}, size = 0x{size:x} }},\n')
        f.write("]\n")

    # --- emit us.toml ---------------------------------------------------------------
    with OUT_CFG.open("w", newline="\n", encoding="utf-8") as f:
        f.write("# AUTOGENERATED by scripts/gen_syms_toml.py -- DO NOT EDIT BY HAND.\n")
        f.write("# Whole-ROM recompile config (ROM+syms mode; see the script docstring).\n")
        f.write("# Run (see BUILDING.md step 3):\n")
        f.write("#   cmake --build build --target N64RecompCLI\n")
        f.write("#   ./build/lib/N64ModernRuntime/librecomp/N64Recomp/N64Recomp aerogauge.us.toml\n\n")
        f.write("[input]\n")
        f.write(f"entrypoint = 0x{ENTRY:08X}\n")
        f.write('output_func_path = "RecompiledFuncs"\n')
        f.write('symbols_file_path = "aerogauge.syms.toml"\n')
        f.write('rom_file_path = "AeroGauge (USA).z64"\n\n')
        f.write("[patches]\n")
        f.write("stubs = [\n")
        for n in stubs:
            f.write(f'    "{n}",\n')
        f.write("]\n")
        # Natively-replaced enhancement hooks (see NATIVE_NAMES): body skipped, call sites
        # keep the bare name, src/ provides the symbol.
        f.write("ignored = [\n")
        for n in sorted(n for n in NATIVE_NAMES.values() if n in known):
            f.write(f'    "{n}",\n')
        f.write("]\n")
        # Verbatim hand-authored patch blocks (hooks/instruction patches; see PATCH_BLOCKS).
        f.write(PATCH_BLOCKS)

    n_auto = len(auto_stubs & known)
    named = sorted(n for n in LIBULTRA_NAMES.values() if n in known)
    missing_named = sorted((set(LIBULTRA_NAMES.values()) | set(NATIVE_NAMES.values())) - known)
    if missing_named:
        print(f"WARNING: LIBULTRA_NAMES/NATIVE_NAMES vram not a derived function start: {missing_named}")
    print(f"hook-named (toml ignored): {sorted(n for n in NATIVE_NAMES.values() if n in known)}")
    print(f"functions: {len(funcs)}  (jal+prologue-derived)")
    print(f"libultra-named: {len(named)}  {named}")
    print(f"stubs: {len(stubs)}  (auto CP0/branch-out: {n_auto}, force_stub.txt: {len(force & known)})")
    print(f"wrote {OUT_SYMS.name} + {OUT_CFG.name}")


if __name__ == "__main__":
    main()
