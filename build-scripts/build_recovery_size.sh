#!/usr/bin/env bash
set -euo pipefail

source /opt/esp/idf/export.sh >/dev/null 2>&1

BUILD_DIR="${1:-build-recovery-trim}"
DEFAULTS="sdkconfig.defaults;sdkconfig.recovery.defaults"

# Build only the recovery ELF (skip squeezelite.elf target).
idf.py -B "${BUILD_DIR}" -D SDKCONFIG_DEFAULTS="${DEFAULTS}" recovery.elf

# Generate a standalone recovery.bin from the built ELF.
python /opt/esp/idf/components/esptool_py/esptool/esptool.py \
  --chip esp32 elf2image -o "${BUILD_DIR}/recovery.bin" "${BUILD_DIR}/recovery.elf" >/dev/null

# Print recovery image size summary from map.
python /opt/esp/idf/tools/idf_size.py "${BUILD_DIR}/recovery.map"

# Print partition fit/overflow status for recovery.bin.
python /opt/esp/idf/components/partition_table/check_sizes.py \
  --offset 0x8000 partition --type app \
  "${BUILD_DIR}/partition_table/partition-table.bin" \
  "${BUILD_DIR}/recovery.bin" || true
