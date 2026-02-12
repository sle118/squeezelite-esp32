# Hardware Test Matrix

Date: 2026-02-12  
Scope: Platform-specific stability and release validation for `squeezelite-esp32`.

## Purpose

Define a practical hardware-focused test strategy that complements host/unit tests and acknowledges embedded uncertainty under real-world load.

This matrix is designed to:
- maximize platform stability
- prioritize high-impact failures first
- provide clear release gates per hardware profile

Use together with:
- `documentation/TESTING_CHARTER.md`
- `documentation/CONTRACT_TEST_TEMPLATE.md`

## Platforms

Mapped to build profiles used in this repo:
- `i2s` -> `build-scripts/I2S-4MFlash-sdkconfig.defaults`
- `muse` -> `build-scripts/Muse-sdkconfig.defaults`
- `squeezeamp` -> `build-scripts/SqueezeAmp-sdkconfig.defaults`

## Priority Levels

- `P0`: release blocking, must pass
- `P1`: strong confidence tests, expected to pass before release
- `P2`: extended coverage/soak, required for nightly and milestone validation

## Execution Tiers

- PR smoke: fast subset (`P0` only, selected `P1`)
- Nightly: full `P0 + P1` and selected `P2`
- Pre-release: full matrix, all `P0/P1`, mandatory `P2` soak and fault-injection

## Core Hardware Matrix

| ID | Area | Test Path | Priority | Platforms | Pass Criteria |
|---|---|---|---|---|---|
| HW-BOOT-001 | Boot | Cold boot to operational state | P0 | all | boots successfully within target time budget; no panic/reset loop |
| HW-BOOT-002 | Boot | Warm reboot (`esp_restart`) loop x50 | P0 | all | no stuck boot, no boot partition confusion, stable counters |
| HW-BOOT-003 | Boot identity | Platform profile and key GPIO map sanity | P0 | all | expected `FW_PLATFORM_NAME`, expected peripheral init paths |
| HW-STOR-001 | Storage/NVS | NVS read/write/reset cycle | P0 | all | persisted values survive reboot, reset restores defaults |
| HW-STOR-002 | Storage/NVS | Corrupt/partial NVS handling | P1 | all | graceful recovery path, no crash, defaults restored or fallback used |
| HW-STOR-003 | SPIFFS | SPIFFS mount + defaults file read | P0 | all | mount succeeds, required files loaded, clear error if missing |
| HW-NET-001 | Wi-Fi | AP connect + DHCP + DNS | P0 | all | connected state reached, IP assigned, DNS query success |
| HW-NET-002 | Wi-Fi resilience | AP loss/recovery reconnect | P0 | all | reconnect within SLA, no task deadlock or watchdog |
| HW-NET-003 | Service discovery | mDNS announce/discover | P1 | all | service visible on LAN; no crash if mDNS unavailable |
| HW-NET-004 | Ethernet | Link up/down + DHCP + traffic | P1 | ethernet-capable | state transitions handled cleanly; no memory growth trend |
| HW-AUD-001 | Audio output | Start/stop playback, no signal path errors | P0 | all | successful init and playback lifecycle, no panic |
| HW-AUD-002 | Audio behavior | Format/rate transitions during playback | P1 | all | transitions without hard fault; bounded glitch behavior |
| HW-AUD-003 | Audio resilience | Underrun/rebuffer recovery | P1 | all | playback recovers, no runaway CPU or deadlock |
| HW-AUD-004 | Controls | Volume/mute/jack/speaker path checks | P1 | platform-specific | control actions reflected correctly in output state |
| HW-UI-001 | Inputs | Button/rotary/IR event mapping | P1 | platform-specific | expected commands dispatched; no ghost/repeat storm |
| HW-UI-002 | Display | Display init + update loop | P1 | display-capable | stable render/update path, no crash under repeated updates |
| HW-PWR-001 | Battery/charger | Battery telemetry and status logic | P1 | battery-capable | values in expected range; no invalid-state loop |
| HW-BT-001 | Bluetooth | Pair/connect/disconnect cycles | P1 | bt-enabled | stable lifecycle across repeated cycles |
| HW-BT-002 | Bluetooth resilience | BT stack restart/recovery | P2 | bt-enabled | stack recovers without reboot or hard fault |
| HW-OTA-001 | OTA | Happy-path OTA update | P0 | all | new image boots and reports expected version |
| HW-OTA-002 | OTA failure | Interrupted OTA (network/power cut) | P0 | all | safe fallback/rollback path works; device recoverable |
| HW-OTA-003 | Recovery partition | Recovery boot and exit path | P0 | all | recovery mode entry/exit consistent and deterministic |
| HW-PWRF-001 | Power fault | Brownout/power-cut during write/OTA | P0 | all | no bricking; deterministic recovery behavior |
| HW-SOAK-001 | Soak | 12h playback + periodic reconnect | P2 | all | no crash/reset; memory trend within threshold |
| HW-SOAK-002 | Soak | 24h mixed load (stream/control/network churn) | P2 | all | no regressions in responsiveness and stability |

## Platform-Specific Focus

## `i2s` (generic baseline)

- Emphasize:
  - audio path stability under varied sample rates
  - generic GPIO/peripheral defaults
  - OTA + recovery baseline behavior
- Required:
  - all `P0`
  - `HW-AUD-002`, `HW-AUD-003`, `HW-SOAK-001`

## `muse` (portable/battery-centric)

- Emphasize:
  - battery telemetry, charger events, low-power edge conditions
  - UI/display responsiveness under battery/network churn
  - Wi-Fi reconnect after sleep-like or low-power transitions
- Required:
  - all `P0`
  - `HW-PWR-001`, `HW-UI-001`, `HW-UI-002`, `HW-SOAK-001`

## `squeezeamp` (amp/control-centric)

- Emphasize:
  - amplifier-related controls, output routing, jack/speaker transitions
  - thermal/long-play stability proxies (extended playback)
  - network resilience during active playback
- Required:
  - all `P0`
  - `HW-AUD-004`, `HW-NET-002`, `HW-SOAK-001`, `HW-SOAK-002`

## Release Gates

A release is eligible only if:
- every target platform passes all `P0` tests
- no unresolved regression in previously passing `P1` tests
- at least one `P2` soak run per platform completed within release window
- OTA/recovery tests (`HW-OTA-001/002/003`) pass on all platforms

## Observability Requirements During Tests

Collect at minimum:
- reset reason, panic reason, boot count, recovery boot count
- heap free/minimum and largest block trend
- task stack high-water marks for critical tasks
- network reconnect counters and durations
- OTA stage/result events

A test can be marked "pass with warning" only for non-`P0` tests and only with a filed follow-up issue and owner.

## Failure Triage Rules

- `P0` fail: immediate release block
- repeated `P1` fail on same platform: treat as release risk, escalate to block unless waived
- any soak fail with crash/reset: open defect with logs and reproduction steps before next release candidate

## Suggested First Implementation Wave

1. Automate all `P0` checks for each platform profile.
2. Add nightly `P1` network/audio/UI subset.
3. Add 12h soak (`HW-SOAK-001`) with telemetry capture and trend thresholds.
4. Expand to full pre-release matrix once stable signal quality is established.
