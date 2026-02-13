# GOAL-XXX: <Short Goal Title>

Status: active  
Owner: arch:<agent-id|tbd>  
Last updated: YYYY-MM-DD

## Objective

<One paragraph describing the intended outcome and constraints.>

## Scope

- In scope:
  - <item>
- Out of scope:
  - <item>

## Follow-On Goals (optional)

- `documentation/short-term/active/GOAL-YYY-<slug>.md`

## Workstreams

| Workstream | Outcome | Owner | Status | Notes |
|---|---|---|---|---|
| WS1: <name> | <outcome> | arch:<agent-id|tbd> | pending | <notes> |
| WS2: <name> | <outcome> | infra:<agent-id|tbd> | pending | <notes> |

Status values: `pending`, `in_progress`, `blocked`, `done`.

Owner format:
- `<role>:<agent-id>`
- roles: `arch`, `infra`, `runner`

Role activation states:
- `unassigned` -> `assigned` -> `first_heartbeat` -> `active`

## Deliverables Checklist (Goal Completion Gate)

- [ ] <deliverable 1 with objective evidence target>
- [ ] <deliverable 2 with objective evidence target>
- [ ] <deliverable 3 with objective evidence target>

Goal completion rule:
- A goal is complete only when every deliverable above is checked and
  evidence is recorded in coordination artifacts.
- Workstream `done` does not by itself mean goal complete.
- Do not archive until all deliverables are checked.

## Agent Governance

- `arch` owns engineering coordination, status governance, and
  delegation to remote agents; it should also propose role/process
  improvements when asked or when major inefficiencies are evident.
- governance docs should be enabling and operational, not restrictive.
- `infra` owns host/container/VM maintenance and must never clone the
  product repository.
- `runner` owns VM CI/CD and test execution.
- `infra` must not directly communicate with `runner` except when only
  local infra commands can reach runner execution context.
- each remote role (`infra`, `runner`) requires its own dedicated
  repository and AGENT artifacts.

## Delegation Checklist (Required For Remote Roles)

- [ ] Owner assigned (`infra:<id>` or `runner:<id>`, not `tbd`)
- [ ] Role activation state is at least `assigned`
- [ ] First role-context handoff entry logged
- [ ] Operator gate decision recorded (`operator_required=yes|no`)
- [ ] Execution mechanism recorded (`executor=remote_codex|ssh_direct`)
- [ ] Commit hygiene plan stated (expected commit cadence + push points)

## Course-Correction Checklist (Required For Reprioritization)

Use this when scope/sequence changes even without hard prerequisites.

- [ ] Replan request captured in handoff (`action_type=replan`)
- [ ] Impacted workstreams restated (`blocked`/`pending` updates)
- [ ] New intermediate quest workstream(s) added where useful
- [ ] Dependencies and next actions updated in board
- [ ] Deliverables checklist still matches revised path

## Ad-hoc Request Intake

- Ask user: `Track this as a ticket? (yes/no)`
- If `yes`: create/update ticket in
  `documentation/short-term/coordination/ad_hoc_ticket_queue.md`
- If `no`: proceed and log `ticket_tracking=declined` in handoff summary

## Acceptance By Workstream

### WS1: <name>

Tasks:
1. <task>
2. <task>

Acceptance:
- <acceptance criterion>

### WS2: <name>

Tasks:
1. <task>
2. <task>

Acceptance:
- <acceptance criterion>

## Evidence

- Commands:
  - `<command>`
- Logs/artifacts:
  - `<path>` (prefix with `arch_`, `infra_`, or `runner_`)
- Related coordination updates:
  - `documentation/short-term/coordination/workstream_board.md`
  - `documentation/short-term/coordination/handoff_log.md`

## Handoff Entry Format

```text
YYYY-MM-DD HH:MM UTC | agent/session | goal/workstream | context | action_type | operator_required | summary | next action | blocker (optional)
```
