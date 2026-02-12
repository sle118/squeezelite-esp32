# Contract Test Template

Use this template when adding/refactoring tests to ensure they are refactor-resilient.

## Metadata

- Test ID:
- Module/Boundary:
- Priority (`P0`/`P1`/`P2`):
- Platform scope (`host`, `all`, `i2s`, `muse`, `squeezeamp`, etc.):

## Contract

Describe what externally visible behavior must remain true.

## Invariant(s)

List invariant properties this test protects.

## Inputs/Preconditions

- Required setup:
- Input variants:
- Fault conditions (if any):

## Expected Behavior

- Success conditions:
- Error conditions:
- Bounds/time/resource expectations:

## Non-goals

State what this test intentionally does not verify (to avoid implementation coupling).

## Refactor Resilience Check

- Would this still pass if internals were reorganized but behavior unchanged? (`yes`/`no`)
- If `no`, explain why coupling is unavoidable.

## Regression Linkage

- Related issue/bug (if regression test):
- Why this boundary was chosen:
