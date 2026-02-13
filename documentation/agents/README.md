# Agent Documentation

Use this folder for operational docs that agents can discover progressively.

## Start Path (minimal context first)

1. Read `documentation/agents/start_here.md`.
2. Load exactly one route document based on task type.
3. Load additional documents only when the active route requires them.

## Route Documents

- `documentation/agents/ci_lane_contract.md`
  - lane boundary and SHA handoff between upstream and local HIL
- `documentation/agents/integration_test_worklist.md`
  - integration/hardware execution tracker and handoff protocol
- `documentation/agents/document_gardening.md`
  - docs lint and docs maintenance workflow for CI/LXD sessions
- `documentation/agents/frontend_requirements_context.md`
  - requirements-first UI/API context and update contract

## Short-Term Execution Docs

- `documentation/short-term/active/*.md`
  - active, time-boxed goals
- `documentation/short-term/active/GOAL_TEMPLATE.md`
  - standardized template for creating new goals
- `documentation/short-term/coordination/workstream_board.md`
  - goal/workstream owner and status board (role-based owner governance)
- `documentation/short-term/coordination/handoff_log.md`
  - session handoff trail
- `documentation/short-term/coordination/ad_hoc_ticket_queue.md`
  - user-approved ad-hoc ticket tracking queue

## Writing Rules

- Keep files operational and concrete.
- Prefer checklists and explicit commands.
- Record known pitfalls and non-obvious invariants.
- Keep documents pointer-heavy so agents can discover context on demand.
- Link from `AGENTS.md` only to maintained docs.
- Archive completed short-term goals out of active rotation.
- Do not mark a goal complete or archive it while its deliverables
  checklist has unchecked items.
