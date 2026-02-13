# GOAL-001 / WS8 Runner Handshake + Push Packet (runner:codex)

thread_ref=WS8-PUSH-20260213A

Goal: drive WS8 to a pushed state (commit SHA visible on `origin/main`)
and report back with evidence. This packet is safe to run repeatedly.

Hard rules:
- execute inside runner VM repo only:
  - `/home/runner/workspaces/codex-runner-agent`
- do not leak secrets into logs, repo files, or handoff summaries
- completion requires a pushed SHA on `origin/main` (not just local commits)

## Tasks

1. Handshake (record in runner handoff summary):
   - `thread_ref=...`
   - current `HEAD` SHA and branch
   - what you believe is the best next step and why
2. Collect non-secret diagnostics (evidence log):
   - `getent hosts git.lecsys.net || true`
   - `curl -fsS http://git.lecsys.net/ >/dev/null && echo ok_http || echo fail_http`
   - `git -C /home/runner/workspaces/codex-runner-agent remote -v`
   - `git -C /home/runner/workspaces/codex-runner-agent status --porcelain`
   - `git -C /home/runner/workspaces/codex-runner-agent fetch origin`
   - `git -C /home/runner/workspaces/codex-runner-agent log --oneline --decorate -n 10`
   - `git -C /home/runner/workspaces/codex-runner-agent log --oneline --decorate origin/main..HEAD | head -n 50 || true`
3. Credential gate (do not print secrets):
   - expected credential file:
     - `${XDG_CONFIG_HOME:-$HOME/.config}/codex/credentials/gitlab/git.lecsys.net/runner.env`
   - if missing or `GITLAB_USER`/`GITLAB_PAT` is empty:
     - create the directory + a template file with correct perms
       (`0700` dir, `0600` file)
     - stop and report `operator_required=yes` with the exact path that
       needs to be filled
4. Push:
   - once `runner.env` has non-empty `GITLAB_USER` and `GITLAB_PAT`,
     push non-interactively without storing credentials in git config.
   - recommended approach:
     - use an ephemeral `http.extraheader` basic auth header built in
       memory and run:
       - (optional) `git -c http.extraheader="Authorization: Basic <...>" fetch origin`
       - `git push origin main`
   - if available, you may use an approved local helper script that
     performs a PAT-backed push without persisting credentials in git
     config (for example `/home/runner/.local/bin/gitlab_push_origin_main.sh`)
5. Evidence + coordination:
   - write:
     - `documentation/evidence/runner_ws8_push_handshake_<timestamp>.log`
   - append a runner handoff log line with:
     - `context=runner-live`, `action_type=execute`
     - `thread_ref=...`
     - pushed SHA(s) (or remaining blocker)
     - evidence path

## Completion Criteria

- `origin/main` contains the WS8 commits (push succeeded), and runner
  handoff includes pushed SHA(s) + evidence path.
