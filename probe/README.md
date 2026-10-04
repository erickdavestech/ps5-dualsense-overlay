# Research probes

Small programs written while finding out how a payload can read the DualSense on the PS5. They are
not part of the overlay and are kept for educational purposes.

| Program | Approach | Result |
|---|---|---|
| `pad_probe` (`main.c`) | Open and read the controller from a standalone payload with `scePadOpen` / `scePadReadState`. | Fails: `scePadOpen` returns `0x809b0081`. The controller belongs to the foreground process. |
| `pad_rpc` (`pad_rpc.c`) | Attach with ptrace to a process that owns the controller and call the pad functions in its context. | Explored as an alternative; superseded by reading inside `SceShellUI`, which needs no ptrace on another process. |

Conclusion: the overlay reads the controller from inside `SceShellUI`, where the renderer already runs
and the controller is always readable.

`pt/` contains the ptrace helpers from [elfldr](https://github.com/ps5-payload-dev/elfldr) by John
Törnblom (GPLv3+), included unmodified.

## Building

Requires the PS5 payload SDK:

```bash
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
make
make test-rpc PS5_HOST=<PS5-IP>
```

The probes write their results to the kernel log and to `/data/pad_probe.log` or `/data/pad_rpc.log`.
