# Agent Documentation

Store long-form guidance for coding agents here.

## Suggested Files

- `documentation/agents/architecture.md`: subsystem boundaries, data flow, invariants.
- `documentation/agents/build-and-test.md`: canonical commands, fast checks, CI mapping.
- `documentation/agents/style-and-lint.md`: formatting policy, lint severity, suppression rules.
- `documentation/agents/refactor-playbook.md`: safe refactor steps, rollout strategy, risk controls.
- `documentation/agents/frontend_requirements_context.md`: requirements-first UI context, size budgets, and migration constraints.
- `documentation/agents/module-notes/<module>.md`: module-level constraints and edge cases.

## Writing Rules

- Keep files operational and concrete.
- Prefer checklists and explicit commands.
- Record known pitfalls and non-obvious invariants.
- Link back from `AGENTS.md` only to docs that are actively maintained.
- When frontend payload/routes/contracts change, refresh the snapshot with `build-scripts/ui_footprint_snapshot.sh` and update `frontend_requirements_context.md`.
