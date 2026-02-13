# Remote Delegation + Threading Contract

This document makes remote delegation predictable across sessions and
reduces "arch has to re-steer every time" drift.

Scope: how `arch:codex` delegates to `runner:codex` / `infra:codex` as
autonomous executors, including a lightweight handshake and a stable
thread reference mechanism.

## Definitions

- `delegation packet`: a Markdown prompt intended to be fed to a remote
  role Codex runtime (typically via SSH transport).
- `thread_ref`: an opaque identifier chosen by `arch` to correlate a
  multi-step remote effort across sessions (example:
  `thread_ref=RUNNER-WS8-20260213A`).
- `codex_thread_id` (optional): if the remote runtime exposes an
  internal conversation/thread ID, record it, but do not depend on it.
- `executor=remote_codex`: work is executed by the Codex runtime inside
  the role environment/repository.
- `executor=ssh_direct`: bootstrap/emergency only; direct commands run
  without prompting a remote Codex runtime.

Authoritative state is always in repos + evidence + commit SHAs, not in
chat history.

## Delegation Is Not Micromanagement

When `arch` delegates to a remote role, `arch` provides:

- objective and constraints (what outcome, what invariants)
- acceptance criteria and evidence format (what "done" looks like)
- reporting requirements (commit SHA(s), evidence paths, next action)
- escalation gates (what requires operator, what must bounce to another
  role)

Remote roles decide implementation details and may adjust the plan if
they can still satisfy the acceptance criteria. If they deviate, they
must explain the deviation in their handoff summary.

Commit hygiene rule:
- each role Codex runtime owns commit hygiene in its own role repository
  (small commits, frequent pushes, SHA-based reporting). `arch` must not
  "paper over" missing remote commits by committing into role repos
  locally.

## Handshake (HANDSHAKE-001)

Problem this solves: starting a new session without relying on implicit
context, while still enabling a stable multi-step "thread".

Rule:
- For any multi-step remote effort (or any time context is uncertain),
  start with a 30-90 second handshake request before deeper execution.

Handshake request must include:
- `thread_ref=<value>`
- target scope (workstream/ticket)
- the single most important success condition for the next step

Handshake response must include (in the role repo handoff log entry):
- `thread_ref=<same value>`
- `executor=remote_codex` (or `executor=ssh_direct` if forced)
- current `HEAD` commit SHA and branch
- 1-3 bullets: what the role believes is the best next step and why
- any blockers with `operator_required=yes|no`

If the role can provide a `codex_thread_id`, include it as
`codex_thread_id=<value>` in the summary, but treat it as advisory.

## Parallel Threads

Parallelism is allowed and expected:

- `arch` may run multiple simultaneous delegations, each with its own
  `thread_ref`.
- Do not try to "multiplex" multiple unrelated objectives into one
  thread.
- If two threads touch the same stateful target (for example the same
  HUT slot, the same VM configuration, or the same infra resource),
  the remote role must serialize safely (locks or explicit sequencing).

Transport note:
- SSH is the transport. It does not need to be persistent to enable
  parallel threads. Correlation happens via `thread_ref` + evidence +
  commit SHA(s), not via a single interactive SSH session.

## Where To Record `thread_ref`

`arch` must record the `thread_ref` in:

- the delegation packet header (or first lines), and
- the `documentation/short-term/coordination/handoff_log.md` summary
  field for `action_type=delegate` and subsequent related entries.

Remote roles must record the same `thread_ref` in their role-local
handoff log for related entries.

## Closure Contract (When `arch` Owns It)

If `arch:codex` is assigned as the owner (or is explicitly asked to "take
ownership") for a task/workstream that requires remote-role execution,
`arch` must drive the work to a stable closure state:

- `done`: acceptance met and evidence + commit SHA(s) recorded, or
- `blocked`: concrete blocker(s) recorded with:
  - correct role owner(s) for the next action (`infra:*` / `runner:*` /
    `operator`)
  - `operator_required=yes|no`
  - the exact next action (packet or command family), not a vague
    placeholder

Delegation packets are necessary but not sufficient: `arch` remains
responsible for follow-through and for keeping the short-term board and
handoff logs aligned with reality.

Allowed stop condition:
- `arch` may stop when a pivot is required or a problem-solving loop is
  stalled, but must first record a closure state (`replan` or `blocked`)
  with concrete next actions and the information needed to resume.
