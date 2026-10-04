# ps5-dualsense-overlay - release artifact verifier
# Copyright (C) 2026 erickdavestech
# SPDX-License-Identifier: GPL-3.0-or-later
import pathlib
import sys

PLUGIN_HEADER = b"etaHEN_PLUGIN\0DSOV00001\0" + b"1.00\0"


def main() -> int:
    if len(sys.argv) != 4:
        print(
            "usage: verify_release.py controller.elf overlay.plugin shellui_renderer.elf",
            file=sys.stderr,
        )
        return 2

    elf, plugin, renderer = (pathlib.Path(arg).read_bytes() for arg in sys.argv[1:4])

    checks = [
        (elf.startswith(b"\x7fELF"), "controller is not an ELF"),
        (renderer.startswith(b"\x7fELF"), "renderer is not an ELF"),
        (len(renderer) > 4096, "renderer ELF is unexpectedly small"),
        (elf.find(renderer) > 0, "exact renderer ELF is not embedded in the controller"),
        (plugin.startswith(PLUGIN_HEADER), "plugin metadata mismatch"),
        (plugin[len(PLUGIN_HEADER):] == elf, "plugin body differs from the controller ELF"),
        (b"internal_fork=absent" in elf, "no-fork marker missing"),
        (b"renderer_injection=deferred_until_game" in elf, "deferred renderer injection marker missing"),
        (b"game_gate=process_present_stable_3s" in elf, "stable game-process gate missing"),
        (b"pad_overlay full ready" in renderer, "controller overlay renderer missing"),
        (b"file://%s/pad_%s" in renderer, "embedded sprite loader missing"),
        (b"file:///data" not in renderer, "renderer references /data, which hard-hangs SceShellUI"),
    ]

    failed = [message for ok, message in checks if not ok]
    for message in failed:
        print(f"ERROR: {message}", file=sys.stderr)
    if failed:
        return 1

    print("release artifacts: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
