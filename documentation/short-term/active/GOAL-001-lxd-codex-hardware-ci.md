# GOAL-001: LXD Codex Orchestration + Multi-Hardware CI

Status: active  
Owner: arch:codex  
Last updated: 2026-02-12

## Objective

Establish a durable workflow where:
1. the Architecture Agent (`arch`) in this repository remains the
   code-authoring and coordination control plane,
2. the Architecture Agent can drive an Infrastructure Agent (`infra`)
   for host/container/VM maintenance,
3. the Architecture Agent can drive a Runner Agent (`runner`) inside
   the provisioned VM for CI/CD and hardware execution,
4. the LXD VM becomes the operational base for hardware-aware CI, and
5. multiple hardware-under-test (HUT) nodes can be flashed/tested in
   parallel with safe, locked power control.

## Follow-On Goals

- `documentation/short-term/active/GOAL-002-hut-surface-first-test.md`
  for first cross-slot HUT surfacing execution.
- `documentation/short-term/active/GOAL-003-agent-doc-lint-ci.md`
  for documentation linting and gardening flow in CI/CD.

## CI Environment Split (required contract)

There are two CI lanes and they must remain distinct:

1. Upstream GitHub lane (no physical hardware):
   - compile, package, static checks, unit/integration tests that do not require physical devices
   - never assume serial ports, relay access, or USB-attached targets
2. Local LXD hardware lane (physical HUT available):
   - flash/monitor, hardware integration tests, soak runs, power-cycle recovery flows

Seamless operation requirement:
- hardware lane consumes the same commit SHA/artifacts produced by upstream lane (or rebuilds deterministically from same SHA),
- result reporting flows back to shared status artifacts and short-term board,
- no hidden environment-only behavior differences outside explicitly documented hardware dependencies.

Note:
- CI lanes define where tests run.
- Agent roles define which Codex agent controls which environment.
- These are orthogonal and must not be conflated.

## Agent Roles (required contract)

Short names (use these in board ownership and reports):

1. `arch`: Architecture Agent (main engineering/control plane)
2. `infra`: Infrastructure Agent (platform/host/VM operations)
3. `runner`: Runner Agent (VM CI/CD + test execution)

Role responsibilities:

1. `arch`
   - owns product-repo development, task coordination, and status
     reporting governance
   - delegates remote work to role-local Codex runtimes when available
     (`infra:codex`, `runner:codex`); SSH is treated as a transport
     layer, not as the execution model
   - mentors and improves `infra`/`runner` operating efficiency over
     time; must propose improvements when asked or when major
     inefficiencies degrade team throughput
   - governs documentation quality, tracking hygiene, and baseline
     environment decisions
   - keeps governance documentation enabling and operational, not
     artificially restrictive
2. `infra`
   - manages host/container/VM provisioning and maintenance
   - never clones or edits this product repository
   - maintains its own repository and AGENT documentation
   - does not communicate directly with `runner` except when only local
     host commands can reach the VM
3. `runner`
   - executes CI/CD and test workflows inside VM scope
   - reports structured artifacts/status back to `arch`
   - maintains its own repository and AGENT documentation

## Owner Semantics (Prevent Drift)

Owner values encode where execution happens:

1. `<role>:codex`
   - Codex is installed/authenticated in that role environment.
   - Coordination means prompting that remote Codex runtime to execute
     work in its role repository (not SSH-direct command execution).
2. `<role>:agent`
   - The `arch:codex` control-plane is acting as a temporary proxy
     executor for that role (typically via SSH transport).
   - Allowed only for bootstrap/emergency.
   - Must be called out in handoff summaries as `executor=ssh_direct`.

## Repository Boundary Contract (required target)

1. Product repository (`squeezelite-esp32`) for `arch`.
2. Infrastructure repository for `infra`.
3. Runner orchestration repository for `runner`.

Required behavior:
- each role has a dedicated repository
- each remote role (`infra`, `runner`) has independent `AGENTS.md`,
  workstream board, and handoff log
- strict execution boundary:
  - `arch` must never implement `runner` workstreams by modifying or
    running commands inside the runner repository on the development
    machine (example local path: `/workspaces/codex-runner-agent`)
  - `runner` workstreams are executed only by `runner:codex` inside the
    runner VM, with results reported back via runner evidence + commit SHA
- cross-agent handoffs include commit SHA, agent role, status, and
  artifact/log pointers
- optional local GitLab can be used as shared upstream for portability
  and document access when the operator enables it

## Operator Touchpoints

