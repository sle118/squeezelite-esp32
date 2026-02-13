# GOAL-001 / WS8 Runner Closeout Packet (runner:codex)

thread_ref=WS8-CLOSE-20260213A

Goal: bring `GOAL-001/WS8` to completion by finishing runner workstream
`HIL-TOPO-001` with real slot identities (>=2 `/dev/serial/by-id/*`),
updating runner coordination, and pushing the resulting commit(s) to
`origin/main`.

Hard rules:
- execute inside runner VM repo only:
  - `/home/runner/workspaces/codex-runner-agent`
- do not commit local slot mapping file:
  - `config/hut_slots.json` must remain ignored
- completion requires:
  - evidence committed, coordination updated, and a pushed SHA on
    `origin/main` (local-only progress does not count)

## Tasks

1. Verify device count:
   - list `/dev/serial/by-id/*`
   - if fewer than 2 entries exist: stop and report `blocked` with
     `by_id_count=<n>` and the exact missing dependency.
2. Inventory evidence (commit this):
   - run:
     - `scripts/hut_inventory.sh | tee documentation/evidence/runner_hil_topo_inventory_<timestamp>.log`
3. Slot mapping (local-only, do not commit):
   - update `config/hut_slots.json` to map:
     - `hut-01` -> one stable `/dev/serial/by-id/*`
     - `hut-02` -> a different stable `/dev/serial/by-id/*`
   - validate:
     - `scripts/hut_slot_resolve.sh hut-01` resolves and target exists
     - `scripts/hut_slot_resolve.sh hut-02` resolves and target exists
4. Lock evidence (commit this):
   - run:
     - `scripts/hut_lock_selftest.sh | tee documentation/evidence/runner_hil_topo_lock_selftest_<timestamp>.log`
5. Runner coordination:
   - update `documentation/coordination/workstream_board.md`:
     - set `HIL-TOPO-001` to `done` once the above is satisfied
     - ensure any stale WS8/push rows reflect current state (push is no
       longer blocked if PAT is configured)
   - append `documentation/coordination/handoff_log.md` entry with:
     - `context=runner-live`, `action_type=execute`, `operator_required=no`
     - `thread_ref=...`
     - evidence paths
     - pushed SHA (after push)
     - next action: `POWER-CTL-001`
6. Commit + push:
   - stage only tracked changes (evidence + coordination), not
     `config/hut_slots.json`
   - commit with a small message (WS8 closeout)
   - push to `origin/main` using a PAT-backed non-interactive method
     (do not paste secrets into logs). If available, use:
     - `/home/runner/.local/bin/gitlab_push_origin_main.sh`
7. Report back to arch:
   - pushed commit SHA
   - evidence paths
   - by-id identities chosen for `hut-01` and `hut-02`

