# Changelog

## 1.0.5

Packaging-contract correction: the public ZIP now has one visible PortMaster
entry point, `Chrono Trigger.sh`. It contains the generated declarative config,
discovers `chrono/`, loads `chrono/nxbootstrap.sh` directly and calls
`nxbootstrap_main`. The redundant `chrono/run.sh` layer has been removed from
the source, package recipe and release gate.

- Runtime/game bytes are unchanged from the physically accepted build.
- The corrected ZIP passed deterministic host packaging, seven simulated ROM
  layouts, symlink and FAT-mode tests, and contains 21 entries with no
  `run.sh`.
- The exact corrected ZIP was then launched on the accepted NextOS Mali-450
  stack: PortMaster, NXExtract, JNI, GLES2/Mali video, PulseAudio and native
  input reached ready state; SIGTERM returned cleanly with no process left.
- Release v1.0.4 remains available as rollback evidence and was not
  overwritten.

Corrected ZIP SHA-256:
`2a9dec87e0742bf00b9e9f3af64e6b00218467e5f7665a769c687427a1f3277f`.

## 1.0.4

This is the publication release of the exact Chrono framework-pilot package
accepted on both supported stacks. The downloadable ZIP is byte-for-byte the
same package internally identified as 1.0.3; it was deliberately not rebuilt
after physical acceptance.

- The public repository now carries the framework sources used by the port:
  `nxloader`, `nxcompat`, `nxgl`, `nxinput` and `nxaudio`.
- The same ZIP was accepted on a NextOS Mali-450 stack with fbdev/PulseAudio
  and on an ArkOS RK3326 stack with KMSDRM/ALSA. Video, audio, native controls,
  the SELECT+START shutdown path and clean return were verified.
- The release asset is published with a SHA-256 companion file so the tested
  bytes can be checked after download.
- No proprietary game data is included. Installation still requires a
  user-owned Android APK and the bundled NXExtract flow.

Version 1.0.3 remains available as the rollback release. Tag 1.0.4 records the
source/framework promotion and publishes the already-tested package; it does
not claim a new game binary build.

## 1.0.3

The frame handed to the panel is now opaque. A player on an RG34XX-SP (muOS,
Mali-G31, SDL `mali` backend) reported that the screen never comes back after
the title fades out — with the engine plainly alive in the log: the scene
transition happens and the menu labels are built.

- **The backbuffer alpha is forced opaque immediately before the present.**
  Cocos leaves the game's own alpha in the default framebuffer. Measured on a
  Mali-450 at 1280×720: the menu frame carries **alpha 0 on 78.5 % of its
  pixels** and alpha 255 only where there is artwork; during the title fade it
  reaches 97.6 %. A compositor that ignores per-pixel alpha — the Amlogic OSD
  as NextOS configures it, and the opaque KMSDRM plane on ArkOS, the two
  devices this port was validated on — shows the picture anyway. A compositor
  that honours alpha reads the same frame as almost entirely transparent, and
  the panel goes black exactly when the title disappears.
- The clear reads the bound draw framebuffer, binds 0, writes **only** the
  alpha channel and puts back what it found, then logs once that it actually
  ran. A GLES3 driver can reach the swap with a non-zero FBO bound, which would
  send the clear to the FBO instead of the backbuffer and leave the flag on
  while nothing is fixed. The reporting device is GLES 3.2.
- `CHRONO_OPAQUE=0` turns it off for bench comparison.

Measured after the change: alpha is **255 on 100 % of the frame** and the RGB
content is byte-for-byte the same as before (21.4 % non-black on the menu, both
runs) — the clear touches nothing but alpha. Frame rate on the R36S over 150 s
per side: **60.0 fps steady with the fix, 42–45 fps without it**.

Also in this release: the Mali-450 regression pass is done, and the device
table now says so.

**Honest limit:** the port has no RG34XX-SP. This fix removes a measured defect
that explains the report on the hardware we do have; it has not been run on the
device that failed. Reports on Discord are welcome.

## 1.0.2

NXExtract recipe made tolerant, so a legitimate APK is never rejected for being
a different build of the same game.

- Assets are matched as `assets/*.dat` instead of a fixed `001`–`008` list: a
  build that renumbers, adds or drops a movie file still installs.
- Structural bounds widened everywhere (tree 8–96 files, 300 MB–1.6 GB;
  `libchrono.so` 6–48 MB; `resources.bin` 200 MB–1.2 GB). Nothing is pinned to
  one build's byte count.
- Required paths reduced to what every build has: `resources.bin`, `001.dat`
  and the shader tree. Region-dependent files (`007-en.dat`, `008.dat`) are
  installed when present and never demanded.
- The launcher's artefact gate matches that same list.
- Verified with `nxextract plan` against a real retail APK: the widened recipe
  resolves the identical 17 items / 564 MB as before.

Updating from 1.0.0/1.0.1 does not re-extract anything: the installer adopts
already-valid data and only rewrites the marker.

## 1.0.1

Portability audit against the multi-device contract; every fix below prevents a
"black screen" or "does not start" on hardware the release was never run on.

- **Video and audio now initialise as independent subsystems.** A single
  `SDL_Init(VIDEO|AUDIO|...)` meant a dead inherited PulseAudio took the whole
  boot down and the port never drew a frame. Audio failure is now scoped: the
  game starts, with picture, and says in the log that it has no sound.
- **Audio ladder**: `dummy` and `disk` no longer count as success, and one
  logged retry drops an invalid inherited `SDL_AUDIODRIVER`/`PULSE_SERVER`
  before giving up.
- **EGLConfig fallback ladder** (alpha 8→0, depth/stencil 24/8→16/0→0/0). The
  first rung is exactly the configuration validated on Mali-450 and on the R36S;
  a driver that refuses it now gets the next rung instead of a fatal error.
- **Desktop GL is rejected.** If the driver hands back a non-ES context, the
  context is recreated once against the GLES driver; Cocos2d-x shaders are GLSL
  ES and would otherwise render black.
- **Inherited `SDL_VIDEODRIVER`** that fails a real probe is dropped once, giving
  autodetection back to SDL — the backend is never chosen by us.

## 1.0.0

First universal BYO-data release of the Chrono Trigger compatibility loader.

- Single AArch64 executable built against GLIBC 2.27 (public ceiling: 2.30);
  SDL2, GLESv2 and FreeType come from the target firmware.
- Runtime capability detection: ROM root, real drawable size, SDL video/audio
  backend chosen by the firmware, PortMaster control mapping. No device-name
  profiles, no hardcoded resolution, no forced SDL backend.
- `glFinish` before the buffer swap only on KMSDRM (30 → 60 fps there, no cost
  on fbdev).
- Exact EGLConfig logged once (RGBA8888 vs. the driver's RGBX8888 default).
- Single instance enforced with `flock` on the executable itself.
- SELECT+START (SDL and raw evdev, including TRIGGER_HAPPY1/2), SIGTERM and the
  engine's own quit paths all converge on one shutdown: pause/save, then exit,
  with a deadline so nothing keeps the display or audio device.
- UI font resolved at runtime: bundled Noto Sans (SIL OFL 1.1) or a suitable
  firmware font. No Android system font is redistributed.
- BYO-data installation through NXExtract 1.2.3, with a structural recipe that
  accepts every known Play build instead of a single hash.
