# Integration Test Worklist

Date: 2026-02-12
Scope: Shared progress tracker for integration and hardware-boundary tests in `squeezelite-esp32`.

## Related Active Goal

- `documentation/short-term/active/GOAL-002-hut-surface-first-test.md`
  drives `HW-BOOT-001` across all available HUT slots on the target
  system.

## How To Use

- Update this file in every integration-test PR that changes status.
- Keep entries ordered by priority (`P0`, then `P1`, then `P2`).
- Status values: `todo`, `in_progress`, `blocked`, `done`.
- Add `Owner`, `Last Update`, and `Evidence` (PR, CI run, or log path) when status changes.
- Do not remove completed rows; keep history visible.

## Agent Contract

Use this contract at the start of any new conversation so execution is consistent.

```text
MODE: guided+freeform
GOAL: implement test roadmap execution using documentation/agents/integration_test_worklist.md
START_ITEM: <ID or auto>      # e.g. UT-CHUNK-001 or auto
CONTROL: stepwise              # one step at a time
VALIDATION: fast|full          # default validation level
CONSTRAINTS:
- short answers
- precise control points
- update worklist status/evidence/handoff on every step
FIRST_ACTION: propose next step with A/B/C + freeform option
```

Minimal kickoff:

```text
Use documentation/agents/integration_test_worklist.md as orchestrator.
Run guided+freeform, short responses, one step at a time.
Start with UT-CHUNK-001, validation=fast.
Give A/B/C plus freeform each step.
```

### Short-Hand Hints

- `kickoff auto fast` -> start from highest-priority unclaimed item with fast checks
- `kickoff <ID> full` -> start from specific item with full checks
- `pick A|B|C` -> choose one proposed option
- `do: <plain instruction>` -> freeform instruction instead of multiple choice
- `switch <ID>` -> change active item
- `pause` -> stop changes and wait
- `continue` -> proceed with current plan
- `tighten` -> stricter done criteria and evidence bar
- `status` -> one-screen summary of active item, blockers, next action
- `handoff` -> force handoff update now (status, evidence, next)

### `idf.py` Usage Hints

- Baseline test-build invocation in this repo:
  - `source /opt/esp/idf/export.sh >/tmp/idf_export.log 2>&1 && idf.py -C test build`
- Why this is appropriate:
  - `test/CMakelists.txt` defines a standalone ESP-IDF test project, so `-C test` is the expected entry point.

For long/chatty builds, redirect to a temporary log to avoid context overload:

```bash
build_log="$(mktemp /tmp/idf_test_build.XXXXXX.log)"
source /opt/esp/idf/export.sh >/tmp/idf_export.log 2>&1 && idf.py -C test build >"$build_log" 2>&1
tail -n 200 "$build_log"
```

Log retention and cleanup rule:

- Keep temp log files only while actively analyzing a failure.
- Remove when no longer needed:
  - `rm -f "$build_log" /tmp/idf_export.log`

## Agent Handoff Protocol

Use this protocol so any agent can continue work with minimal context loading.

### Claiming

1. Pick one `todo` item with highest priority and no unresolved dependency.
2. Set `Status` to `in_progress`, set `Owner`, set `Last Update` (YYYY-MM-DD).
3. In `Notes`, add:
- `Context:` short current state (1 line)
- `Next:` single next action
- `Blockers:` `none` or short blocker text

### During Work

1. Keep updates compact and factual.
2. If scope expands, add new IDs instead of rewriting existing IDs.
3. If blocked, set `Status` to `blocked` and state unblock condition in `Notes`.

### Handoff

Before stopping work on an item, update:

1. `Evidence`: latest PR/commit/CI/log reference.
2. `Notes`:
- `Done:` what was completed
- `Next:` exact next action for the next agent
- `Risks:` any known regression risk or uncertainty
3. Add a one-line entry in `Activity Log`.

### Done Criteria For Any Agent-Closed Item

- Contract tested at stable boundary (`documentation/TESTING_CHARTER.md`).
- Regression case included for a realistic failure mode.
- Runnable command listed and passing evidence attached.
- Handoff `Next` is either `none` or a linked follow-up ID.

## Dependency Keys

Use these keys in `Notes` when a task depends on another:

- `DEP:HW-*` for hardware matrix dependencies
- `DEP:UT-*` for unit chunk dependencies
- `DEP:CI-*` for CI/workflow dependencies
- `DEP:DOC-*` for required documentation updates

## Activity Log

Append-only, newest first.

