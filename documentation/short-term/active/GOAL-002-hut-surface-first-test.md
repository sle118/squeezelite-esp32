# GOAL-002: Surface Every HUT with First Hardware Test

Status: blocked  
Owner: unassigned  
Last updated: 2026-02-12

## Objective

Use the first implemented hardware test (`HW-BOOT-001`) as a surfacing pass
across every hardware-under-test (HUT) slot on the target system.

Reference source:

- `documentation/agents/integration_test_worklist.md`

## Why This Goal Exists

- `HW-BOOT-001` is already defined and partially validated (build path).
- HIL evidence is still incomplete.
- Running it across all slots establishes a baseline for real hardware coverage.

## Scope

In scope:

- local LXD hardware lane execution only
- per-slot cold-boot to operational-state validation
- standardized evidence capture for each slot

Out of scope:

- new test definitions beyond `HW-BOOT-001`
- upstream GitHub lane changes requiring hardware

## Inputs

1. HUT slot inventory and stable serial mappings.
2. Build artifact or deterministic same-SHA rebuild.
3. Slot-level flash/monitor command contract.

## Workstreams

## WS1: Slot Inventory and Identity

1. Assign one stable slot id per board (`hut-01`, `hut-02`, ...).
2. Record serial path (`/dev/serial/by-id/...`) per slot.
3. Record target type/platform profile per slot.

Acceptance:

- all active boards are mapped to stable slot ids and serial identities.

WS1 execution snapshot (2026-02-12):

- Commit: `bf2fff44`
- Evidence: `test/build/log/hut_slot_inventory_20260212.log`
- Result: current workspace has no visible `/dev/serial/by-id`, `ttyUSB*`, or `ttyACM*` devices; slot mapping must be collected on the LXD HIL host.

| Slot ID | Serial Path | Platform Profile | Status | Notes |
|---|---|---|---|---|
| pending | pending | pending | blocked | Inventory capture must run on hardware-attached target host |

## WS2: HW-BOOT-001 Execution Harness

1. Define one canonical command sequence for:
   - flash
   - reboot (or hard power-cycle where required)
   - boot-monitor evidence capture
2. Use one log naming standard:
   - `<sha>_<slot>_HW-BOOT-001_<timestamp>.log`

Acceptance:

- a single command path can run HW-BOOT-001 on any mapped slot.

## WS3: Per-Slot Surfacing Runs

1. Execute `HW-BOOT-001` on each mapped slot.
2. Capture result and log path per slot.
3. Mark slot as `pass`, `fail`, or `blocked` with reason.

Acceptance:

- every mapped slot has one recorded outcome and evidence pointer.

## WS4: Tracker and Matrix Sync

1. Update `HW-BOOT-001` row evidence in:
   - `documentation/agents/integration_test_worklist.md`
2. Update slot coverage status in:
   - `documentation/HARDWARE_TEST_MATRIX.md`

Acceptance:

- worklist and hardware matrix show the same slot-level reality.

## Exit Criteria

1. Every mapped slot has one HW-BOOT-001 outcome with evidence.
2. Remaining blockers are explicit, actionable, and assigned.
3. Next hardware item is named for follow-up execution.

## Park/Resume Note

- GOAL-002 kickoff was accidental and is intentionally parked.
- Resume condition: GOAL-001 is implemented and the LXD hardware backend is available.
