# Contributing

Thanks for your interest in ps5-dualsense-overlay. Bug reports, tests on other consoles and firmware
versions, and pull requests are all welcome.

By participating you agree to follow the [Code of Conduct](CODE_OF_CONDUCT.md).

## Reporting bugs

Open an issue with the **Bug report** template and include:

- the overlay version and whether you used the `.elf` or the `.plugin`;
- console model and exact system software version;
- homebrew enabler and payload loader;
- the game you were running;
- what you did, what you expected and what happened;
- the renderer log, `/system_tmp/padoverlay_shellui.log`, if you can read it.

Make sure only one copy of the overlay was loaded in that boot before reporting a crash.

Security problems must not be reported in public issues; follow [SECURITY.md](SECURITY.md).

## Suggesting features

Open an issue with the **Feature request** template and describe the use case.

## Development

Build instructions, host tests and the release verifier are described in [BUILDING.md](BUILDING.md).
Before opening a pull request:

1. Build the payloads with `scripts/ps5_source_build.sh` (it runs `tools/verify_release.py`).
2. Run the host tests and `python3 tests/test_plugin_wrapper.py`.
3. Test on hardware when your change affects runtime behavior, and say on which console and system
   software you tested.

## Code guidelines

- Follow the style of the surrounding code (C17 / C++20).
- Keep source code free of explanatory comments; documentation belongs in the README, BUILDING.md and
  CHANGELOG.md. License headers are required.
- Every source file starts with its license header and SPDX identifier. Never remove or alter
  existing copyright notices. When you substantially change a file, add a
  `Modifications Copyright (C) <year> <name>` line to its header.
- The renderer must never access `/data` (`SceShellUI` hard-hangs the console). Use `/Temp`.
- Never read or write game process memory.
- Keep the renderer injection deferred until a real game is running.
- Update `CHANGELOG.md` under an "Unreleased" section.

## Licensing of contributions

This project is licensed under GPL-3.0-or-later. By submitting a contribution you agree that it is
licensed under the same terms. Changes to the controller artwork remain under CC BY 3.0 and must keep
the attribution to Zacksly.

Please sign off your commits (`git commit -s`) to certify the
[Developer Certificate of Origin](https://developercertificate.org/): you wrote the change or have the
right to submit it under this license.

## Out of scope

The project does not accept contributions that add exploits, jailbreaks, copy-protection circumvention,
piracy features, or cheats for online play.
