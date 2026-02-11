# ESP-IDF v5.5 Migration Remediation Impacts

Date: 2026-02-11  
Branch: `refactoring`  
Scope: Container-first migration and iterative build remediation on ESP-IDF v5.5 baseline.

## Purpose

This document records the impacts that have already been remediated during the migration to the new container/IDF baseline.  
It focuses on remediation outcomes (what broke, what was fixed, and where).

## Remediated Impact Areas

## 1) Container/CI baseline alignment

- Impact:
  - Firmware and UI workflows were not aligned on a single ESP-IDF/container baseline.
- Mitigation:
  - Unified workflows and docker tooling toward the IDF v5.5 container baseline.
  - Updated contributor guidance to container-first while preserving native setup path.
- Primary files:
  - `.github/workflows/Platform_build.yml`
  - `.github/workflows/CrossBuild.yml`
  - `.github/workflows/esp-idf-v4.3-build.yml`
  - `.github/workflows/web_deploy.yml`
  - `Dockerfile`
  - `docker/build_tools.py`
  - `README.md`

## 2) Component dependency and CMake modernization

- Impact:
  - Legacy component dependency assumptions caused configure/build incompatibilities on IDF 5.5.
- Mitigation:
  - Added/updated component requirements and removed obsolete dependencies.
  - Updated component-level `REQUIRES`/`PRIV_REQUIRES` for headers now split into dedicated components.
- Primary files:
  - `main/idf_component.yml`
  - `main/CMakeLists.txt`
  - `components/squeezelite/CMakeLists.txt`
  - `components/services/CMakeLists.txt`
  - `components/metrics/CMakeLists.txt`
  - `components/platform_console/CMakeLists.txt`
  - `components/platform_console/app_recovery/CMakeLists.txt`
  - `components/esp_http_server/CMakeLists.txt`

## 3) Ethernet stack migration (SPI + RMII + netif integration)

- Impact:
  - IDF 5.5 changed Ethernet init/config APIs and SPI-Ethernet setup flow.
- Mitigation:
  - Updated SPI Ethernet config macros to new signatures.
  - Migrated RMII MAC/PHY constructor usage.
  - Updated `esp_mac` usage and timer callback signatures in Ethernet manager.
  - Removed obsolete SPI device attach path where IDF now handles internals in driver flow.
- Primary files:
  - `components/wifi-manager/network_driver_W5500.c`
  - `components/wifi-manager/network_driver_DM9051.c`
  - `components/wifi-manager/network_driver_LAN8720.c`
  - `components/wifi-manager/network_ethernet.c`
  - `components/wifi-manager/network_services.h`
  - `components/wifi-manager/network_manager.h`
  - `components/raop/util.c`
  - `components/squeezelite-ota/protocol_examples_common.h`
  - `components/tools/tcpip_adapter_compat.h`

## 4) FreeRTOS legacy API/type removal

- Impact:
  - Build failures due to removed/deprecated compatibility symbols (`portTICK_RATE_MS`, legacy queue/tick types, callback signatures).
- Mitigation:
  - Replaced `portTICK_RATE_MS` with `portTICK_PERIOD_MS`.
  - Updated queue/tick types and timer callback prototypes to current FreeRTOS API.
- Primary files:
  - `components/services/led.c`
  - `components/services/buttons.c`
  - `components/services/battery.c`
  - `components/services/audio_controls.c`
  - `components/services/infrared.c`
  - `components/services/gpio_exp.c`
  - `components/platform_console/cmd_i2ctools.c`
  - `components/platform_console/cmd_wifi.c`
  - `components/services/services.c`
  - `components/driver_bt/bt_app_core.c`
  - `components/driver_bt/bt_app_source.c`

## 5) GPIO/pad selection API updates

- Impact:
  - Legacy GPIO pad selection symbols produced missing symbol/implicit declaration failures on IDF 5.5.
- Mitigation:
  - Migrated to `esp_rom_gpio_pad_select_gpio(...)` path and aligned wrapper macro behavior.
  - Added explicit include coverage where needed.
- Primary files:
  - `components/services/gpio_exp.h`
  - `components/services/gpio_exp.c`
  - `components/services/services.c`
  - `components/squeezelite/output_i2s.c`
  - `components/display/SSD1675.c`

## 6) Strict compiler checks and type-width corrections (`-Werror`)

- Impact:
  - New toolchain and stricter flags surfaced format-string and pointer-width mismatches.
- Mitigation:
  - Fixed `printf`/`snprintf`/`sscanf` format mismatches.
  - Switched to width-safe intermediates/casts and correct callback argument types.
  - Corrected size-type usage in SPIFFS info reporting and metadata parsing.
- Primary files:
  - `components/services/monitor.c`
  - `components/tools/tools_spiffs_utils.cpp`
  - `components/tools/tools.c`
  - `components/squeezelite/alac.c`
  - `components/squeezelite/helix-aac.c`
  - `components/squeezelite/mad.c`
  - `components/squeezelite/decode_external.c`
  - `components/raop/raop.c` (partial type-format cleanup)

## 7) Header hygiene and removed transitive includes

- Impact:
  - IDF 5.5 no longer transitively includes some headers (for example MAC APIs), and some internal/private headers are no longer valid.
- Mitigation:
  - Added explicit includes where required (`esp_mac.h`, etc.).
  - Removed stale/internal header use in multiple components.
