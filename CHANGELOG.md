# Changelog

All notable changes to this project are documented in this file. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/).

## [Unreleased]

### Changed

- Usage documentation simplified to one method: upload the `.elf` through the payload manager's web
  portal. Releases ship the `.elf` only.

## [1.0.0] - 2026-10-03

First public release. Based on Common FPS for PS5 v1.2.1 by porhe911 with the SimpleFPS patch by
khalifa007.

### Added

- DualSense controller overlay: buttons, sticks with an L3/R3 ring, analog L2/R2 fill and touchpad
  dot; semi-transparent, bottom-left and sharp at 4K.
- Controller input read from inside `SceShellUI` with `scePadReadState` (`libScePad` resolved with a
  NID fallback).
- Embedded controller artwork ("PS5 Button Icons and Controls" by Zacksly, CC BY 3.0, modified) and
  the pipeline that generates it (`tools/build_assets.py`).
- The overlay arms itself without a game, appears when a game opens and rebuilds on game changes.
- Release verifier (`tools/verify_release.py`) and an etaHEN plugin identity of its own
  (`DSOV00001`, version 1.00).
- Research probes in `probe/`.

### Changed

- Game detection ignores system applications and dialogs (title IDs starting with `NPXS`, read with
  `sceKernelGetAppInfo`), so the renderer is never injected for a system dialog.
- The renderer prefers the `Application.Update` hook and falls back to the legacy main-thread guard.
- On system software 11.xx–13.xx the hook write uses MDBG with a temporary RWX remap that covers every
  code page the write touches.
- Released sprites are removed from the scene, and leftover widgets are cleaned up before rebuilding.
- Assets are written to and loaded from `/Temp`, because `SceShellUI` cannot read `/data`.
- Host tests updated for the new firmware backends and defaults.
- The build script runs the release verifier of this project.

### Removed

- FPS counter drawing and the FPS sampler: the payload no longer attaches to, scans or reads the game
  process, and no longer touches `/dev/dce`.
- Upstream FPS configuration example and CI workflows that do not apply to this project.

### Fixed

- *System Software Error* crashes when loading the overlay: system dialogs are no longer treated as
  games, the hook write covers every page it touches and released sprites no longer leak in the scene.
- Controller sprites staying pressed while a system menu intercepts the input.

### Known limitations

- The PS button cannot be read (intercepted by the system).
- The Mute button is disabled: the controller only reports the momentary press.

[1.0.0]: https://github.com/erickdavestech/ps5-dualsense-overlay/releases/tag/v1.0.0
