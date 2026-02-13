# Ad-hoc Ticket Queue

Use this queue when user/operator approves ticket tracking for ad-hoc
requests.

## Ticket ID Format

- `ADHOC-YYYYMMDD-<NN>`
- example: `ADHOC-20260212-01`

## Status Values

- `open`
- `in_progress`
- `blocked`
- `done`
- `declined` (used only when user explicitly declines tracking after
  suggestion)

## Queue

| Ticket ID | Requested UTC | Requested By | Scope Summary | Linked Goal/WS | Owner | Status | Next Action | Evidence |
|---|---|---|---|---|---|---|---|---|
| ADHOC-20260212-01 | 2026-02-12 18:30 UTC | operator | Establish ad-hoc ticket reflex and replan handling protocol | GOAL-001/governance | arch:codex | done | monitor adoption in future sessions | `documentation/short-term/README.md`, `documentation/short-term/coordination/workstream_board.md`, `documentation/short-term/coordination/handoff_log.md` |
| ADHOC-20260212-02 | 2026-02-12 20:04 UTC | operator | Standardize GitLab credential home + secure propagation contract for future agent bootstrap | GOAL-001/WS3-WS7 | arch:codex | done | execute ADHOC-20260212-03 remote/repo bootstrap against standardized credential contract | `AGENTS.md`, `documentation/short-term/README.md`, `documentation/short-term/active/GOAL-001-lxd-codex-hardware-ci.md`, `documentation/short-term/coordination/handoff_log.md`, `/workspaces/codex-infra-agent/AGENTS.md` |
| ADHOC-20260212-03 | 2026-02-12 20:04 UTC | operator | Push new Codex agent repos to GitLab and define bootstrap checkout flow against those remotes | GOAL-001/WS3-WS7 | arch:codex | done | proceed with GOAL-001 role assignment/delegation gates for WS4+ using published remotes and bootstrap contract | `documentation/short-term/README.md`, `documentation/short-term/active/GOAL-001-lxd-codex-hardware-ci.md`, `documentation/short-term/coordination/handoff_log.md`, `/workspaces/codex-infra-agent/README.md`, `/workspaces/codex-runner-agent/README.md` |

## Usage Rules

1. Agent should ask user if ad-hoc request should be tracked.
2. If user says `yes`, create/update a row here before major execution.
3. If user says `no`, do not create a ticket row; log
   `ticket_tracking=declined` in handoff instead.