| Date | Agent | Item ID | Change | Evidence |
|---|---|---|---|---|
| 2026-02-12 | codex | HW-BOOT-001 | GOAL-002 parked by request; execution deferred until GOAL-001 is implemented and LXD backend is available | `documentation/short-term/coordination/workstream_board.md` |
| 2026-02-12 | codex | HW-BOOT-001 | GOAL-002 WS1 claimed and inventory probe executed; current workspace has no serial devices, so slot mapping remains blocked pending run on LXD HIL host | `test/build/log/hut_slot_inventory_20260212.log` |
| 2026-02-12 | codex | HW-BOOT-001 | Retried with updated IDF instructions; `idf.py -C test build` passed after sourcing `/opt/esp/idf/export.sh`; blocker narrowed to pending HIL execution | `test/build/log/idf_py_stdout_output_20260212_2.log` |
| 2026-02-12 | codex | HW-BOOT-001 | Auto-fast kickoff claimed top P0 hardware item; fast validation blocked by missing local `idf.py` toolchain | `test/build/log/idf_py_missing_20260212.txt` |
| 2026-02-12 | codex | UT-CHUNK-001 | Unblocked test-build path for current IDF and recorded passing fast validation | `test/build/log/idf_py_stdout_output_20413` |
| 2026-02-12 | codex | UT-CHUNK-001 | Added bootstate regression tests; fixed test harness recovery path typo; fast build now blocked on missing `mdns` dependency | `components/tools/test/test_bootstate.cpp`, `test/CMakelists.txt`, `test/build/log/idf_py_stderr_output_2477` |
| 2026-02-12 | codex | UT-CHUNK-001 | Claimed item and initiated guided+freeform fast kickoff | `documentation/agents/integration_test_worklist.md` |
| 2026-02-12 | codex | DOC-TEST-ROADMAP-001 | Added no-prune roadmap and unit-test chunk structure for multi-agent execution | `documentation/agents/integration_test_worklist.md` |

## Comprehensive Roadmap (No-Prune)

This roadmap is intentionally exhaustive. No subsystem is excluded at this stage.

### Layer Definitions

- `U`: unit tests (contract-level logic and error semantics)
- `I`: integration tests (cross-component behavior)
- `H`: hardware/HIL tests (real device and peripheral behavior)
- `S`: soak/endurance and recovery testing

### Full Component Coverage Map

| Component | Required Layers | Must-Hold Contracts (Minimum) | Priority Wave |
|---|---|---|---|
| `audio` | U, I, H | init/play/stop lifecycle stability; no panic on format changes | Wave 1 |
| `codecs` | U, I | decode errors are bounded and recoverable; no invalid memory access on malformed frames | Wave 2 |
| `display` | U, I, H | rendering bounds safety; device init/update robustness | Wave 1 |
| `driver_bt` | U, I, H, S | pair/connect/disconnect stability; recoverable stack restart | Wave 2 |
| `esp_http_server` | U, I | route registration/error handling remains stable under malformed requests | Wave 2 |
| `led_strip` | U, I, H | LED state transitions deterministic; invalid config handled safely | Wave 3 |
| `metrics` | U, I | telemetry payload correctness; metrics publication never blocks critical paths | Wave 2 |
| `platform_config` | U, I | config defaulting and schema validation; malformed payload rejection | Wave 1 |
| `platform_console` | U, I, H | command behavior contracts stable; failure paths return deterministic errors | Wave 2 |
| `raop` | U, I, H | session lifecycle and stream control resilience; error recovery on network churn | Wave 3 |
| `services` | U, I, H | queue/event/state contracts deterministic; no deadlock under pressure | Wave 1 |
| `spotify` | U, I, H | connect/playback lifecycle and error handling remain recoverable | Wave 3 |
| `squeezelite` | U, I, H, S | stream/decode/output stability; underrun/rebuffer recovery | Wave 1 |
| `squeezelite-ota` | U, I, H, S | OTA success/failure/rollback safety; never brick | Wave 1 |
| `targets` | U, I, H | target-specific init and mapping correctness (`i2s`, `muse`, `squeezeamp`) | Wave 2 |
| `telnet` | U, I | command channel lifecycle and invalid input handling | Wave 3 |
| `tjpgd` | U, I | image decode bounds and failure safety | Wave 3 |
| `tools` | U, I | utility and storage helper correctness; safe error handling | Wave 1 |
| `wifi-manager` | U, I, H, S | connection/reconnect/credential flow stability; bounded retries | Wave 1 |
| `_override` | I, H | override behavior compatibility with base driver contracts | Wave 3 |
| `esp-dsp` (vendor) | I, H | integration compatibility and runtime stability only | Wave 3 |
| `spotify/cspot` (vendor) | I, H | integration compatibility and runtime stability only | Wave 3 |
| `telnet/libtelnet` (vendor) | I, H | integration compatibility and runtime stability only | Wave 3 |