1. Operator assistance is explicitly expected for:
   - runner Codex interactive authentication
   - optional local GitLab setup/onboarding for agent repos
2. `arch` should request operator help when these gates are reached
   instead of introducing brittle workarounds.

## GitLab Credential Contract (ADHOC-20260212-02)

This contract standardizes secret location, auth mode, and verification
for agent-repo bootstrap on `git.lecsys.net`.

Canonical secret root:
- `${XDG_CONFIG_HOME:-$HOME/.config}/codex/credentials/gitlab/git.lecsys.net/`

Expected role files:
- `codex.env`, `arch.env`, `infra.env`, `runner.env`

Minimum variables per role file:
- `GITLAB_HOST`
- `GITLAB_USER`
- `GITLAB_PASSWORD` (bootstrap-only)
- `GITLAB_PAT` (required for non-interactive flows)

Required controls:
1. permissions: root `0700`, files `0600`
2. auth mode: PAT over HTTPS for automation; password only for bootstrap
3. propagation: transfer role-local file only to its target runtime
4. evidence hygiene: record command outcome, never raw secret values

## Agent Repo Remote Bootstrap (ADHOC-20260212-03)

Bootstrapped remotes and default branch:
- `infra` repo: `http://git.lecsys.net/infra/infra-agent.git` (`main`)
- `runner` repo: `http://git.lecsys.net/runner/runner-agent.git` (`main`)

Bootstrap checkout contract:
1. clone role repo to its canonical workspace path
2. checkout `main`
3. verify `origin` remote URL and branch tracking
4. verify remote reachability with `git ls-remote`

Evidence locations:
- `/workspaces/codex-infra-agent/README.md`
- `/workspaces/codex-runner-agent/README.md`
- `documentation/short-term/coordination/handoff_log.md`

## Container Agent Pickup (first 15 minutes)

1. Read:
   - `documentation/short-term/coordination/workstream_board.md`
   - `documentation/short-term/coordination/handoff_log.md`
2. Claim one workstream by updating board owner/status.
3. Execute only the claimed workstream scope.
4. Log handoff with concrete next step before stopping.

## Workstreams

- WS12: Codex App Server Transport (evaluate -> prototype -> implement)
  - Objective: evaluate Codex App Server as a more structured transport
    than SSH for prompting `runner:codex` / `infra:codex`, especially
    for parallel multi-step threads and stable context reuse.
  - Mechanism lock (required):
    - WS12 must produce and maintain a single transport decision record
      in long-lived docs:
      - `documentation/agents/remote_transport_lock.md`
    - Once `remote_transport_lock.md` sets `status: locked`, follow-on
      WS12 work must implement that locked mechanism (no re-litigating
      transport per session).
    - SSH remains the break-glass fallback unless explicitly retired in
      the lock record.
  - Acceptance (v1):
    - Evaluation:
      - documented transport decision criteria and current status
        (evaluating/locked) in `remote_transport_lock.md`
    - Prototype:
      - a minimal proof-of-viability plan that can be executed by the
        appropriate role(s) without `arch` becoming the executor
    - Implementation:
      - documented invocation pattern(s) (no secrets) that preserve the
        repo boundary contract and keep `thread_ref` correlation usable
        across sessions and in parallel

## Delegation Gate (required before remote-role execution)

Before executing any `infra` or `runner` workstream:

1. assign concrete owner (`infra:<id>` or `runner:<id>`)
2. verify role activation state is at least `assigned`
3. log first role-context handoff entry
4. record operator gate for the action:
   - `operator_required=yes` or `operator_required=no`
5. record execution mechanism in the handoff summary:
   - `executor=remote_codex` (prompt remote Codex runtime), or
   - `executor=ssh_direct` (bootstrap/emergency only)

Handoff lines should include:
- `context`: `arch-local` | `infra-live` | `runner-live`
- `action_type`: `scaffold` | `delegate` | `execute` | `replan`
- `operator_required`: `yes` | `no`

Ad-hoc request reflex:
- ask user: `Track this as a ticket? (yes/no)`
- if `yes`, create/update
  `documentation/short-term/coordination/ad_hoc_ticket_queue.md`
- if `no`, continue and include `ticket_tracking=declined` in handoff
  summary

Evidence naming convention:
- `arch_*`, `infra_*`, `runner_*`

## WS1: Infrastructure SSH + Codex passthrough

### Outcome

`arch` can reliably trigger/drive commands on the `infra` runtime over
SSH.

### Tasks

