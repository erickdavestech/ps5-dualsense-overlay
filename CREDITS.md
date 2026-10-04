# Credits

ps5-dualsense-overlay is a derivative work. It claims no ownership over the upstream code or the
artwork it builds on. Every source file keeps its original copyright header and SPDX identifier, and
each modified file names who modified it.

## Upstream code

- **Common FPS for PS5** — **porhe911**
  https://github.com/porhe911/Common-FPS-for-PS5 — GPL-3.0-or-later
  The base of this project: the controller payload, the `SceShellUI`/PUI (Mono) injection, the hook
  protocol, the build system, the etaHEN plugin packaging and the host tests. This project starts from
  Common FPS for PS5 v1.2.1.

- **SimpleFPS** — **khalifa007**
  https://github.com/khalifa007/SimpleFPS — GPL-3.0-or-later
  The patch that lets Common FPS run on system software 11.xx–13.xx (hook backend selection and the
  MDBG write path for those firmware families) and moves the overlay to the top-left corner by default.

## Artwork

- **"PS5 Button Icons and Controls"** — **Zacksly**
  https://zacksly.itch.io — CC BY 3.0
  Used to draw the on-screen controller. **The artwork was modified**: cropped and recomposed, sticks
  split into movable sprites, pressed states recolored with a glow, L2/R2 recolored and placed over the
  shoulders. The original license is kept in `assets/source/LICENSE.txt`.

  > "PS5 Button Icons and Controls - Zacksly
  > Licensed under CC BY 3.0 - https://zacksly.itch.io"

## Screenshots

The images in `docs/screenshots/` show the overlay over third-party games and belong to their respective
owners: *Wuchang: Fallen Feathers* © 2025 505 Games, developed by Leenzee (`title-screen.webp`); the
gameplay image (`gameplay.webp`) belongs to its respective copyright holder. They are used only to
illustrate the overlay and are not covered by this project's licenses.

## PS5 homebrew projects

- **PS5 payload SDK** — John Törnblom — https://github.com/ps5-payload-dev/sdk
  Toolchain and system headers used to build the payloads.
- **shsrv** — John Törnblom — https://github.com/ps5-payload-dev/shsrv — GPLv3+
  Reference for the ptrace ELF loader and the payload-arguments contract (fetched at build time).
- **elfldr** — John Törnblom — https://github.com/ps5-payload-dev/elfldr — GPLv3+
  The ptrace helpers in `probe/pt/` come from this project.
- **etaHEN** — LightningMods and contributors — https://github.com/etaHEN/etaHEN — GPLv3
  Homebrew enabler whose sources the build uses and whose plugin format the release follows.

Research references mentioned in `probe/`: the Ghostpad ptrace-RPC technique and the
ps5-native-gamepad-input-research notes.

## Original work in this project

By **erickdavestech**. Relative to Common FPS for PS5 v1.2.1 with the SimpleFPS patch:

| Area | Files |
|---|---|
| DualSense overlay renderer, controller input, overlay lifecycle, sprite cleanup, backend order | `src/ps5/shellui_payload/commonfps_shellui.cpp`, `src/ps5/shellui_payload/commonfps_shellui_entry.cpp` |
| `libScePad` resolution with NID fallback | `src/ps5/stable_elfldr_bridge.c` |
| FPS sampler removal, injection flow | `src/ps5/commonfps_ps5_main.cpp`, `src/ps5/shellui_injector.cpp` |
| Game detection that ignores system applications and dialogs | `src/ps5/stable_sampler/process_sysctl.cpp`, `src/ps5/stable_sampler/process_sysctl.hpp`, `src/ps5/ps5_platform.cpp` |
| Hook backend for 11.xx–13.xx and multi-page RWX write | `include/common_fps/shellui_hook_protocol.hpp`, `src/ps5/shellui_hook_controller.cpp` |
| Artwork pipeline and embedded sprite tables | `tools/build_assets.py`, `assets/`, `src/ps5/shellui_payload/pad_assets*` |
| Release verification and plugin identity (`DSOV00001`) | `tools/verify_release.py`, `scripts/ps5_source_build.sh`, `ps5/CMakeLists.txt` |
| Tests updated for the new behavior | `tests/test_core.cpp`, `tests/test_v1_parity.cpp` |
| Research probes | `probe/main.c`, `probe/pad_rpc.c` |

The source code is GPL-3.0-or-later; the artwork remains CC BY 3.0.