### Execution Waves

| Wave | Scope | Exit Criteria |
|---|---|---|
| Wave 1 | Release-critical contracts (`services`, `wifi-manager`, `squeezelite-ota`, `squeezelite`, `platform_config`, `display`, `tools`, `audio`) | Required `P0` chunks complete; no unresolved `P0` regressions |
| Wave 2 | Stability amplification (`metrics`, `platform_console`, `driver_bt`, `targets`, `esp_http_server`, `codecs`) | `P1` chunks for these modules complete; nightly pass signal stable |
| Wave 3 | Extended and compatibility coverage (`raop`, `spotify`, `telnet`, `tjpgd`, `led_strip`, `_override`, vendor integrations) | `P2` chunks and targeted soak coverage complete |

### Required Artifacts Per Completed Chunk

- test file(s) and contract statement
- run command(s) and CI job reference
- pass/fail evidence (logs, run link, or artifact path)
- regression linkage (bug/issue/incident id if applicable)

## Priority Work Queue

| ID | Priority | Status | Test | Platforms | Owner | Last Update | Evidence | Notes |
|---|---|---|---|---|---|---|---|---|
| HW-BOOT-001 | P0 | blocked | Cold boot to operational state | all | - | 2026-02-12 | `documentation/short-term/coordination/workstream_board.md`, `test/build/log/hut_slot_inventory_20260212.log`, `test/build/log/idf_py_stdout_output_20260212_2.log` | Context: GOAL-002 is intentionally parked after accidental kickoff. Done: preserved prior inventory/build evidence and cleared active owner. Next: resume when GOAL-001 is complete and LXD hardware backend is available; then rerun slot inventory and continue WS2/WS3. Risks: none beyond explicit dependency delay. Blockers: DEP:GOAL-001 backend availability prerequisite. |
| HW-BOOT-002 | P0 | todo | Warm reboot loop x50 | all | - | - | - | |
| HW-BOOT-003 | P0 | todo | Platform profile/GPIO sanity | all | - | - | - | |
| HW-STOR-001 | P0 | todo | NVS read/write/reset cycle | all | - | - | - | |
| HW-STOR-003 | P0 | todo | SPIFFS mount + required defaults | all | - | - | - | |
| HW-NET-001 | P0 | todo | Wi-Fi connect + DHCP + DNS | all | - | - | - | |
| HW-NET-002 | P0 | todo | Wi-Fi AP loss/recovery reconnect | all | - | - | - | |
| HW-AUD-001 | P0 | todo | Playback start/stop lifecycle | all | - | - | - | |
| HW-OTA-001 | P0 | todo | OTA happy path | all | - | - | - | |
| HW-OTA-002 | P0 | todo | OTA interrupted update recovery | all | - | - | - | |
| HW-OTA-003 | P0 | todo | Recovery partition entry/exit | all | - | - | - | |
| HW-PWRF-001 | P0 | todo | Power-cut/brownout recovery | all | - | - | - | |
| HW-STOR-002 | P1 | todo | Corrupt/partial NVS recovery | all | - | - | - | |
| HW-NET-003 | P1 | todo | mDNS announce/discover | all | - | - | - | |
| HW-NET-004 | P1 | todo | Ethernet link up/down + DHCP traffic | ethernet-capable | - | - | - | |
| HW-AUD-002 | P1 | todo | Format/rate transitions | all | - | - | - | |
| HW-AUD-003 | P1 | todo | Underrun/rebuffer recovery | all | - | - | - | |
| HW-AUD-004 | P1 | todo | Volume/mute/jack/speaker controls | platform-specific | - | - | - | |
| HW-UI-001 | P1 | todo | Button/rotary/IR input mapping | platform-specific | - | - | - | |
| HW-UI-002 | P1 | todo | Display init + update loop | display-capable | - | - | - | |
| HW-PWR-001 | P1 | todo | Battery telemetry/status logic | battery-capable | - | - | - | |
| HW-BT-001 | P1 | todo | Bluetooth pair/connect/disconnect cycles | bt-enabled | - | - | - | |
| HW-BT-002 | P2 | todo | Bluetooth stack restart/recovery | bt-enabled | - | - | - | |
| HW-SOAK-001 | P2 | todo | 12h playback + periodic reconnect | all | - | - | - | |
| HW-SOAK-002 | P2 | todo | 24h mixed load soak | all | - | - | - | |

