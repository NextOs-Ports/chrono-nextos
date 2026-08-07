# Changelog

## 1.0.0 — unreleased

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
