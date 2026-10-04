## Summary

<!-- What does this change and why? Link the related issue if there is one. -->

## Testing

<!-- Console model, system software, homebrew enabler and game used, or "host only". -->

## Checklist

- [ ] `scripts/ps5_source_build.sh` builds and `tools/verify_release.py` passes.
- [ ] Host tests and `tests/test_plugin_wrapper.py` pass.
- [ ] Existing copyright notices are untouched; changed files carry a `Modifications Copyright` line.
- [ ] The renderer does not access `/data` and nothing reads or writes game memory.
- [ ] `CHANGELOG.md` is updated.
- [ ] Commits are signed off (`git commit -s`).
