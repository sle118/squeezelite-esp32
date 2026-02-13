# GOAL-001 / WS8 Delegation Packet (runner:codex)

Scope: implement `GOAL-001/WS8` by completing runner workstream
`HIL-TOPO-001` (multi-HUT topology and slot mapping).

Hard rule: execute inside the runner VM runner repo only
(`/home/runner/workspaces/codex-runner-agent`). Do not rely on any local
`/workspaces/codex-runner-agent` copy on the dev machine.

Completion rule (commit hygiene):
- WS8 is not complete until changes are committed and pushed to
  `origin/main` from the runner VM, and the pushed commit SHA is
  reported back. Local-only progress does not count.

## Deliverables (runner repo)

1. Slot mapping schema + example:
   - `config/hut_slots.example.json`
   - `.gitignore` that ignores `config/hut_slots.json`
2. Inventory + resolver:
   - `scripts/hut_inventory.sh` (captures `/dev/serial/by-id`)
   - `scripts/hut_slot_resolve.sh hut-XX` (reads `config/hut_slots.json`)
3. Per-slot locking:
   - `scripts/hut_lock_exec.sh hut-XX -- <cmd...>` using `flock`
   - `scripts/hut_lock_selftest.sh` proving:
     - same-slot serializes
     - different-slots can run concurrently
4. Topology doc:
   - `documentation/hil_topology.md`
5. Evidence logs (commit these):
   - `documentation/evidence/runner_hil_topo_inventory_<timestamp>.log`
   - `documentation/evidence/runner_hil_topo_lock_selftest_<timestamp>.log`
6. Local config (do not commit):
   - `config/hut_slots.json` with at least `hut-01` + `hut-02` mapping to
     stable `/dev/serial/by-id/...` symlinks.

## Execution Checklist (runner VM)

1. Confirm repo is clean:
   - `git -C /home/runner/workspaces/codex-runner-agent status --porcelain`
2. Implement deliverables above; ensure scripts are executable.
3. Run:
   - `scripts/hut_inventory.sh | tee documentation/evidence/runner_hil_topo_inventory_<timestamp>.log`
   - `scripts/hut_lock_selftest.sh | tee documentation/evidence/runner_hil_topo_lock_selftest_<timestamp>.log`
4. Populate local slot mapping (ignored file):
   - copy `config/hut_slots.example.json` -> `config/hut_slots.json`
   - replace `serial_by_id` with real paths from `/dev/serial/by-id`
   - validate:
     - `scripts/hut_slot_resolve.sh hut-01`
     - `scripts/hut_slot_resolve.sh hut-02`
5. Commit + push all tracked changes (do not commit `config/hut_slots.json`):
   - include commit SHA in runner handoff entry
6. Update runner coordination:
   - `documentation/coordination/workstream_board.md`: set `HIL-TOPO-001`
     to `done` with evidence paths
   - `documentation/coordination/handoff_log.md`: append one line with:
     - `context=runner-live`, `action_type=execute`, `operator_required=no`
     - commit SHA(s), evidence paths, and next action (`POWER-CTL-001`)

## Report Back (to arch)

Return:
- pushed commit SHA
- evidence paths
- summary of discovered slots (`hut-01`, `hut-02`) and their `/dev/serial/by-id` identities
