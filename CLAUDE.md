# OPERATOR

An FM synth firmware for the M-VAVE FM-1 (JieLi AC79, pi32v2), inspired by Ableton Live's Operator, on the Felucca
platform cut down from dspaudio/orbit (GPL-3.0-only), the same platform as npi3pak/fm1-gator. Public repo: everything in English.

- Unity build: `firmware/sys/fw.c` includes every `.c`; include order matters. Apps live in `firmware/apps/NAME/`
  (`app.c` + optional `*.cc` + optional `third_party.txt` / `name.txt` / `logo.png` for the splash); `--app NAME`
  selects one (default `operator`; `demo` is the small example). The logo is drawn in canvas strips (LCD DMA reads RAM only).
- C is built without FPU (generic, as ORBIT); C++ with `-mcpu=r3 -mfprev1`. Cross the boundary with ints/pointers only.
  No static constructors (build fails on `.init_array`).
- The app: `app.c` (pages, presets, input) and `fm.cc` / `fm.h` (the engine, float). The engine is a boilerplate:
  4 sine operators, 4 voices, the algorithm table `MODS` / `CARRIERS` in `fm.cc` is provisional (a modulator always has
  a higher index than its target: operators render D -> A). The concept is still being decided with the user.
- Do not use Ableton's name or logo in the product's look; in texts say "inspired by Ableton Live's Operator" and
  "not affiliated with or endorsed by Ableton".
- The sample rate lives in `firmware/sys/fw_rate.h` only (C, C++ and the HAL clock bit); 48 kHz is the default and the only
  released package; `--fs 44` (build and sim) is kept for comparison. `FW_FS` is the rounded integer, `FW_FS_HZ` the exact float.
- `firmware/hal/`, `firmware/loader/`, `sys/usb.c`, `sys/ota.c`, `sys/recovery.c`, `sys/storage.c`, `sys/midi_uart.c`
  come from ORBIT and are hardware-verified there: change them only with a reason. The loader must stay byte-identical
  unless deliberately changed (it is what makes reflashing safe).
- Register access only in `hal/`; `tools/build.py` fails on MMIO literals, volatile casts or inline asm in `sys/`, `apps/`, `loader/`.
- `app_audio` runs in the ALNK0 ISR (256 frames, 5.8 ms); TIMER5 (input, USB) outranks it. No float in C, no malloc.
  Notes reach the engine through `fm.cc`'s event queue, drained at the start of each 16-frame block.
- Target build: `./build.sh` (Docker on macOS; the SDK files are vendored in `tools/sdk/`, Apache-2.0). Host check:
  `python tools/sim.py` (ASan/UBSan, fatal: a non-zero exit is a finding). Host results say nothing about timing.
- `sys/audio.c` acknowledges the ALNK half before rendering (ChoralRoot 0.18, heard on hardware); OTA sets
  `fw_audio_hold` so the app does not render; `fm1_cstart` raises the supplies to stock (SLOOP 2.7). Keep these.
- The envelope's times: `env_time_us` (app.c, integer table) and `env_seconds` (fm.cc) are the same curve
  (1 ms * 10^(4 v/1000)); change both.
- Presets: `bank_t` in app.c, `BANK_MAGIC` / `BANK_VERSION`; `fm_patch_t` is saved as is, so bump `BANK_VERSION`
  (or migrate) when its layout changes.
- Web installer: `web/` (fm1ota.js / fm1pkg.js are SLOOP 2.7's, vendored unchanged; the page and installer.js are
  ours, English only, dark). `web/make_site.py [--release build/release-X.Y]` -> `build/site`;
  `node web/test_web.mjs PKG` is the protocol test.
- Releases: tag `vX.Y` (one digit each, identity FM-1_9XY) -> `.github/workflows/release.yml` (`tools/release.py`) ->
  `pages.yml`. Release notes in `docs/releases/X.Y.md`. Licences: `LICENSE`, `LICENSING.md`, `LICENSES/` travel
  with every package (release and site).
- New files carry `SPDX-License-Identifier: GPL-3.0-only`; keep the existing copyright headers of platform files.
  A platform file changed for the first time gets the line "Modified for OPERATOR by npi3pak, 2026, from ORBIT 0.6.0".
  A new third-party part goes into LICENSING.md, README's licence table, LICENSES/ and the installer's Licences section.
- The splash stays `FW_SPLASH_MS` (main.c) from drawing it; `app_init` runs meanwhile and must not draw.
- Never claim hardware testing that did not happen.
