## Summary

- What changed:
- Why it changed:

## Test Strategy Check (Required)

- [ ] I validated behavior at contract/module boundaries (not private implementation details).
- [ ] New tests are refactor-resilient (should survive internal reorganization with identical behavior).
- [ ] For bug fixes, I added at least one regression test at the highest stable boundary.
- [ ] I avoided brittle assertions on internals (private helper call order, private layout, incidental log text), unless behavior-critical.

## Validation Performed

- [ ] Host/unit tests
- [ ] Hardware/HIL tests (if applicable)
- [ ] Platform profile(s) validated: `i2s` / `muse` / `squeezeamp` / N/A

Commands and results:
```bash
# paste exact commands used
```

## Risk and Rollback

- Risk level: low / medium / high
- Potential regressions:
- Rollback plan:

## Related Docs

- [ ] `documentation/TESTING_CHARTER.md` reviewed
- [ ] `documentation/HARDWARE_TEST_MATRIX.md` reviewed (if platform impact)
