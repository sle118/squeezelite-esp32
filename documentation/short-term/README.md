# Short-Term Goals (Ephemeral)

Use this folder for time-boxed, multi-agent implementation goals.

This area is intentionally operational and temporary:

- track current work
- coordinate handoffs
- capture blockers and next actions
- archive when complete

## Structure

- `documentation/short-term/active/`
  - active goal documents (one file per goal)
- `documentation/short-term/coordination/workstream_board.md`
  - current ownership and status by workstream
- `documentation/short-term/coordination/handoff_log.md`
  - chronological handoffs across agents/sessions
- `documentation/short-term/coordination/ad_hoc_ticket_queue.md`
  - optional ticket tracking queue for user-approved ad-hoc requests
- `documentation/short-term/archive/`
  - closed goals moved out of active rotation

## Operating Rules

1. Keep one active goal file as the source of truth for scope and acceptance criteria.
2. Update `workstream_board.md` when status, owner, or blocker changes.
3. Add a handoff entry before ending a session when work is in progress.
4. Keep entries concise and actionable; do not duplicate long-term
   architecture docs here.
5. Treat workstream completion and goal completion separately:
   - workstream `done` requires that workstream acceptance is met
   - goal completion requires every deliverable checklist item in the goal file checked with evidence
6. Move completed goals to `archive/` only after all deliverable checklist
   items are checked; leave a short completion note in the board.
7. For docs-maintenance sessions, use
   `documentation/agents/document_gardening.md`.
8. Use role-based ownership format in workstream boards:
   `<role>:<agent-id>` where role is one of:
   - `arch` (Architecture Agent)
   - `infra` (Infrastructure Agent)
   - `runner` (Runner Agent)
9. Enforce repository boundaries:
   - `infra` and `runner` operate from dedicated repositories
   - `infra` must never clone product repositories like
     `squeezelite-esp32`
   - `arch` must never execute `runner` or `infra` workstreams by
     editing or running commands in their repositories locally (for
     example `/workspaces/codex-runner-agent`); instead, delegate by
     prompting the remote Codex runtime in that role environment
10. Handoff entries must include execution metadata:
   - `context`: `arch-local` | `infra-live` | `runner-live`
   - `action_type`: `scaffold` | `delegate` | `execute` | `replan`
   - `operator_required`: `yes` | `no`
11. Use evidence naming prefixes by role:
   - `arch_*`, `infra_*`, `runner_*`
12. For remote-role workstreams (`infra`/`runner`):
   - do not move to `in_progress` while owner is `tbd`
   - complete delegation checklist before execution:
     - owner assigned
     - activation state at least `assigned`
     - first handoff line logged in role context
13. Owner semantics are execution semantics:
   - `<role>:codex` means Codex is installed/authenticated in that role
     environment and the work is executed by that remote Codex runtime.
   - `<role>:agent` means the `arch:codex` control-plane is acting as a
     temporary proxy executor for that role (typically via SSH transport).
     This is allowed only for bootstrap/emergency and must be explicitly
     called out in handoff summaries as `executor=ssh_direct`.
   - avoid ambiguous ownership values like `infra:codex` unless the
     remote Codex runtime is actually active and being prompted there.
14. Commit hygiene (arch + runner):
   - make small commits that correspond to workstream-sized progress
   - push frequently so progress is traceable by remote commit SHA
   - include commit SHA(s) in handoff summaries when work is executed
   - do not batch unrelated changes; avoid “mega commits”
   - do not commit secrets or generated/build outputs

## Ad-hoc Ticket Reflex

When user/operator gives an ad-hoc request, agents should proactively
offer ticket tracking and let the user decide.

Required reflex:
1. Ask: "Track this as a ticket? (yes/no)" before major execution.
2. If `yes`:
   - create/update entry in
     `documentation/short-term/coordination/ad_hoc_ticket_queue.md`
   - reference ticket id in handoff summaries and relevant workstream
     notes
3. If `no`:
   - continue execution without ticket creation
   - add one line in handoff summary: `ticket_tracking=declined`
4. Prefer `yes` recommendation when request affects:
   - cross-session continuity
   - cross-role delegation
   - sequencing/prioritization
   - non-trivial risk or scope

## GitLab Credential Home Contract

Use this contract for agent-repo bootstrap work targeting
`git.lecsys.net`.

Canonical local secret home:
- `${XDG_CONFIG_HOME:-$HOME/.config}/codex/credentials/gitlab/git.lecsys.net/`

Required files (one per account/role):
- `codex.env`
- `arch.env`
- `infra.env`
- `runner.env`

Required file variables:
- `GITLAB_HOST=git.lecsys.net`
- `GITLAB_USER=<account>`
- `GITLAB_PASSWORD=<bootstrap-only>`
- `GITLAB_PAT=<preferred for non-interactive git/api>`

## Remote Agent Credential + Invocation Contract

Remote agents need two things:
1. credentials stored locally in a non-repo location, and
2. a documented transport/invocation mechanism.

### Credential Storage (Do Not Commit)