- Primary files:
  - `components/wifi-manager/network_ethernet.c`
  - `components/tools/tools.c`
  - `components/squeezelite/embedded.c`
  - `components/tools/bootstate.cpp`
  - `components/services/accessors.c`
  - `components/telnet/telnet.c`
  - `components/services/messaging.c`
  - `components/tools/trace.c`

## 8) Protobuf/Nanopb tooling stability in container

- Impact:
  - Generator and plugin execution flow had environment/shebang/version conflicts in the new container baseline.
- Mitigation:
  - Fixed Python shebangs for container runtime.
  - Improved protobuf package checks and version constraints.
  - Corrected nanopb/protoc lookup behavior to prefer valid compiler binary.
- Primary files:
  - `tools/protoc_utils/check_python_packages.py`
  - `tools/protoc_utils/ProtocParser.py`
  - `tools/protoc_utils/parse_bin.py`
  - `tools/protoc_utils/generate_bin.py`
  - `tools/protoc_utils/protoc-gen-defaults.py`
  - `tools/protoc_utils/protoc-gen-dump.py`
  - `tools/protoc_utils/protoc-gen-json.py`
  - `tools/protoc_utils/protoc-gen-options.py`
  - `components/spotify/cspot/bell/external/nanopb/extra/FindNanopb.cmake`

## 9) SPIFFS/path and partition constraints

- Impact:
  - Asset/image generation and partition validation failed under new checks.
- Mitigation:
  - Increased SPIFFS object-name length where required.
  - Updated partition flags to satisfy stricter partition-table checks.
- Primary files:
  - `sdkconfig`
  - `build-scripts/I2S-4MFlash-sdkconfig.defaults`
  - `build-scripts/Muse-sdkconfig.defaults`
  - `build-scripts/SqueezeAmp-sdkconfig.defaults`
  - `partitions.csv`

## 10) Additional compile/runtime compatibility fixes

- Impact:
  - Multiple compile breaks from deprecated/changed API surfaces and C/C++ correctness issues.
- Mitigation:
  - Added missing includes.
  - Updated legacy flash size call path.
  - Fixed string and initialization issues and allocator mismatch in ESP-DSP sources.
  - Added HTTP redirect event handling for OTA flow.
- Primary files:
  - `components/platform_console/cmd_system.c`
  - `components/platform_console/app_recovery/recovery.c`
  - `components/squeezelite-ota/squeezelite-ota.c`
  - `components/squeezelite-ota/squeezelite-ota.h`
  - `components/spotify/cspot/bell/main/io/URLParser.cpp`
  - `components/metrics/Batch.h`
  - `components/esp-dsp/modules/support/snr/float/dsps_snr_f32.cpp`
  - `components/esp-dsp/modules/support/sfdr/float/dsps_sfdr_f32.cpp`
  - `components/esp-dsp/modules/support/view/dsps_view.cpp`

## 11) Connectivity and service-layer compatibility

- Impact:
  - Compatibility gaps around network utility functions and handler glue code.
- Mitigation:
  - Added tcpip adapter compatibility shims and network helper updates.
  - Updated network/address conversion helper usage for current lwIP/IDF behavior.
- Primary files:
  - `components/tools/tcpip_adapter_compat.h`
  - `components/wifi-manager/http_server_handlers.c`
  - `components/wifi-manager/network_manager.c`
  - `components/wifi-manager/network_services.h`

## 12) RAOP mbedTLS v3 migration + late-stage build hardening

- Impact:
  - RAOP RSA flow used legacy mbedTLS APIs removed/changed in IDF 5.5.
  - Additional strict-format and `-fno-common`/linkage issues appeared after earlier compile blockers were removed.
- Mitigation:
  - Migrated RAOP RSA key parse/sign/decrypt flow to mbedTLS v3-compatible APIs and added RNG callback/error handling.
  - Corrected display time formatting width mismatches.
  - Updated Wi-Fi auth-mode switch mapping for new IDF enums and fixed RSSI format typing.
  - Fixed multiple-definition issues by converting header-level globals to `extern` declarations and adding single C definitions.
  - Resolved duplicate `loglevel` definition in squeezelite decode path.
- Primary files:
  - `components/raop/raop.c`
  - `components/display/display.c`
  - `components/platform_config/WifiList.cpp`
  - `components/squeezelite/embedded.h`
  - `components/squeezelite/slimproto.c`
  - `components/display/display.h`
  - `components/display/core/gds_font.h`
  - `components/squeezelite/decode.c`

## Current Status

- Build progress has moved significantly forward versus initial migration state.
- RAOP mbedTLS migration blocker in `components/raop/raop.c` is remediated and RAOP now compiles/links.
- Most broad migration impacts above are remediated.
- Remaining blockers have moved to final link/image stages:
  - unresolved symbol groups in final `squeezelite.elf` link (network/PPP/mbedTLS TLS13/WPS/BT/RTC symbol families)
  - binary/footprint constraints (IRAM pressure hint and image-size pressure hint)
  - `recovery.bin` partition overflow warning (`recovery` partition too small for current binary)
- Latest investigated logs:
  - `build/log/idf_py_stderr_output_52883`
  - `build/log/idf_py_stdout_output_52883`

## Notes for Contributors

- This file documents remediated impacts only.
- For open blockers, refer to latest in-container build logs under:
  - `build/log/idf_py_stderr_output_*`
  - `build/log/idf_py_stdout_output_*`