1. Validate key-based SSH path and non-interactive command execution.
2. Standardize connection variables in `.lxd.env` (host/user/key/path).
3. Create a minimal remote control wrapper script (local side) for repeatable calls.
4. Add heartbeat check command (`hostname`, `uptime`, `whoami`, disk availability).

### Acceptance

- One command from `arch` context can run remote shell on `infra`
  without manual interaction.
- Failures return non-zero status and readable diagnostics.

## WS2: Multi-agent operating model

### Outcome

`arch` can delegate infrastructure operations to `infra` and CI/CD
execution to `runner`, then collect structured status back without role
ambiguity.

### Tasks

1. Define role-responsibility contract for `arch`/`infra`/`runner`
   including explicit no-product-repo boundary for `infra`.
2. Define contract for remote job invocation:
   - target role (`infra` or `runner`)
   - job id
   - working directory
   - command bundle
   - expected artifacts/log paths
3. Define report format (single machine-readable summary + human note)
   including `agent_role`, `commit_sha`, and `artifact_paths`.
4. Add guardrails:
   - one remote job per lock file
   - timeout policy
   - cancellation behavior
5. Define minimum required files for each agent-role repository:
   - `AGENTS.md` with startup path and discovery-first checks
   - workstream board/handoff format for cross-session continuation
6. Smoke test one `infra` maintenance command through
   `build-scripts/lxd_remote.sh` and log evidence.

### Acceptance

- Remote execution contract documents all three agent roles and their
  boundaries.
- `infra` instructions explicitly avoid assuming existing host/VM
  resources.
- At least one `infra` smoke task is executed with evidence.

### WS2 Contract Draft (v1)

#### Role Responsibility Contract

1. `arch` (Architecture Agent, this repo):
   - prepares job request and selects target role
   - may edit product code
   - collects reports and updates coordination docs
2. `infra` (Infrastructure Agent):
   - manages host/VM lifecycle and host maintenance only
   - must not edit product code repositories directly
   - must run discovery-first checks before mutating host state
3. `runner` (Runner Agent inside VM):
   - executes CI/CD and hardware workflows in VM scope
   - may operate on product repo clones/worktrees inside VM
   - does not own host virtualization lifecycle

#### Invocation Schema (machine-readable)

```json
{
  "schema_version": "ws2.invocation.v1",
  "job_id": "GOAL-001-WS2-0001",
  "target_role": "infra",
  "requested_by": "arch",
  "request_utc": "2026-02-12T15:30:00Z",
  "commit_sha": "<git sha or none>",
  "repo": "squeezelite-esp32",
  "workdir": "/home/codexsvc",
  "command": [
    "hostname",
    "whoami",
    "uptime"
  ],
  "timeout_sec": 600,
  "lock_key": "goal001-ws2-smoke",
  "expected_artifacts": [
    "test/build/log/lxd_ws2_smoke_YYYYMMDD.log"
  ],
  "notes": "WS2 role routing smoke test"
}
```

Required fields:
- `schema_version`, `job_id`, `target_role`, `requested_by`,
  `request_utc`, `command`, `timeout_sec`, `lock_key`.

Allowed `target_role` values:
- `infra`
- `runner`

#### Report Schema (machine-readable + human note)

```json
{
  "schema_version": "ws2.report.v1",
  "job_id": "GOAL-001-WS2-0001",
  "agent_role": "infra",
  "status": "success",
  "start_utc": "2026-02-12T15:30:10Z",
  "end_utc": "2026-02-12T15:30:12Z",
  "duration_sec": 2,
  "exit_code": 0,
  "lock_key": "goal001-ws2-smoke",
  "executor": {
    "hostname": "hpi5-2",
    "user": "codexsvc"
  },
  "commit_sha": "<git sha or none>",
  "artifact_paths": [
    "test/build/log/lxd_ws2_smoke_YYYYMMDD.log"
  ],
  "human_summary": "Infra-role smoke command completed successfully.",
  "next_action": "Proceed with role-specific AGENT repo skeletons."
}
```

Required fields:
- `schema_version`, `job_id`, `agent_role`, `status`, `start_utc`,
  `end_utc`, `duration_sec`, `exit_code`, `lock_key`, `human_summary`.

Allowed `status` values:
- `success`
- `failed`
- `timeout`
- `cancelled`

#### Guardrail Defaults

1. Locking:
   - lock file path pattern:
     `/tmp/codex-<target_role>-<lock_key>.lock`
   - only one active job per `(target_role, lock_key)` tuple
2. Timeout defaults:
   - `infra`: 600 seconds default, 3600 seconds max
   - `runner`: 1800 seconds default, 14400 seconds max