- SSH transport (example: LXD host access):
  - configure connection variables in `.lxd.env` (see `build-scripts/lxd_remote.sh`)
  - store private keys outside the repo (typically under `~/.ssh/`) and
    point to them via `LXD_SSH_PRIVATE_KEY_PATH`
- GitLab (agent repos):
  - store role credentials in:
    `${XDG_CONFIG_HOME:-$HOME/.config}/codex/credentials/gitlab/git.lecsys.net/<role>.env`
    (mode `0600`; parent directory mode `0700`)
  - prefer PAT for non-interactive git/API; never embed secrets in
    remotes, scripts, or handoff logs
- Codex login (remote Codex runtime):
  - authentication state lives on the remote machine under the remote
    user's Codex config/state (do not copy it into repos)

### Invocation Mechanism (Prompting Remote Codex)

When a workstream owner is `<role>:codex`, delegation means: prompt the
Codex runtime in that role environment to do the work inside its role
repository, then report back via commit SHA + evidence paths.

Transport is typically SSH, but SSH is not the execution model:
- `executor=remote_codex`: SSH is only used to reach the remote and run
  `codex exec` (or open an interactive Codex session) on that machine.
- `executor=ssh_direct`: bootstrap/emergency only (direct commands
  without prompting a remote Codex runtime).

Template (non-interactive delegation using a packet read from stdin):

```bash
ssh -i ~/.ssh/<role_key> <role_user>@<role_host> \\
  'cd /path/to/<role-repo> && codex exec --sandbox workspace-write -C . -' \\
  < documentation/short-term/coordination/delegations/<packet>.md
```

Security contract:
1. Create root directory with mode `0700`.
2. Create each `*.env` file with mode `0600`.
3. Do not store secrets in repository files, commit messages, or
   handoff summaries.
4. Propagate only role-specific credential files to target runtime
   hosts; never copy the entire secret home.

Auth mode contract:
1. Account password is bootstrap-only (initial sign-in/token creation).
2. PAT is the default for non-interactive API/git operations.
3. Git remotes use HTTPS hostnames without embedded credentials.

Verification flow (record only redacted evidence):
1. Verify permissions: `stat` on secret root and role file.
2. Verify auth: `curl --fail -H "PRIVATE-TOKEN: $GITLAB_PAT"
   https://git.lecsys.net/api/v4/user`.
3. Verify repo reachability: `git ls-remote
   https://git.lecsys.net/<namespace>/<repo>.git`.
4. Log pass/fail and command context without printing token/password.

## Agent Repo Bootstrap Flow (ADHOC-20260212-03)

GitLab remotes created/pushed:
- `http://git.lecsys.net/infra/infra-agent.git`
- `http://git.lecsys.net/runner/runner-agent.git`

Bootstrap checkout commands:

```bash
git clone http://git.lecsys.net/infra/infra-agent.git /workspaces/codex-infra-agent
git -C /workspaces/codex-infra-agent checkout main

git clone http://git.lecsys.net/runner/runner-agent.git /workspaces/codex-runner-agent
git -C /workspaces/codex-runner-agent checkout main
```

Bootstrap verification:

```bash
git -C /workspaces/codex-infra-agent remote -v
git -C /workspaces/codex-runner-agent remote -v
git ls-remote http://git.lecsys.net/infra/infra-agent.git
git ls-remote http://git.lecsys.net/runner/runner-agent.git
```

Transport note (2026-02-12):
- `https://git.lecsys.net` was unreachable from this execution
  environment (TCP/443 connect failure), while `http://git.lecsys.net`
  was reachable and used for bootstrap push/clone.
- After TLS is available, migrate remotes to `https://` and switch to
  PAT-based auth for non-interactive operations.

## Goal Adjustment Protocol

Apply this protocol for both hard prerequisites and softer strategy
changes (priority shifts, easier path proposals, intermediate quests).

Trigger examples from operator/user input:
- \"we missed ...\"
- \"in order to achieve this in an easier way ...\"
- \"we should prioritize ...\"

Required actions:
1. Add a `replan` handoff entry documenting the requested change.
2. Freeze affected workstreams (`blocked`) if current sequencing is no
   longer valid.
3. Add/reshape workstreams in the active goal for:
   - prerequisite fixes, and/or
   - intermediate quest path(s) that improve delivery.
4. Rewire dependencies/blockers and next actions in the board.
5. Reconfirm owner assignment and activation state for impacted remote
   roles.
6. Resume execution only after the new plan is reflected in goal + board
   + handoff docs.

## Goal Template

When creating a new short-term goal, start from:

- `documentation/short-term/active/GOAL_TEMPLATE.md`

## Current Active Goals

- `GOAL-001`: LXD Codex orchestration and multi-HUT CI foundation
- `GOAL-002`: HUT surfacing with first implemented hardware test (`HW-BOOT-001`)
- `GOAL-003`: agent-driven documentation linting in CI/CD

## Agent Pickup Order

When asked to continue short-term work, read in this order:

1. `documentation/short-term/coordination/workstream_board.md`
2. relevant file in `documentation/short-term/active/`
3. `documentation/short-term/coordination/handoff_log.md`
