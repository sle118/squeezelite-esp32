# Workstream Board

Last updated: 2026-02-13

Use this table as the live status board for active short-term goals.

Mission critical focus:
- `GOAL-001/WS8` Multi-HUT hardware CI topology (runner lane enablement)

| Goal ID | Workstream | Owner | Status | Blockers | Next Action |
|---|---|---|---|---|---|
| GOAL-001 | WS1: Infrastructure SSH + Codex passthrough | arch:codex | done | none | none (completed) |
| GOAL-001 | WS2: Multi-agent operating model | arch:codex | done | none | none (completed) |
| GOAL-001 | WS3: Infrastructure agent bootstrap | arch:codex | done | none | none (completed) |
| GOAL-001 | WS4: Runner VM provisioning | infra:agent | done | none | Evidence recorded in infra repo (VM-PROV-001). |
| GOAL-001 | WS5: Runner SSH reachability | infra:agent | done | none | Evidence recorded in infra repo (RUNNER-SSH-001). |
| GOAL-001 | WS6: Runner Codex authentication | arch:codex | done | none | Operator completed `codex login` on runner VM; ready to activate `runner:codex` execution |
| GOAL-001 | WS7: Runner agent bootstrap | runner:codex | done | none | Evidence: `/home/runner/workspaces/codex-runner-agent/documentation/evidence/runner_first_heartbeat_20260212_2209_utc.log`, `/home/runner/workspaces/codex-runner-agent/documentation/evidence/runner_smoke_report_20260212_2209_utc.json`; GitLab: `runner/runner-agent@fed6ce9`, `runner/runner-agent@6255908` |
| GOAL-001 | WS8: Multi-HUT hardware CI topology | runner:codex | blocked | MISSION_CRITICAL: runner VM now has >=2 stable `/dev/serial/by-id/*` devices and WS8 closeout commits exist locally (`runner/runner-agent@c03a9a0`), but push/fetch to GitLab is blocked by HTTP auth failure (runner PAT appears invalid or missing `write_repository`) | operator: refresh `runner` PAT with `write_repository` and update runner VM `runner.env`; runner: run `~/.local/bin/gitlab_push_origin_main.sh` to push `main`, then report final pushed SHA and mark WS8 `done` | Runner evidence (local): `/home/runner/workspaces/codex-runner-agent/documentation/evidence/runner_hil_topo_inventory_20260213_215705_utc.log`, `/home/runner/workspaces/codex-runner-agent/documentation/evidence/runner_hil_topo_lock_selftest_20260213_215839_utc.log` |
| GOAL-001 | WS9: Power-cycle control with Home Assistant relay | runner:codex | blocked | DEP:GOAL-001/WS8 pushed to GitLab and slot mapping completed (hut-01 + hut-02 stable) | After WS8 push unblocked: define lock protocol and HA service contract | - |
| GOAL-001 | WS10: Parallel branch/agent workflow | arch:codex | pending | none | Define worktree strategy, artifact routing, and branch policy |
| GOAL-001 | WS11: Long-Run Remote Delegation Grounding | arch:codex | pending | none | Promote remote delegation + credential + transport invocation contract into long-lived docs (`documentation/agents/`) and keep an explicit “arch<->runner link” procedure (no secrets) so future tickets can reliably delegate to `runner:codex` |
| GOAL-001 | WS12: Codex App Server Transport (evaluate -> prototype -> implement) | arch:codex | pending | none | Create `documentation/agents/remote_transport_lock.md` and run WS12 as a gated workflow; once locked, implement the locked mechanism and keep SSH as break-glass fallback unless retired |
| GOAL-002 | WS1: HUT slot inventory + identity | unassigned | blocked | DEP:GOAL-001/WS8 runner topology not yet implemented | Resume after GOAL-001 WS8; run slot inventory on runner/HIL host |
| GOAL-002 | WS2: HW-BOOT-001 execution harness | unassigned | blocked | DEP:GOAL-001/WS8 runner topology not yet implemented | Resume after GOAL-001 WS8; define canonical flash/reboot/monitor flow |
| GOAL-002 | WS3: Per-slot surfacing runs | unassigned | blocked | DEP:GOAL-001/WS8 runner topology not yet implemented | Resume after GOAL-001 WS8; execute per-slot HW-BOOT-001 |
| GOAL-002 | WS4: Worklist + matrix sync | unassigned | blocked | DEP:GOAL-001/WS8 runner topology not yet implemented | Resume after GOAL-001 WS8; sync worklist/matrix with slot outcomes |
| GOAL-003 | WS1: Upstream docs lint gate | unassigned | pending | none | Define required markdown lint scope and failure policy |
| GOAL-003 | WS2: Local agent gardening loop | unassigned | pending | none | Define agent task contract for docs maintenance runs |
| GOAL-003 | WS3: Progressive doc discovery | unassigned | pending | none | Ensure agents can discover context without large upfront reads |
| GOAL-003 | WS4: Lint evidence + handoff format | unassigned | pending | none | Standardize report fields for lint before/after and residuals |

