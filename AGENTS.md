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
- Do not introduce platform-specific compile-time branches when
  runtime/config solutions are sufficient.
- Avoid unrelated churn; keep patches tightly scoped to the request.

## Standard Workflow

1. Read the request and identify touched modules and contracts.
2. Inspect current code and existing patterns in those modules.
3. Implement the smallest coherent patch that preserves contracts.
4. Run targeted validation commands.
5. Report changes, validation evidence, and residual risks.
6. Commit hygiene: make small commits and push frequently when progress
   needs to be traceable by commit SHA across sessions/roles.

Commit hygiene scope:
- each role Codex runtime is responsible for commit hygiene in its own
  repository (product repo for `arch`, infra repo for `infra`, runner
  repo for `runner`)

## Ownership Contract (Arch Closure Rule)

When `arch:codex` is explicitly asked to "take ownership" of a task or
workstream, `arch:codex` is accountable for driving it to closure:

- closure means either:
  - `done` with acceptance met and evidence recorded, or
  - `blocked` with concrete blockers, owners, operator gates, and next
    actions recorded (not vague "waiting")
- `arch:codex` may delegate execution to `infra:codex` / `runner:codex`,
  but must actively steer until closure (handoffs updated, blockers
  cleared or escalated, and the workstream status stabilized)
- do not stop at "prepared a packet"; prepared packets are inputs to
  closure, not closure themselves

Exception:
- `arch:codex` may stop early when a pivot is required or a problem
  solving loop is stalled, but only after recording a stable closure
  state:
  - `action_type=replan` with the pivot proposal and updated next
    actions, or
  - `blocked` with the specific stalled hypothesis, what was tried, and
    what new information is needed to proceed

## Validation Commands

- Style check: `build-scripts/lint_style.sh check`
- Style normalize: `build-scripts/lint_style.sh format`
- Firmware build (example): `idf.py build`
- Recovery footprint (platform-specific):
  `build-scripts/build_recovery_size.sh <platform|defaults-file> [build-dir]`

Use targeted checks first; avoid full-matrix builds unless requested.

## Testing Rule (Grounding)

- Tests should validate behavior contracts and invariants, not implementation details.
- Prefer module-boundary and regression tests over private-internal assertions.
- Every bug fix should include a regression test at the highest stable
  boundary possible.
- Use:
  - `documentation/TESTING_CHARTER.md`
  - `documentation/CONTRACT_TEST_TEMPLATE.md`
  - `documentation/HARDWARE_TEST_MATRIX.md`
  - `documentation/agents/ci_lane_contract.md`

## Style Baseline

- Use repository formatting/lint settings as the source of truth.
- Do not introduce style-only churn outside task scope unless requested.
- During refactors, normalization is acceptable when intentionally planned.
- For doc-only cleanup tasks, follow
  `documentation/agents/document_gardening.md`.

## Frontend Context (Required)

- For any UI/API work involving `components/wifi-manager/webapp/` or
  `components/wifi-manager/http_server_handlers.c`, read:
  - `documentation/agents/frontend_requirements_context.md`
- Treat that file as requirements-first context and keep it current.
- When frontend payload, routes, or protobuf contract changes, run:
  - `build-scripts/ui_footprint_snapshot.sh`
- Then update `documentation/agents/frontend_requirements_context.md` in
  the same change.

## Agent Docs Layout

- Keep this file concise and policy-focused.
- Put detailed playbooks under `documentation/agents/`.
- For progressive context discovery, start with:
  - `documentation/agents/start_here.md`
- For remote-role delegation (`infra`/`runner`), follow:
  - `documentation/agents/remote_delegation_contract.md` (autonomous
    delegation + handshake + `thread_ref` correlation)
- Add module-specific guidance in local `AGENTS.md` files when needed
  (closest file to changed code takes precedence).

## Short-Term Goal Tracking (Ephemeral)

- Active multi-agent implementation goals live under:
  - `documentation/short-term/active/`
- Coordination files live under:
  - `documentation/short-term/coordination/workstream_board.md`
  - `documentation/short-term/coordination/handoff_log.md`
- When prompted to continue pending multi-agent work, read those files
  first and update them before ending the session.
- Use role-based owner values in short-term boards:
  - `arch:<agent-id>` for architecture/control-plane work
  - `infra:<agent-id>` for infrastructure/host/VM work
  - `runner:<agent-id>` for VM CI/CD execution work
- Handoff entries for short-term goals should include:
  - `context` (`arch-local` | `infra-live` | `runner-live`)
  - `action_type` (`scaffold` | `delegate` | `execute` | `replan`)
  - `operator_required` (`yes` | `no`)
- Role-origin evidence naming should use prefixes where feasible:
  - `arch_*`, `infra_*`, `runner_*`
- User/operator reprioritization or path-shift requests (e.g. \"we
  missed...\", \"easier way...\", \"we should prioritize...\") should
  trigger a formal short-term replan update before continuing execution.
- For ad-hoc user requests, proactively ask whether to track as a
  ticket (`yes`/`no`) and, when accepted, record it in
  `documentation/short-term/coordination/ad_hoc_ticket_queue.md`.
- For local GitLab usage (`git.lecsys.net`), keep role secrets only in
  `${XDG_CONFIG_HOME:-$HOME/.config}/codex/credentials/gitlab/git.lecsys.net/`
  with directory mode `0700` and file mode `0600`; never commit
  credential files or paste secret values in handoff logs.
- Preserve CI lane boundary:
  - upstream GitHub lane for build/non-hardware tests
  - local LXD lane for hardware-in-loop execution
- Repository boundary enforcement (strict):
  - never edit or run role execution from `arch` against runner/infra
    repositories on this machine (for example
    `/workspaces/codex-runner-agent`, `/workspaces/codex-infra-agent`)
  - all `runner` and `infra` workstreams must be executed by their
    remote Codex runtimes and reported back via handoff logs
- Completion gate:
  - Workstream `done` means that workstream acceptance is met.
  - Goal completion is separate and requires every item in the goal
    deliverables checklist to be checked with evidence.
  - Do not mark a goal complete or archive it while any deliverable
    checklist item remains unchecked.
