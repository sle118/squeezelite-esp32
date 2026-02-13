# Agent Start Here

Use this file to avoid loading large context up front.

## 90-Second Startup

1. Read `AGENTS.md` for repository policy.
2. Read `documentation/agents/ci_lane_contract.md` to select lane:
   - upstream GitHub lane (no hardware)
   - local LXD lane (hardware/HIL)
3. If the request requires remote-role work (`infra`/`runner`), read:
   - `documentation/agents/remote_delegation_contract.md` (handshake +
     `thread_ref` correlation)
4. If the request references a short-term goal, read:
   - `documentation/short-term/coordination/workstream_board.md`
   - the referenced file under `documentation/short-term/active/`
   - `documentation/short-term/coordination/handoff_log.md`
   - owner-governance rules in the board (`Owner Governance` section)
   - role activation tracker/state in the board
5. Read one task route doc only:
   - hardware/integration execution:
     `documentation/agents/integration_test_worklist.md`
   - documentation linting/gardening:
     `documentation/agents/document_gardening.md`
   - frontend payload/route/API changes:
     `documentation/agents/frontend_requirements_context.md`

## Load-On-Demand Rule

- Start with the smallest possible doc set.
- Pull in more docs only when the current task hits a dependency.
- Keep updates local to the active goal/workstream.

## Session Close-Out

1. Update active goal/workstream status if changed.
2. Add evidence pointers (logs, run IDs, artifacts, commands).
3. Add a handoff line in
   `documentation/short-term/coordination/handoff_log.md` when work remains.
4. If you are the owner (especially `arch`) and work is not `done`,
   ensure the workstream is either:
   - `blocked` with concrete blockers + owners + operator gate + next
     action, or
   - explicitly delegated with a `thread_ref` and a scheduled follow-up
     check (do not leave work as "implicitly pending")
   - if stopping due to a required pivot or a stalled loop, log
     `action_type=replan` (pivot) or `blocked` (stall) with the new
     decision/unknowns captured
5. If closing a goal: verify all deliverables checklist items in the goal
   file are checked. If any are unchecked, keep goal status active and do
   not archive.
6. For remote-role work:
   - do not set `in_progress` when owner is `tbd`
   - include `context`, `action_type`, and `operator_required` fields in
     handoff entries
   - update role activation tracker (`unassigned`/`assigned`/
     `first_heartbeat`/`active`) when state changes
7. For user/operator course-correction signals (for example: \"we
   missed...\", \"easier way...\", \"we should prioritize...\"):
   - treat as formal `action_type=replan`
   - update goal workstreams + board dependencies before resuming
     execution
8. For ad-hoc user requests:
   - proactively ask if user wants ticket tracking (`yes`/`no`)
   - if `yes`, create/update
     `documentation/short-term/coordination/ad_hoc_ticket_queue.md`
   - if `no`, proceed and log `ticket_tracking=declined` in handoff
