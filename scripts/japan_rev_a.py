"""Japanese Rev A recompiler boundaries; see docs/reference/rom.md."""

LIBULTRA_NAMES = {
    0x8006D640: "__osInitialize_common",
    0x80068640: "osCreateThread",
    0x80068790: "osStartThread",
    0x80070760: "osSetThreadPri",
    0x800668C0: "osCreateMesgQueue",
    0x80069160: "osSendMesg",
    0x80066B20: "osRecvMesg",
    0x80070230: "osJamMesg",
    0x8006B890: "osSetEventMesg",
    0x800700C0: "osViSetEvent",
    0x80070050: "osViSetMode",
    0x8006DB80: "osViSetSpecialFeatures",
    0x80070380: "osViSwapBuffer",
    0x800701B0: "osViGetCurrentFramebuffer",
    0x800701F0: "osViGetNextFramebuffer",
    0x8006DD40: "osViBlack",
    0x80078830: "osGetThreadPri",
    0x80070840: "osCreatePiManager",
    0x800669A0: "osPiStartDma",
    0x80068940: "osVirtualToPhysical",
    0x800668F0: "osInvalDCache",
    0x80077100: "osWritebackDCache",
    0x80077BD0: "osGetCount",
    0x80077DE4: "osSetTime",
    0x80068420: "osAiSetFrequency",
    0x800689C0: "osAiSetNextBuffer",
    0x80068A70: "osAiGetLength",
    0x8006FCF0: "osCreateViManager",
    0x80074480: "osSetTimer",
    0x800704DC: "osSpTaskLoad",
    0x80070644: "osSpTaskStartGo",
    0x80070740: "osSpTaskYield",
    0x80070130: "osSpTaskYielded",
    0x80070690: "osDpSetNextBuffer",
    0x8006B970: "osContInit",
    0x8006D8E0: "osGetTime",
    0x8006BD30: "osContStartReadData",
    0x8006BDF4: "osContGetReadData",
    0x8006EA70: "osEepromProbe",
    0x8006EAE0: "osEepromRead",
    0x80078350: "osEepromWrite",
    0x8006EF20: "osEepromLongRead",
    0x8006EDE0: "osEepromLongWrite",
    0x8007B7A4: "osMotorInit",
    0x8007BA64: "osMotorStop",
    0x8007B93C: "osMotorStart",
}

NATIVE_NAMES = {
    0x8000757C: "aeroRegisterTrackSections",
    0x80007758: "aeroRegisterZoneObjects",
    0x8006C234: "aero_pak_status",
    0x80075260: "aero_pak_read",
    0x80077FE0: "aero_pak_write",
    0x8006C820: "guPerspectiveF",
}

BOOT_EXTRA = [0x80000400, 0x80000450, 0x80065DB0]
INDIRECT_STARTS = [0x80002164, 0x8000E480, 0x8000A390, 0x8000E540,
    0x8000FDF0, 0x8000FFCC, 0x800101E0, 0x80001480, 0x80008040,
    0x8000819C, 0x8000843C, 0x8000D850, 0x80019920, 0x8005CED4]


