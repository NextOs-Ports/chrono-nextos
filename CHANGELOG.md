# Changelog

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
