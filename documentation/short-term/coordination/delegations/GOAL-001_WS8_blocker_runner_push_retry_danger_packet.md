# GOAL-001 / WS8 Blocker Packet: Push Retry (danger-full-access) (runner:codex)

Purpose: retry pushing the runner repo local WS8 commits to origin from
the runner VM **outside the restricted sandbox**.

Notes:
- Previous attempts ran under a restricted execution environment where:
  - `sudo` was blocked by `no new privileges`
  - network reachability tests failed
- This retry must be executed with `codex exec --sandbox danger-full-access`
  so the agent can access the VM’s real network and privileges.

## Tasks

Completion rule:
- this blocker is not cleared until the runner repo changes are pushed
  to `origin/main` and the pushed commit SHA is reported.

1. Confirm network + DNS from the runner VM (evidence):
   - `getent hosts git.lecsys.net || true`
   - `ping -c 1 -W 1 git.lecsys.net || true`
   - `curl -fsS http://git.lecsys.net/ >/dev/null && echo ok_http || echo fail_http`
   - `git ls-remote http://git.lecsys.net/runner/runner-agent.git HEAD`
2. Reconcile with `origin/main` before pushing (avoid unsafe force):
   - `git -C /home/runner/workspaces/codex-runner-agent fetch origin`
   - `git -C /home/runner/workspaces/codex-runner-agent status --porcelain`
   - `git -C /home/runner/workspaces/codex-runner-agent log --oneline -n 15`
   - `git -C /home/runner/workspaces/codex-runner-agent log --oneline --decorate origin/main..HEAD || true`
   - `git -C /home/runner/workspaces/codex-runner-agent log --oneline --decorate HEAD..origin/main || true`
   - If `HEAD..origin/main` is non-empty, prefer `git pull --rebase` and
     re-run the checks above.
   - Do not use `--force` unless explicitly required and safe; if it is
     required, prefer `--force-with-lease` and record the justification
     + before/after SHAs in evidence.
3. Push all local commits on `main`:
   - `git -C /home/runner/workspaces/codex-runner-agent status --porcelain`
   - `git -C /home/runner/workspaces/codex-runner-agent log --oneline -n 10`
   - `git -C /home/runner/workspaces/codex-runner-agent push origin main`
   - if non-interactive auth is required, use the role PAT contract
     (`runner.env`) and a PAT-backed push method; do not paste secrets
     into logs
4. Evidence + coordination:
   - write `documentation/evidence/runner_ws8_push_retry_<timestamp>.log`
     capturing the above command outputs and push result
   - append a runner handoff line with:
     - `context=runner-live`, `action_type=execute`
     - pushed commit SHA range and evidence path

## Completion

- `git push origin main` succeeds and commits appear on `origin/main`
