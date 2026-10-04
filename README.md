# ps5-dualsense-overlay

[![License: GPL-3.0-or-later](https://img.shields.io/badge/license-GPL--3.0--or--later-blue.svg)](LICENSE)
[![Latest release](https://img.shields.io/github/v/release/erickdavestech/ps5-dualsense-overlay)](https://github.com/erickdavestech/ps5-dualsense-overlay/releases/latest)
[![CI](https://github.com/erickdavestech/ps5-dualsense-overlay/actions/workflows/ci.yml/badge.svg)](https://github.com/erickdavestech/ps5-dualsense-overlay/actions/workflows/ci.yml)

On-screen **DualSense controller overlay** for a PlayStation 5 running homebrew. A single payload
runs entirely on the console and draws the controller over the running game, lighting up buttons,
sticks, triggers and the touchpad in real time.

> Educational project about PS5 homebrew, process injection and the system UI (`SceShellUI`,
> PUI on Mono). Use it only on hardware you own and read the [legal notice](#legal-notice).

**[Download the latest release](https://github.com/erickdavestech/ps5-dualsense-overlay/releases/latest)**

## Screenshots

![The DualSense overlay over the title screen of Wuchang: Fallen Feathers](docs/screenshots/title-screen.webp)

![The DualSense overlay during gameplay](docs/screenshots/gameplay.webp)

<sub>Game images: <i>Wuchang: Fallen Feathers</i> © 2025 505 Games, developed by Leenzee (first image); the
second image belongs to its respective copyright holder. They are shown only to illustrate the overlay.
This project is not affiliated with or endorsed by the game publishers.</sub>

## Download

Get `ps5-dualsense-overlay-<version>.elf` from the
[latest release](https://github.com/erickdavestech/ps5-dualsense-overlay/releases/latest). The
release also includes `SHA256SUMS.txt` to verify the file.

## Features

- Every functional button lights up: ✕ ○ △ □, D-pad, L1/R1, L2/R2, L3/R3, Create, Options and the
  touchpad click.
- Sticks follow the axes and show a color ring when moved or clicked.
- L2/R2 show an analog fill proportional to the pressure; the touchpad shows a dot under the finger.
- Semi-transparent, placed bottom-left and sharp at 4K.
- Load it once per boot: it waits for a game, appears when one opens and rebuilds itself when you
  switch games.
- Sprites are released when a system menu intercepts the input, so nothing stays stuck.
- It never reads or writes game memory.

## Compatibility

| Console | System software | Status |
|---|---|---|
| PS5 Slim (CFI-2015) | 13.60 (13.600.007) | Tested |
| PS5 | 11.xx – 13.50 | Allowed by the code, not tested |
| PS5 | 10.xx and earlier | Code paths inherited from upstream, not tested with this overlay |

Tested with kstuff-lite, loading the `.elf` through the payload manager's web portal. Other homebrew
enablers may work but have not been tested.

## Usage

1. Jailbreak the console as usual (tested with kstuff-lite).
2. Load `ps5-dualsense-overlay-<version>.elf` with your payload manager, in either of these ways:
   - **Web portal:** from a PC or phone on the same network, open the payload manager's portal using
     the console's local IP and upload the `.elf`.
   - **USB:** copy the `.elf` to a USB drive, connect it to the console and launch it from the payload
     manager.
3. Open any game. The controller appears in the bottom-left corner.

Load it only **once** per boot, and do not run it together with Common FPS or SimpleFPS: they inject
into the same system process. To update, reboot and load the new version.

## Troubleshooting

| Symptom | Cause and fix |
|---|---|
| Nothing appears | The overlay only draws while a game is in the foreground. Open a game. |
| *System Software Error* right after loading | A second copy was loaded on top of a running one, or Common FPS / SimpleFPS is also running. Reboot and load only this overlay, once. |
| The PS button never lights up | Expected. The system intercepts it before any application can read it. |
| The Mute button never lights up | Expected. It is disabled, see [Known limitations](#known-limitations). |

The renderer writes its log to `/system_tmp/padoverlay_shellui.log`, which can be read with a shell
payload such as shsrv.

## Known limitations

- **PS button:** intercepted by the system to open the Control Center; it cannot be read.
- **Mute button:** the controller only reports the momentary press, not the microphone state, so the
  button stays disabled until a reliable state source is found.
- The overlay position and size are fixed.
- Only one firmware version has been tested on hardware.

## How it works

1. A **controller** payload waits until a real game is running — system applications and dialogs are
   filtered out by title ID — and until the system UI is stable. Then it injects a **renderer** into
   `SceShellUI`.
2. The renderer hooks the UI update loop (`Application.Update`, with the legacy main-thread guard as a
   fallback), reads the DualSense with `scePadReadState` from inside `SceShellUI` and draws `ImageBox`
   sprites in the game's scene.
3. The sprite images are embedded in the payload and written to `/Temp` at startup. `SceShellUI`
   cannot read `/data`, and loading `file:///data/...` hard-hangs the console.
4. When the game scene changes, the old sprites are removed and the overlay is rebuilt for the new
   game.

On system software 11.xx–13.xx the one-byte hook writes go through MDBG, with a temporary RWX remap of
every code page involved.

## Building from source

See [BUILDING.md](BUILDING.md).

## Project layout

```text
src/ps5/shellui_payload/   renderer injected into SceShellUI (drawing, controller input)
src/ps5/                   controller payload (game detection, injection, hook control)
include/common_fps/        shared protocol and data types
assets/                    controller artwork: source SVGs, PNGs, layout and an HTML preview
tools/                     artwork pipeline, etaHEN plugin packer, release verifier
probe/                     research programs written while learning how to read the controller
tests/                     host tests
docs/                      upstream Common FPS for PS5 documentation, kept for reference
```

## Authorship

This repository is a derivative work of [Common FPS for PS5](https://github.com/porhe911/Common-FPS-for-PS5)
by **porhe911**, including the [SimpleFPS](https://github.com/khalifa007/SimpleFPS) patch by
**khalifa007** that enables system software 11.xx–13.xx. Their work provides the injection, the hook
infrastructure and the build system.

**Original work in this project, by erickdavestech:**

- The DualSense overlay renderer: sprite composition, live mapping of buttons, sticks, triggers and
  touchpad, transparency, 4K-sharp scaling and placement.
- Reading the controller from inside `SceShellUI` (`libScePad` resolved with a NID fallback) and
  neutral handling of intercepted input.
- The overlay lifecycle: arming without a game, rebuilding on game changes and removing orphaned
  widgets.
- Stability work: filtering system applications out of game detection, the generalized MDBG + RWX hook
  write on 11.xx–13.xx, the `Application.Update` hook preference with legacy fallback, and the removal
  of the FPS sampler so the payload never attaches to or scans the game process.
- The artwork pipeline (`tools/build_assets.py`, layout and embedded sprite tables) and the
  `/Temp`-based asset loading.
- The research probes in `probe/` and the release verifier.

Each source file states its authors in its license header. The complete credits are in
[CREDITS.md](CREDITS.md) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md); every change is listed in
[CHANGELOG.md](CHANGELOG.md).

## Contributing

Bug reports and pull requests are welcome. Read [CONTRIBUTING.md](CONTRIBUTING.md) and the
[Code of Conduct](CODE_OF_CONDUCT.md) first. Report security problems privately as described in
[SECURITY.md](SECURITY.md).

## License

- Source code: **GPL-3.0-or-later** — see [LICENSE](LICENSE). It inherits this license from Common FPS
  for PS5 and SimpleFPS.
- Controller artwork: **"PS5 Button Icons and Controls" by Zacksly**, licensed **CC BY 3.0**
  (https://zacksly.itch.io), modified — see [`assets/source/LICENSE.txt`](assets/source/LICENSE.txt).

Every file keeps its copyright header and SPDX license identifier. The screenshots in
`docs/screenshots/` show third-party games and are not covered by this project's licenses.

## Legal notice

- PlayStation, PS5 and DualSense are trademarks or registered trademarks of Sony Interactive
  Entertainment Inc. This project is not affiliated with, authorized, sponsored or endorsed by Sony
  Interactive Entertainment.
- No Sony code, firmware, encryption keys, official SDK or game content is included. The project is
  built with the open-source PS5 payload SDK.
- The screenshots show third-party games; those images remain the property of their respective owners
  and are used only to illustrate this project.
- This project does not contain or distribute any exploit, jailbreak or copy-protection circumvention
  tool, and it does not enable running unauthorized copies of software. It only runs on a console on
  which its owner has already enabled homebrew.
- Modifying a console may void its warranty and break the platform's terms of service. Use it offline
  and at your own risk.
- The software is provided "as is", without warranty of any kind, as stated in the GPL-3.0.
