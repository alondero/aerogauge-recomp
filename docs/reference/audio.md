# Audio and accessories reference

## Audio path

The game submits N64 audio tasks. The current path is:

~~~text
translated game audio code
    -> M_AUDTASK
    -> generated aspMain RSP code
    -> AI buffer
    -> byte-order correction
    -> master gain (host copy only)
    -> SDL audio queue
~~~

The RSP input and its ROM locations are recorded in
[the ROM reference](rom.md#audio-microcode). The generated file
src/aspMain.cpp is a build product. If it is missing, regenerate it with
RSPRecomp after the first CMake configure.

The port keeps a persistent SDL audio conversion stream. This matters when
the game source rate differs from the obtained host device rate: conversion
state must survive across submitted buffers. The audio module also has a
headless virtual AI FIFO so a no-window test can exercise game-side audio
backpressure without requiring an audio device.

Both host build scripts apply patch 0013: non-graphics RSP tasks execute on
the submitting thread before the game can reuse the task descriptor and
command buffer. A failed task still signals SP completion instead of exiting
the process. Omitting this patch on Linux allowed an audio-task failure to
terminate the game during Time Trial's Controller Pak check. The
`rsp_task_submission` test exercises the production submission code, including
task failure followed by another successful submission.

The ROM has separate paths for sequenced music and short sound clips. Both
paths eventually submit M_AUDTASK work to the same generated aspMain output.
The sequenced music loader waits for the message belonging to its own PI DMA
request. Local patch 0012 preserves that message instead of posting a null
completion value; without it, a song can remain waiting even though its ROM
read finished.

Normal player audio uses SDL on the supported Windows and Linux targets. An
unavailable host device is a host setup problem; it does not prove that the
generated audio task is wrong. Use the audio statistics and RMS probes in
[debugging](../debugging.md) to separate those cases.

## Master volume and mute

The port owns a player-facing master volume and a mute switch. They live on
the port's own Audio settings page, persist in enhancements.json as
`master_volume` (0-100) and `master_muted`, and are overridable for one run
with AERO_MASTER_VOLUME. The port settings own the persistence; the audio
module owns the cross-thread gain snapshot and the host PCM conversion.

The gain is applied to the host's own copy of the submitted PCM, after the
byte-order correction and before the host conversion. Consequences that the
current implementation guarantees:

- The guest's source buffer is never written to, so a buffer the game reuses
  is never attenuated twice.
- The guest's own music, effects and announcer balance is unchanged. Nothing
  in the generated mixer is touched.
- Scaling rounds half away from zero and saturates to the int16 range. A gain
  of 1.0 is an exact pass-through and 0.0 is exact silence.
- The published gain is clamped to [0, 1]. The port attenuates; it never
  amplifies the source.

The gain is a cross-thread snapshot: the settings layer publishes it from the
SDL main thread, and the audio path reads it once per submitted buffer. A
change therefore lands on the next buffer, without a lock, without reopening
the device, and without resetting the conversion stream.

Muting silences the host output only. The game keeps synthesising at the same
cadence, the virtual AI FIFO keeps advancing, and the guest-side backpressure
is unchanged. The RMS probe reports the PCM the guest submitted, so a muted run
still shows the generated content; it is not a measurement of speaker output.

## Audio evidence

Useful checks are:

| Check | What it establishes |
| --- | --- |
| audio_playback_buffering | Host-side queue and conversion behavior |
| audio_intro_playback | Windows intro/attract playback with a real device |
| audio_task_crash | The headless generated audio path does not hit the known task failure |
| AERO_AUDIO_STATS=1 | Device rate, queue, and rebuffer information |
| AERO_AUDIO_RMS=1 | Whether submitted PCM is non-zero over time |

The full prerequisites and exact commands are in
[testing](../testing.md). Do not call a no-device headless run proof of
speaker output.

## Controller Pak

The current port exposes one virtual raw Controller Pak for Controller 1.
The image is 32 KiB and is used by the ROM's PFS calls. The default location
is under the application save directory; AERO_PAK_PATH can select an explicit
file for tests or a controlled setup.

The port also supplies a small EEPROM device through the runtime. EEPROM is
separate from the Controller Pak and must not be treated as the same save
file.

The [controllers and accessories reference](../controllers.md) describes the
input, Controller Pak, EEPROM, and rumble boundaries.

## Haptics

Game rumble requests are translated to SDL gamepad vibration. The current
player path samples one physical controller. A controller without a rumble
motor can still provide input; it simply cannot demonstrate the vibration
output. The Windows end-to-end haptics check uses the real ROM and a debugger
watch to confirm the game event reaches the port callback.

## Ownership rules

- The game owns the guest audio task and its source buffers.
- RSPRecomp owns the generated aspMain translation.
- aero_audio.cpp owns host conversion, queueing, and headless backpressure.
- SDL owns the host audio device and gamepad API.
- Save files are host data. They are not guest RDRAM and are not part of a
  save-state snapshot.

## Shutdown ordering

The audio device is released by the port, after the runtime teardown has
returned. The order matters because the guest threads are what submit PCM:

1. The shutdown request stops new guest work. The guest threads unwind, so no
   further PCM is submitted.
2. The EEPROM and settings stores are flushed.
3. The guest threads are joined. Until they have all unwound, a submission can
   still arrive from a guest thread that is still executing translated code.
4. The runtime teardown joins the graphics, VI, task, and saving threads and
   ends the renderer.
5. The port closes the audio device and destroys the SDL window.

`aero::audio::shutdown()` frees the conversion stream and closes the SDL
device. A submission that arrives after it is dropped by the device check in
`submit()`, so the device is safe to close; the ordering above exists so that
no PCM is lost on the way out, not to defend against a late submission.

When a failure crosses these boundaries, capture evidence at the boundary
before changing the next layer.
