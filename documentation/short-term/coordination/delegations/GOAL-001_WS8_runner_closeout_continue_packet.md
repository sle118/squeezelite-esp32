# GOAL-001 / WS8 Runner Closeout Continue Packet (runner:codex)

thread_ref=WS8-CLOSE-20260213A

Use this only if the main closeout packet was interrupted mid-run.

Goal: finish the remaining closeout steps, commit evidence/coordination,
and push to `origin/main` using a PAT-backed non-interactive method.

Hard rules:
- execute inside runner VM repo only:
  - `/home/runner/workspaces/codex-runner-agent`
- do not commit `config/hut_slots.json`

## Tasks

1. Validate slot resolution:
   - `p1=$(scripts/hut_slot_resolve.sh hut-01); test -e "$p1"`
   - `p2=$(scripts/hut_slot_resolve.sh hut-02); test -e "$p2"`
   - record `hut-01` and `hut-02` by-id strings in the handoff summary
2. Lock selftest evidence (commit this):
   - `scripts/hut_lock_selftest.sh | tee documentation/evidence/runner_hil_topo_lock_selftest_<timestamp>.log`
3. Update runner coordination:
   - set `HIL-TOPO-001` to `done` with evidence paths
   - fix stale WS8/push row (credentials are now configured and pushes work)
   - append runner handoff log entry:
     - `context=runner-live`, `action_type=execute`, `operator_required=no`
     - `thread_ref=...`
     - evidence paths
     - next action: `POWER-CTL-001`
4. Commit + push:
   - stage evidence + coordination only
   - commit (small message)
   - push via `/home/runner/.local/bin/gitlab_push_origin_main.sh`
   - if push fails with `HTTP Basic: Access denied`, treat it as a
     credential blocker:
     - stop and report `operator_required=yes`
     - ask operator to refresh the runner PAT with `write_repository`
       and update `~/.config/codex/credentials/gitlab/git.lecsys.net/runner.env`
5. Report back:
   - pushed commit SHA
   - evidence paths
   - selected hut-01/hut-02 by-id identities
