# GOAL-001 / WS8 Blocker Packet: Unblock Runner Push (runner:codex)

Objective: unblock `git push origin main` from the runner VM so WS8
topology work can be pushed and reported by commit SHA.

This is not a separate workstream from WS8; it is a WS8 blocker-clearing
packet.

Context (from arch side):
- `git.lecsys.net` resolves to `192.168.10.75` from the arch environment
- runner VM error was: `Could not resolve host: git.lecsys.net`

## Tasks (runner VM)

Completion rule:
- this blocker is not cleared until the runner repo changes are pushed
  to `origin/main` and the pushed commit SHA is reported.

1. Capture DNS diagnostics (evidence):
   - `getent hosts git.lecsys.net || true`
   - `cat /etc/resolv.conf || true`
   - `ip -4 addr && ip route`
2. Attempt to reach GitLab by IP (evidence):
   - `curl -fsS http://192.168.10.75/ >/dev/null && echo ok_ip_http || echo fail_ip_http`
3. Fix name resolution, preferring least-invasive:
   - If `getent hosts git.lecsys.net` fails, try adding a host entry:
     - idempotent edit: only add if missing
     - line: `192.168.10.75 git.lecsys.net`
     - command suggestion:
       - `sudo sh -c 'grep -q \"^192\\.168\\.10\\.75[[:space:]]\\+git\\.lecsys\\.net$\" /etc/hosts || echo \"192.168.10.75 git.lecsys.net\" >> /etc/hosts'`
   - If `sudo` is unavailable/non-functional:
     - stop and report blocker `operator_required=yes` and recommend
       delegating to `infra` to fix runner VM DNS.
4. Verify name resolution works:
   - `getent hosts git.lecsys.net`
   - `curl -fsS http://git.lecsys.net/ >/dev/null && echo ok_name_http || echo fail_name_http`
5. Push runner repo commits:
   - `git -C /home/runner/workspaces/codex-runner-agent status --porcelain`
   - `git -C /home/runner/workspaces/codex-runner-agent log --oneline -n 5`
   - `git -C /home/runner/workspaces/codex-runner-agent push origin main`
   - if non-interactive auth is required, use the role PAT contract
     (`runner.env`) and a PAT-backed push method; do not paste secrets
     into logs
6. Evidence + coordination:
   - write one evidence log capturing key outputs:
     - `documentation/evidence/runner_ws8_push_unblock_<timestamp>.log`
   - append runner handoff line:
     - `context=runner-live`, `action_type=execute`
     - include pushed commit SHA(s) and the evidence path

## Completion Criteria

- `git push origin main` succeeds from runner VM
- runner handoff log includes the push success and evidence path