## Status Values

- `pending`: not started
- `in_progress`: actively being worked
- `blocked`: waiting on dependency or decision
- `done`: completed and verified for that workstream (does not by itself mean the parent goal is complete)

## Owner Governance

- Owner format: `<role>:<agent-id>` (examples: `arch:codex`,
  `infra:tbd`, `runner:tbd`).
- Role keys:
  - `arch`: Architecture Agent (engineering/control/governance)
  - `infra`: Infrastructure Agent (host/container/VM operations)
  - `runner`: Runner Agent (VM CI/CD + test execution)
- Governance rules:
  - Owner values are execution semantics:
    - `<role>:codex` means a Codex runtime is installed/authenticated in
      that role environment and is being prompted there.
    - `<role>:agent` means the `arch:codex` control-plane is acting as a
      temporary proxy executor for that role (typically via SSH
      transport). This is allowed only for bootstrap/emergency and must
      be called out in handoff summaries as `executor=ssh_direct`.
  - `arch` delegates work; SSH may be used as a transport layer, but
    direct SSH command execution must not be conflated with prompting a
    remote Codex runtime.
  - Ownership implies closure accountability:
    - when `arch:codex` is the owner (or is explicitly asked to "take
      ownership"), `arch` must drive the workstream to closure:
      - `done` with acceptance met and evidence recorded, or
      - `blocked` with concrete blockers, correct owners, operator gate,
        and explicit next action(s)
    - "prepared a packet" is not closure; packets are inputs to closure
  - Commit hygiene (all roles):
    - each role Codex runtime (`arch`, `infra`, `runner`) is responsible
      for commit hygiene in its own role repository
    - make small commits frequently and push so progress is traceable by
      commit SHA
    - include commit SHA(s) in handoff summaries for executed work
    - avoid batching unrelated changes
  - `arch` is accountable for coordination quality and status reporting.
  - `arch` should continuously improve agent efficiency and governance
    quality when requested or when serious inefficiencies are observed.
  - bootstrap scaffolding by `arch` for a remote role repository does
    not mean that remote role is active; activation requires explicit
    owner assignment and first role-context handoff entry.
  - `infra` must never clone `squeezelite-esp32`.
  - `infra` does not directly communicate with `runner` except when only
    local host commands can reach `runner`.
  - each remote agent role uses a dedicated repository with its own
    AGENTS/board/handoff artifacts.
  - evidence paths should use role prefixes for quick origin discovery:
    `arch_*`, `infra_*`, `runner_*`.
  - a remote-role workstream must not be set to `in_progress` while
    owner is `infra:tbd` or `runner:tbd`.

## Role Activation State Machine

- States:
  - `unassigned`
  - `assigned`
  - `first_heartbeat`
  - `active`
- Transition rules:
  - `unassigned` -> `assigned`: concrete owner set (`infra:<id>` or
    `runner:<id>`)
  - `assigned` -> `first_heartbeat`: first remote heartbeat evidence
    captured
  - `first_heartbeat` -> `active`: first role-context handoff entry
    logged
- Enforcement:
  - remote execution workstreams require role state `>= assigned`
  - destructive or stateful remote changes require role state
    `>= first_heartbeat`
  - continuous delegated execution requires role state `active`

## Change Intake (Prereq + Priority + Intermediate Quest)

When operator/user input indicates plan change (for example: \"we
missed...\", \"easier way...\", \"we should prioritize...\"), treat it as
a formal replan event even if it is not a hard prerequisite.

Required board updates:

1. Set impacted workstreams to `blocked` or `pending` as appropriate.
2. Add new prerequisite/intermediate-quest workstreams when needed.
3. Update blockers/dependencies and next actions to match new sequence.
4. Ensure owner + activation state are still valid for the new plan.
5. Require a matching `action_type=replan` handoff entry.

## Ad-hoc Ticket Reflex

When user/operator gives an ad-hoc request, agents should recommend
ticket tracking for continuity and let user decide.

Rules:
1. Ask user: `Track this as a ticket? (yes/no)`.
2. If `yes`, add/update ticket in:
   - `documentation/short-term/coordination/ad_hoc_ticket_queue.md`
3. If `no`, continue and note `ticket_tracking=declined` in handoff.
4. Default recommendation is `yes` when request spans sessions, roles,
   or priority changes.

## Role Activation Tracker

| Role | Owner | Activation State | Evidence | Notes |
|---|---|---|---|---|
| `infra` | `infra:agent` | `active` | `/workspaces/codex-infra-agent/documentation/evidence/infra_first_heartbeat_20260212_2044_utc.log` | executor=ssh_direct; WS4/WS5 completed with evidence logs under `/workspaces/codex-infra-agent/documentation/evidence/` |
| `runner` | `runner:codex` | `active` | `/home/runner/workspaces/codex-runner-agent/documentation/evidence/runner_first_heartbeat_20260212_2209_utc.log` | executor=remote_codex; runner repo bootstrap + first handoff entry captured |
