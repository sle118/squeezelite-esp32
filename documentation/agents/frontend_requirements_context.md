# Frontend Requirements Context (Living)

Status: Active  
Last reviewed: 2026-02-12  
Review scope: `components/wifi-manager/webapp`, `components/wifi-manager/http_server_handlers.c`, `components/wifi-manager/wifi_manager_http_server.c`, `spiffs_src/CMakeLists.txt`

## Purpose

This document captures reverse-engineered **product requirements** for the embedded web UI, with measurable constraints.  
It is intentionally not an implementation walkthrough.

Use this as session handoff context for UI refactoring and footprint control.

## Snapshot Findings (Current State)

### 1. What actually ships to firmware

SPIFFS packaging currently copies only:

- `components/wifi-manager/webapp/dist/*.gz`
- `components/wifi-manager/webapp/dist/*.png`

Evidence: `spiffs_src/CMakeLists.txt`.

Current shipped web payload:

- Total shipped bytes: `219741` (`~214.6 KiB`, `~0.21 MiB`)
- Largest asset: `dist/js/node_vendors.bundle.js.gz` (`166729` bytes)

### 2. What looks large but is not currently shipped

- `dist/` total is about `4.5M`, dominated by source maps (`*.map`).
- Source maps are large in repo artifacts, but not copied to SPIFFS by current packaging.

### 3. Major technical debt signal

- UI source references many legacy endpoints (`/status.zzz`, `/messages.zzz`, `/commands.zzz`, etc.).
- Active server registration in `wifi_manager_http_server.c` is centered on `/data.bin` + static handlers; many legacy handlers are commented out.

This indicates API/contract drift and unclear migration state.

### 4. Modularity and proto usage signal

- Main UI logic is largely monolithic in `src/js/custom.ts` (~2.5k LOC).
- `index.ts` imports generated protobuf bundle (`configuration_pb.js`) up front.
- `dist/js/index.bundle.js` includes many generated proto message definitions, not only request/response minimum set.

Implication: upfront JS parse/execute and bundle weight are likely higher than needed.

## Reverse-Engineered Product Requirements

These are the requirements we should preserve while refactoring.

### RQ-UI-001 Captive Portal Bootstrap

The UI must reliably open and render from AP/captive-portal entry points with no manual URL knowledge.

Acceptance:

- Landing page reachable from captive portal probes.
- First meaningful controls visible without requiring cloud services.

### RQ-UI-002 Network Onboarding and Recovery

Users must be able to scan APs, join/disconnect Wi-Fi, and understand current link mode (Wi-Fi vs Ethernet) with clear status.

Acceptance:

- Scan, connect, disconnect workflows remain available and understandable.
- Connection state transitions are visible and unambiguous.

### RQ-UI-003 Runtime Observability

Users must see operational status and logs needed for diagnosis (network, playback, OTA progress, errors).

Acceptance:

- Status view remains available in normal and recovery mode.
- Error and warning paths are visible in UI without dev tools.

### RQ-UI-004 Safe Configuration Editing

Users must be able to view/edit/apply configuration with guardrails (validation feedback, clear commit/apply behavior).

Acceptance:

- Config edit path supports both selective and global update actions.
- Validation errors are explicit before/after submit.

### RQ-UI-005 OTA Operations

Users must be able to trigger firmware update by URL or upload, with progress and failure feedback.

Acceptance:

- OTA action always yields deterministic status progression or explicit terminal error.
- Recovery path remains accessible if normal OTA path fails.

### RQ-UI-006 Advanced Control Access

Power-user controls (commands/NVS/diagnostics) must exist but should not degrade common-task UX.

Acceptance:

- Advanced functions remain accessible.
- Main tasks are not blocked by advanced UI complexity.

### RQ-UI-007 Backward Compatibility Envelope

The UI must support currently deployed firmware API variants during migration (legacy and protobuf-era endpoints), with explicit deprecation strategy.

Acceptance:

- Contract compatibility matrix is documented and tested.
- Legacy paths have clear sunset criteria.

### RQ-UI-008 Embedded Footprint Discipline

Web assets must stay within explicit firmware budget and be measurable in CI.

Acceptance:

- Shipping budget exists and is enforced.
- Size regressions fail checks unless explicitly approved.

## Non-Functional Budgets (Proposed)

These are targets, not yet enforced:

- `NB-001` Shipped web payload (`*.gz` + `*.png` in `dist/`) <= `180 KiB` target, `220 KiB` hard cap.
- `NB-002` Main JS gzip <= `120 KiB` target.
- `NB-003` Vendor JS gzip <= `120 KiB` target.
- `NB-004` No runtime dependency on external CDNs for critical UI function.
- `NB-005` One canonical API transport for new features (`/data.bin` protobuf), with compatibility shim for legacy endpoints during migration only.

## UX + Footprint Improvement Directions

Prioritized by impact/risk.

### P1 Contract Unification (High impact, medium risk)

- Define one frontend API client contract around protobuf request/response.
- Treat legacy `.zzz` paths as compatibility layer, not primary path.
- Add explicit compatibility table (firmware version -> supported endpoints).

### P2 Modularization by Capability (High impact, medium risk)

- Split monolithic UI logic into capability modules:
  - onboarding
  - status/logs
  - config editor
  - OTA
  - advanced
- Lazy-load non-critical modules (advanced, release browser, heavy editors).

Expected result: lower startup JS cost and clearer ownership boundaries.

### P3 Proto Surface Minimization (High impact, medium risk)

- Stop importing broad generated config surface on initial load.
- Create thin protobuf DTO layer for startup-critical flows.
- Load heavy schema modules only when entering related screens.

### P4 UI Responsiveness and Clarity (Medium impact, low risk)

- Replace mixed polling patterns with a single scheduler and explicit backoff policy.
- Standardize in-progress/error/success state rendering across all actions.
- Reduce ambiguous icon-only cues with text fallback in constrained/offline contexts.

### P5 Build Output Hygiene (Medium impact, low risk)

- Keep generating sourcemaps for dev/debug if needed, but separate from shipping artifact workflow.
- Enforce shipped-size budget on what SPIFFS actually receives, not raw `dist/`.

## Living Maintenance Protocol

This file must be updated when any of the following changes:

- web routes/endpoints
- protobuf request/response schema used by UI
- shipped asset packaging rules
- significant UI feature flow

Required update steps:

1. Run `build-scripts/ui_footprint_snapshot.sh`.
2. Update Snapshot Findings and budget deltas in this file.
3. Update compatibility notes if API contract changed.
4. Add an entry to Decision Log.

## Decision Log

| Date | Decision | Why | Follow-up |
|------|----------|-----|-----------|
| 2026-02-12 | Treat this file as requirements-first context, not implementation notes | Reduce drift and speed up session handoffs | Add CI size gate and API contract checks |

## Open Questions

- Are legacy `.zzz` endpoints still required for deployed firmware versions, and for which versions exactly?
- Is the intended long-term model pure protobuf over `/data.bin` for all dynamic data?
- What startup latency target is acceptable on AP-mode captive portal connections?
