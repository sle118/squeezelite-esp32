# AGENTS.md (Web UI)

Local guidance for work under `components/wifi-manager/webapp/`.

## Required Context

- Read `documentation/agents/frontend_requirements_context.md` before making UI/API changes.
- Treat that document as the active requirements context (not just historical notes).

## Scope Rules

- Prioritize behavior and contract clarity over framework churn.
- Do not edit `dist/` files manually.
- Keep generated protobuf artifacts and API usage aligned with the active request/response contract.

## Maintenance Rules

- If routes, payload contracts, or shipped web asset sizes change:
  1. Run `build-scripts/ui_footprint_snapshot.sh`.
  2. Update `documentation/agents/frontend_requirements_context.md`.
  3. Add/update the decision log entry in that file.