3. Cancellation behavior:
   - cancellation request writes
     `/tmp/codex-cancel-<job_id>.flag`
   - executor checks cancellation between command steps and returns
     status `cancelled`
4. Non-zero exits:
   - any non-zero command exit returns report `status=failed`
   - partial outputs are still published in `artifact_paths`

#### Minimum Files For Each Agent-Role Repository

1. `AGENTS.md`
   - startup read order
   - role boundaries
   - discovery-first checks
2. `documentation/coordination/workstream_board.md`
   - owner/status/blocker/next action table
3. `documentation/coordination/handoff_log.md`
   - timestamped handoff lines with evidence pointers
4. `scripts/`
   - executable wrappers for invocation, reporting, and locks

#### WS2 Evidence

- Smoke command log:
  - `test/build/log/lxd_ws2_smoke_20260212.log`
- Structured report:
  - `test/build/log/lxd_ws2_report_20260212.json`

## WS3: Infrastructure agent bootstrap

### Outcome

`infra` has a dedicated repository, baseline `AGENTS.md`, and
coordination artifacts so host/VM operations can run independently from
this product repository.

### Tasks

1. Create/initialize the `infra` repository.
2. Add role-specific `AGENTS.md` with discovery-first startup checks.
3. Add `workstream_board.md` and `handoff_log.md` in the infra repo.
4. Document explicit boundary: `infra` must never clone
   `squeezelite-esp32`.

### Acceptance

- Infra repo exists with required files and startup path.
- Role boundary rules are documented and testable.

### WS3 Evidence

- Infra repository path:
  - `/workspaces/codex-infra-agent`
- Infra governance files:
  - `/workspaces/codex-infra-agent/AGENTS.md`
  - `/workspaces/codex-infra-agent/documentation/coordination/workstream_board.md`
  - `/workspaces/codex-infra-agent/documentation/coordination/handoff_log.md`

## WS4: Runner VM provisioning

### Outcome

`infra` can provision the runner VM with deterministic naming and
baseline dependencies.

### Tasks

1. Provision VM instance for `runner` workloads.
2. Record VM identity (name/IP/image/resources) in infra tracking docs.
3. Install baseline packages required for remote management.

### Acceptance

- VM is created and reachable from host.
- Provisioning commands and outputs are captured in infra evidence logs.

## WS5: Runner SSH reachability

### Outcome

`arch` can reach the runner VM over SSH using non-interactive key auth.

### Tasks

1. Configure SSH service/user/key on runner VM.
2. Validate non-interactive SSH from `arch` context to runner VM.
3. Capture heartbeat evidence (`hostname`, `whoami`, `uptime`, disk).

### Acceptance

- Non-interactive SSH to runner VM succeeds from `arch`.
- Failed SSH attempts return clear non-zero diagnostics.

## WS6: Runner Codex authentication (operator-assisted)

### Outcome

Runner VM Codex runtime is authenticated and ready for delegated tasks.

### Tasks

1. Execute Codex auth bootstrap on runner VM.
2. Request operator assistance for interactive auth step when needed.
3. Record completion evidence and residual access risks.

### Acceptance

- Runner Codex auth is confirmed and timestamped.
- Operator-assist step is logged when used.

## WS7: Runner agent bootstrap

### Outcome

`runner` has its own repository with AGENT contract and reporting
format, ready to accept delegated CI/test tasks.

### Tasks

1. Initialize runner repository and baseline docs.
2. Add `AGENTS.md`, workstream board, and handoff log.
3. Define runner reporting contract back to `arch`.

### Acceptance

- Runner repo and governance docs exist.
- A smoke report from runner to arch is captured.

## WS8: Multi-HUT hardware CI topology

### Outcome

Runner-based CI can schedule and run hardware jobs across multiple
attached boards in parallel with deterministic ownership.

### Lane Boundary Rules

1. Hardware job definitions stay out of mandatory upstream checks.
2. Upstream jobs must succeed without any hardware runner availability.
3. Hardware jobs are triggered from local lane and mapped back to same
   commit/branch context.

### Topology (recommended)

1. Runner VM runs orchestration:
   - GitHub self-hosted runners (or local runner service)
   - artifact store path
   - health checks
2. One runner label per HUT slot, e.g.:
   - `esp32-hut-01`
   - `esp32-hut-02`
3. Each HUT slot has stable serial identity:
   - `/dev/serial/by-id/...`
4. Job routing uses labels and lock files to prevent slot collision.

### Acceptance

- At least two HUT slots can run independent jobs without resource
  conflict.

