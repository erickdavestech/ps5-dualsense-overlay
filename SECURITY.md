# Security Policy

## Supported versions

| Version | Supported |
|---|---|
| Latest release | Yes |
| Older releases | No — please update to the latest release |

## Reporting a problem

Do not open a public issue, discussion or pull request for security problems. Report them privately
through GitHub: open the repository's **Security** tab and choose **Report a vulnerability**.

Please include:

- the overlay version and how it was loaded;
- console model, system software version and homebrew enabler;
- a description of the problem and its impact;
- steps to reproduce and, if possible, the renderer log (`/system_tmp/padoverlay_shellui.log`).

## Response process

- **Acknowledgement:** the maintainer aims to acknowledge every report within 7 days.
- **Assessment:** within 30 days you will be told whether the report is confirmed and, if so, the plan
  to fix or mitigate it.
- **Fix and release:** confirmed problems are fixed in a new release; the advisory and the release notes
  describe the impact and the affected versions.
- **Coordinated disclosure:** please keep the details private until a fix is released, or for 90 days
  after the report, whichever comes first, unless a different timeline is agreed.
- **Credit:** reporters are credited in the advisory and the release notes unless they prefer to remain
  anonymous.

## Scope

In scope: problems in this project's code, for example behavior that corrupts memory of a process it
should not touch, writes outside the documented paths, or crashes the system UI in a reproducible way.

Out of scope: vulnerabilities in the PS5 system software, in homebrew enablers or in payload managers —
report those to their respective projects. This project does not accept or publish exploits.

## Verifying downloads

Download the overlay only from this repository's
[Releases page](https://github.com/erickdavestech/ps5-dualsense-overlay/releases). Each release
includes `SHA256SUMS.txt`; check the file before loading it:

```bash
sha256sum -c SHA256SUMS.txt
```

Release tags (`v*`) are protected against deletion and modification, commits on `main` are signed and
show as **Verified** on GitHub, and every release can be rebuilt from source to obtain the same binary
(see [BUILDING.md](BUILDING.md)).
