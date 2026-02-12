# AGENTS.md

This file is the default startup brief for Codex sessions in this repository.
Keep it short, directive, and high-signal.

## Mission

- Preserve platform behavior and stability.
- Prefer small, reversible changes over broad rewrites.
- Keep behavior unchanged unless a request explicitly asks for behavior changes.

## Working Scope

- Primary source code: `components/`, `main/`, `test/`.
- Build and release scripts: `build-scripts/`, `docker/`, `.github/workflows/`.
- Documentation: `documentation/`.

## Engineering Priorities

1. Correctness and safety.
2. Backward-compatible behavior.
3. Maintainability and clarity.
4. Build/test reproducibility.
5. Performance and footprint optimization.

Optimize in this order unless the task explicitly changes priorities.

## Do Not Edit By Default

- Vendor/external code:
  - `components/esp-dsp/`
  - `components/spotify/cspot/`
  - `components/telnet/libtelnet/`
  - `managed_components/`
- Build outputs and generated artifacts:
  - `build/`
  - `components/wifi-manager/webapp/dist/`

Only modify these paths when the task explicitly requires it.

## Behavioral Invariants

- Treat OTA/recovery reliability as a product-level contract.
- Treat partition layout as a fixed contract unless explicitly requested otherwise.
- Do not introduce platform-specific compile-time branches when runtime/config solutions are sufficient.
- Avoid unrelated churn; keep patches tightly scoped to the request.

## Standard Workflow

1. Read the request and identify touched modules and contracts.
2. Inspect current code and existing patterns in those modules.
3. Implement the smallest coherent patch that preserves contracts.
4. Run targeted validation commands.
5. Report changes, validation evidence, and residual risks.

## Validation Commands

- Style check: `build-scripts/lint_style.sh check`
- Style normalize: `build-scripts/lint_style.sh format`
- Firmware build (example): `idf.py build`
- Recovery footprint (platform-specific): `build-scripts/build_recovery_size.sh <platform|defaults-file> [build-dir]`

Use targeted checks first; avoid full-matrix builds unless requested.

## Testing Rule (Grounding)

- Tests should validate behavior contracts and invariants, not implementation details.
- Prefer module-boundary and regression tests over private-internal assertions.
- Every bug fix should include a regression test at the highest stable boundary possible.
- Use:
  - `documentation/TESTING_CHARTER.md`
  - `documentation/CONTRACT_TEST_TEMPLATE.md`
  - `documentation/HARDWARE_TEST_MATRIX.md`

## Style Baseline

- Use repository formatting/lint settings as the source of truth.
- Do not introduce style-only churn outside task scope unless requested.
- During refactors, normalization is acceptable when intentionally planned.

## Frontend Context (Required)

- For any UI/API work involving `components/wifi-manager/webapp/` or `components/wifi-manager/http_server_handlers.c`, read:
  - `documentation/agents/frontend_requirements_context.md`
- Treat that file as requirements-first context and keep it current.
- When frontend payload, routes, or protobuf contract changes, run:
  - `build-scripts/ui_footprint_snapshot.sh`
- Then update `documentation/agents/frontend_requirements_context.md` in the same change.

## Agent Docs Layout

- Keep this file concise and policy-focused.
- Put detailed playbooks under `documentation/agents/`.
- Add module-specific guidance in local `AGENTS.md` files when needed (closest file to changed code takes precedence).