### Required Runner Artifacts (WS8 completion evidence)

Produced in runner repository (by `runner:codex` on the runner VM):
- topology documentation describing labels + lock strategy
- per-slot lock wrapper (`flock`) and a self-test proving:
  - same-slot operations serialize
  - different-slot operations can run concurrently
- slot inventory evidence capturing `/dev/serial/by-id` visibility
- a local (ignored) slot mapping config with at least two slots defined
  (`hut-01`, `hut-02`) mapping to stable `/dev/serial/by-id/...`

### Current Status (WS8)

- Runner VM: two stable `/dev/serial/by-id/*` devices are visible.
  - Evidence (runner repo): `runner_hil_topo_inventory_20260213_215705_utc.log`
  - Evidence (runner repo): `runner_hil_topo_lock_selftest_20260213_215839_utc.log`
- GitLab: WS8 closeout pushed to `runner/runner-agent@c03a9a0`.

## WS9: Hard power-cycle via Home Assistant relay

### Outcome

Power control is scripted and lock-protected so recovery sequences are
deterministic and safe.

### Service Contract

1. Inputs:
   - relay entity id
   - slot id
   - on/off durations
2. Safety:
   - per-slot lock (`/var/lock/hut-<id>.lock`)
   - max retry count
   - cooldown interval
3. Output:
   - structured result (`ok`, `timeout`, `ha_error`)

### Acceptance

- CI job can request power-cycle for a slot and receive deterministic
  status.
- Concurrent power-cycle requests for the same slot are serialized.

## WS10: Branch/agent parallelism model

### Outcome

High parallel throughput with low conflict/overhead across agents.

### Recommended Pattern

1. Keep GitHub as source of truth (no mandatory local git server).
2. Use local bare mirror/cache only as accelerator (optional).
3. Spawn one worktree per agent/task:
   - `~/workspace/wt/<task-id>`
4. Run isolated build/test output folders per worktree.
5. Standardize artifact naming by task id and commit hash.

### Seamless Lane Handoff

1. Upstream lane publishes build artifacts keyed by commit SHA.
2. Local lane pulls/uses artifact for that same SHA when possible.
3. Hardware result summary references:
   - commit SHA
   - lane (`upstream` or `local-hil`)
   - HUT slot id
   - power-cycle count/recovery events

### Acceptance

- Multiple agents can run concurrently without workspace contamination.

## Achievable "Dream" Environment (pragmatic target)

## Base VM image

- Ubuntu 24.04 LTS (LXD VM, not container, for better device/systemd behavior)

## Core dependencies

1. `git`, `openssh-client`, `curl`, `jq`, `python3`, `pip`
2. Docker engine + buildx + compose plugin
3. ESP-IDF toolchain usage via project container (`espressif/idf:release-v5.5` derivative)
4. Optional observability:
   - `tmux`, `htop`, `nvme-cli`/disk monitors

## CI/Cd strategy

1. Primary CI remains GitHub-hosted + self-hosted hardware jobs.
2. Optional local preflight pipeline on LXD for fast feedback before push.
3. Trigger model:
   - on push to feature branches: local preflight + optional hardware smoke
   - on PR: gated hardware jobs by label/comment trigger

## Why no mandatory local git server?

Local Git service adds operational overhead and split-source risk.
Prefer:
1. GitHub canonical origin
2. optional local mirror for speed/caching only

## Long-run autonomy target for role-based Codex

1. `infra` has:
   - persistent auth
   - host/VM bootstrap scripts
   - health checks
   - clearly scoped sudo permissions
2. `runner` has:
   - persistent auth
   - CI/CD task runner scripts
   - artifact/report emission to `arch`
3. `arch` can delegate operations to `infra`/`runner` and receive
   structured reports without direct host-manual intervention.
4. Human operator only needed for:
   - policy decisions
   - hardware maintenance
   - credential rotation

## Deliverables Checklist

- [x] SSH passthrough and remote command wrapper verified
- [x] Multi-agent contract documented and smoke-tested
- [x] Infrastructure agent repository + governance bootstrap completed
- [ ] Runner VM provisioned with reproducible baseline
- [ ] Architecture-to-runner SSH connectivity verified
- [ ] Runner Codex authentication completed (operator-assisted)
- [ ] Runner agent repository + governance bootstrap completed
- [ ] Multi-HUT runner topology implemented with label routing
- [ ] Home Assistant relay power-cycle lock and retry logic implemented
- [ ] Parallel worktree branch workflow documented and operational
- [ ] Closure notes moved to archive when complete