# (function entry, instruction boundary, expected instruction, native call).
# Reviewed from Rev A disassembly; register/stack contracts match unless
# explicitly adapted in aero_race_intro.c. Guards reject stale boundaries.
HOOKS = [
    (0x80007980, 0x80007AB0, 0x4614C03C, 'extern void aero_car_lod_visibility(uint8_t*, recomp_context*); aero_car_lod_visibility(rdram, ctx);'),
    (0x8005A38C, 0x8005A3A4, 0x90820001, 'extern void aero_car_lod_model(uint8_t*, recomp_context*); aero_car_lod_model(rdram, ctx);'),
    (0x8005A38C, 0x8005A43C, 0x150E003D, 'extern void aero_car_lod_mesh_mode(uint8_t*, recomp_context*); aero_car_lod_mesh_mode(rdram, ctx);'),
    (0x8005A660, 0x8005A6F4, 0x17210004, 'extern void aero_car_lod_animation_mode(uint8_t*, recomp_context*); aero_car_lod_animation_mode(rdram, ctx);'),
    (0x80023238, 0x8002347C, 0x3C19801A, 'extern void aero_scene_scissor(uint8_t*, recomp_context*); aero_scene_scissor(rdram, ctx);'),
    (0x80023238, 0x800235EC, 0x8FBF001C, 'extern void aero_scene_background(uint8_t*, recomp_context*); aero_scene_background(rdram, ctx);'),
    (0x800165FC, 0x800165FC, 0x3C038014, 'extern void aero_warp_tick(uint8_t*, recomp_context*); aero_warp_tick(rdram, ctx);'),
    (0x800165FC, 0x80016600, 0x2463D000, 'extern void aero_savestate_tick(uint8_t*, recomp_context*); aero_savestate_tick(rdram, ctx);'),
    (0x80022E38, 0x80022E38, 0x27BDFF80, 'extern void aero_ws_hud_scan_begin(uint8_t*, recomp_context*); aero_ws_hud_scan_begin(rdram, ctx);'),
    (0x80022E38, 0x800230C8, 0x8FB00018, 'extern void aero_ws_hud_frame_end(uint8_t*, recomp_context*); aero_ws_hud_frame_end(rdram, ctx);'),
    (0x8000DDA0, 0x8000DDA0, 0x3C088014, 'extern void aero_intro_ticker_begin(uint8_t*, recomp_context*); aero_intro_ticker_begin(rdram, ctx);'),
    (0x8000DDA0, 0x8000DDEC, 0x240500B9, 'extern void aero_intro_ticker_origin(uint8_t*, recomp_context*); aero_intro_ticker_origin(rdram, ctx);'),
    (0x8000DDA0, 0x8000E130, 0xADCD0000, 'extern void aero_intro_ticker_end(uint8_t*, recomp_context*); aero_intro_ticker_end(rdram, ctx);'),
    (0x8000E144, 0x8000E380, 0x8FBF0014, 'extern void aero_intro_banner(uint8_t*, recomp_context*); aero_intro_banner(rdram, ctx);'),
    (0x8002025C, 0x80020A8C, 0x8FBF0024, 'extern void aero_intro_glyph(uint8_t*, recomp_context*); aero_intro_glyph(rdram, ctx);'),
    (0x80019C94, 0x80019E5C, 0x0C00863A, 'extern void aero_intro_fade_begin(uint8_t*, recomp_context*); aero_intro_fade_begin(rdram, ctx);'),
    (0x80019C94, 0x80019E64, 0x8FB8003C, 'extern void aero_intro_fade_end(uint8_t*, recomp_context*); aero_intro_fade_end(rdram, ctx);'),
    (0x80019C94, 0x80019F58, 0x0C00863A, 'extern void aero_intro_fade_begin(uint8_t*, recomp_context*); aero_intro_fade_begin(rdram, ctx);'),
    (0x80019C94, 0x80019F60, 0x8FAD003C, 'extern void aero_intro_fade_end(uint8_t*, recomp_context*); aero_intro_fade_end(rdram, ctx);'),
    (0x8001C7C0, 0x8001C7C0, 0x27BDFFA0, 'extern void aero_ws_message_begin(uint8_t*, gpr); aero_ws_message_begin(rdram, MEM_W(0, ctx->r4));'),
    (0x8001C7C0, 0x8001C9E8, 0x8DCE9448, 'extern void aero_ws_message_end(uint8_t*, gpr); aero_ws_message_end(rdram, MEM_W(0x58, ctx->r29));'),
    (0x8001B628, 0x8001B628, 0x27BDFFC0, 'extern void aero_ws_message_begin(uint8_t*, gpr); aero_ws_message_begin(rdram, MEM_W(0, ctx->r4));'),
    (0x8001B628, 0x8001B6EC, 0x8FBF001C, 'extern void aero_ws_message_end(uint8_t*, gpr); aero_ws_message_end(rdram, MEM_W(0x3C, ctx->r29));'),
    (0x8001B6FC, 0x8001B6FC, 0x27BDFFC0, 'extern void aero_ws_message_begin(uint8_t*, gpr); aero_ws_message_begin(rdram, MEM_W(0, ctx->r4));'),
    (0x8001B6FC, 0x8001B848, 0x8FBF0024, 'extern void aero_ws_message_end(uint8_t*, gpr); aero_ws_message_end(rdram, MEM_W(0x3C, ctx->r29));'),
    (0x80019FB4, 0x80019FB4, 0x3C0E8014, 'extern void aero_ws_message_begin(uint8_t*, gpr); aero_ws_message_begin(rdram, MEM_W(0, ctx->r4));'),
    (0x80019FB4, 0x8001A094, 0x8FBF0014, 'extern void aero_ws_message_end(uint8_t*, gpr); aero_ws_message_end(rdram, MEM_W(0x24, ctx->r29));'),
    (0x8001AA6C, 0x8001ADE4, 0x240500C8, 'extern void aero_ws_message_begin(uint8_t*, gpr); aero_ws_message_begin(rdram, MEM_W(0x94, ctx->r29));'),
    (0x8001AA6C, 0x8001B000, 0x00002025, 'extern void aero_ws_message_begin(uint8_t*, gpr); aero_ws_message_begin(rdram, MEM_W(0x94, ctx->r29));'),
    (0x8001AA6C, 0x8001B190, 0x8FBF003C, 'extern void aero_ws_message_end(uint8_t*, gpr); aero_ws_message_end(rdram, MEM_W(0x94, ctx->r29));'),
    (0x8005CCD0, 0x8005CD28, 0x3C188014, 'extern void aero_turbo_boost_tick(uint8_t*, recomp_context*); aero_turbo_boost_tick(rdram, ctx);'),
    (0x80058308, 0x80058D2C, 0xC60C0024, 'extern void aero_haptics_race_tick(uint8_t*, recomp_context*); aero_haptics_race_tick(rdram, ctx);'),
    (0x800165FC, None, None, 'extern void aero_haptics_frame(uint8_t*, recomp_context*); aero_haptics_frame(rdram, ctx);'),
]

PATCH_BLOCKS = "\n".join(
    "\n[[patches.hook]]\n"
    + f'func = "jp_func_{entry:08X}"\n'
    + (f"before_vram = 0x{address:08X}\n" if address is not None else "")
    + f'text = "{text}"\n'
    for entry, address, word, text in HOOKS
)
