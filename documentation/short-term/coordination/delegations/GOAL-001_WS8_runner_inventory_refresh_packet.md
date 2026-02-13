# GOAL-001 / WS8 Runner Packet: Inventory Refresh After USB Passthrough (runner:codex)

thread_ref=WS8-USB-INV-20260213A

Goal: after infra USB passthrough changes, refresh WS8 inventory evidence
and report whether runner now has >=2 stable `/dev/serial/by-id/*`
devices for `hut-01` + `hut-02`.

Hard rules:
- execute inside runner VM repo only:
  - `/home/runner/workspaces/codex-runner-agent`
- do not commit local slot mapping file:
  - `config/hut_slots.json` must remain ignored
- commit hygiene: if any tracked files are changed (evidence/coordination),
  commit and push them to `origin/main` from the runner VM and report the
  pushed SHA; local-only progress does not count.

## Tasks

1. Inventory evidence:
   - run `scripts/hut_inventory.sh` and capture a new evidence log:
     - `documentation/evidence/runner_hil_topo_inventory_<timestamp>.log`
2. Slot mapping status:
   - if there are >=2 `/dev/serial/by-id/*` entries:
     - populate local `config/hut_slots.json` with `hut-01` + `hut-02`
       using stable by-id paths
     - validate:
       - `scripts/hut_slot_resolve.sh hut-01`
       - `scripts/hut_slot_resolve.sh hut-02`
   - else (only 0 or 1 device visible):
     - do not fake identities
     - record explicit blocker: "need >=2 serial devices passed through"
3. Coordination:
   - update runner `documentation/coordination/workstream_board.md`:
     - keep `HIL-TOPO-001` status `blocked` until >=2 devices exist
     - reference the new inventory evidence log
   - append runner `documentation/coordination/handoff_log.md` entry with:
     - `context=runner-live`, `action_type=execute`, `operator_required=no`
     - `thread_ref=...`
     - inventory evidence path
     - current device count observed
     - next action (infra/operator attach + pass through a 2nd device if missing)
4. Commit + push:
   - commit and push the new evidence + coordination updates (not `hut_slots.json`)
   - report pushed commit SHA

## Completion

- new inventory evidence is committed+pushed, and runner coordination reflects
  current device count and blocker/next action accurately.