## Needed Unit Test Chunks (Short-Lived Backlog)

Purpose: define the minimum unit-test chunks needed now to de-risk integration work. Remove this section once all rows are `done`.

| Chunk ID | Priority | Status | Required Tests | Target Component(s) | Suggested Command | Owner | Last Update | Evidence | Notes |
|---|---|---|---|---|---|---|---|---|---|
| UT-CHUNK-001 | P0 | in_progress | Boot/partition decision logic: normal boot, forced recovery, invalid state fallback | `services`, `bootstate` path in `test_main` | `idf.py -C test build` | codex | 2026-02-12 | `test/build/log/idf_py_stdout_output_20413` | Contract: never enters non-recoverable boot loop. Context: added `components/tools/test/test_bootstate.cpp` and updated test-build compatibility for current IDF/CMake tooling. Done: regression tests for normal counter path, forced recovery threshold boundary (`5`), invalid-state counter normalization (`>100`), and recovery reset semantics; fast validation build now passes. Next: execute/collect runtime Unity test evidence on target for chunk closure. Risks: current evidence is build-pass in fast mode; runtime execution evidence still pending. Blockers: none. |
| UT-CHUNK-002 | P0 | todo | OTA decision and error mapping: success path, transport failure, invalid image metadata | `squeezelite-ota`, `platform_console/cmd_ota` | `idf.py -C test -T tools build` | - | - | - | Contract: failed OTA remains recoverable |
| UT-CHUNK-003 | P0 | todo | Messaging queue contracts: publish/subscribe ordering, timeout behavior, overflow handling | `services/messaging` | `idf.py -C test -T tools build` | - | - | - | Contract: no crash or deadlock on queue pressure |
| UT-CHUNK-004 | P1 | todo | Wi-Fi manager state transitions: connect, reconnect backoff, credential update, failure exhaustion | `wifi-manager` | `idf.py -C test -T wifi-manager build` | - | - | - | Contract: bounded retries and deterministic state |
| UT-CHUNK-005 | P1 | todo | Display text/render boundaries: clipping, wrapping, out-of-bounds coordinates, null font/data guards | `display/core` (`gds_text`, `gds_draw`, `gds_font`) | `idf.py -C test -T tools build` | - | - | - | Contract: renderer never writes outside target buffer |
| UT-CHUNK-006 | P1 | todo | Platform config schema handling: defaulting, unknown fields, malformed payload rejection | `platform_config` | `idf.py -C test -T platform_config build` | - | - | - | Extend existing `components/platform_config/test/` coverage |
| UT-CHUNK-007 | P2 | todo | Input event normalization: button/rotary/IR debounce and duplicate suppression | `services/buttons`, `services/rotary_encoder`, `services/infrared` | `idf.py -C test -T tools build` | - | - | - | Contract: no event storm from bounce/repeat |
| UT-CHUNK-008 | P2 | todo | Battery/telemetry bounds: invalid sensor values, low-battery transitions, status publication | `services/battery`, `metrics` | `idf.py -C test -T tools build` | - | - | - | Contract: invalid telemetry never triggers invalid state loops |

### Chunk Completion Rule

- Each chunk must add at least one regression test for a realistic failure mode.
- Each chunk must reference contract text from `documentation/CONTRACT_TEST_TEMPLATE.md` in PR notes.
- Mark chunk `done` only after test pass evidence is attached.

## Agent Startup Checklist

1. Read only these sections first: `Activity Log`, `Needed Unit Test Chunks`, `Priority Work Queue`.
2. Choose one highest-priority unclaimed item.
3. Claim it using `Agent Handoff Protocol`.
4. Execute targeted tests first; avoid full-matrix runs unless required by the item.
5. Leave a complete handoff entry before ending session.

## Definition Of Done

- Test case exists at a stable contract boundary and follows `documentation/TESTING_CHARTER.md`.
- Platform scope is explicit (`all` or constrained target set).
- Execution path is documented (local command or CI job).
- Pass evidence is linked in `Evidence`.

## Update Example

| ID | Priority | Status | Test | Platforms | Owner | Last Update | Evidence | Notes |
|---|---|---|---|---|---|---|---|---|
| HW-NET-001 | P0 | done | Wi-Fi connect + DHCP + DNS | all | @agent-name | 2026-02-12 | PR #123, CI run #456 | Added regression for reconnect timeout handling |
