# Testing Charter

Date: 2026-02-12  
Scope: `squeezelite-esp32` test design and review policy.

## Objective

Keep tests stable across refactors by validating behavior contracts and invariants, not implementation details.

## Grounding Rule

Tests must verify behavior that is expected to remain true after refactoring.

Tests should be rejected in review if they mainly assert internals that can change without changing user-visible or system-visible behavior.

## Contract-First Principles

1. Test module boundaries
- public function behavior
- state machine transitions
- error semantics
- resource/timing bounds where relevant

2. Prefer invariants over internals
- "must never crash on malformed input"
- "illegal state transition is rejected"
- "retry is bounded"
- "idempotent operation remains safe when repeated"

3. Avoid implementation coupling
- do not assert private helper call order unless behavior-critical
- do not assert exact logs/strings unless contractually required
- do not assert private data layout

4. Fixes require regression tests
- every production bug fix should add a regression test at the highest stable boundary that catches it

5. Embedded + host split
- host-native tests for deterministic logic
- hardware/HIL tests for timing, drivers, memory layout, OTA/recovery, power fault behavior

## Review Gate

Reviewers should ask:

1. What contract does this test protect?
2. Would this test still pass after a clean internal refactor with identical behavior?
3. Does this test provide failure signal that matters to users/platform stability?

If answers are weak, request a contract-level test instead.

## Minimum PR Expectations

- New behavior: at least one contract-level test
- Bug fix: at least one regression test
- Refactor-only PR: existing contract tests still pass, no new implementation-coupled tests added

## Mapping to Hardware Matrix

Use this charter with:
- `documentation/HARDWARE_TEST_MATRIX.md`

The charter governs *how* tests are written.  
The matrix governs *where/what* is validated per hardware platform.
