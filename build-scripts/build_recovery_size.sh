#!/usr/bin/env bash
set -euo pipefail

source /opt/esp/idf/export.sh >/dev/null 2>&1

usage() {
  cat <<'EOF'
Usage:
  build-scripts/build_recovery_size.sh <platform|defaults-file> [build-dir]

Platform aliases:
  i2s        -> build-scripts/I2S-4MFlash-sdkconfig.defaults
  muse       -> build-scripts/Muse-sdkconfig.defaults
  squeezeamp -> build-scripts/SqueezeAmp-sdkconfig.defaults

Examples:
  build-scripts/build_recovery_size.sh muse
  build-scripts/build_recovery_size.sh i2s build-recovery-i2s
  build-scripts/build_recovery_size.sh build-scripts/Muse-sdkconfig.defaults
EOF
}

if [[ "${1:-}" == "" ]] || [[ "${1:-}" == "-h" ]] || [[ "${1:-}" == "--help" ]]; then
  usage
  exit 1
fi

PLATFORM_INPUT="$1"
BUILD_DIR="${2:-}"

case "${PLATFORM_INPUT,,}" in
  i2s)
    PLATFORM_DEFAULTS="build-scripts/I2S-4MFlash-sdkconfig.defaults"
    ;;
  muse)
    PLATFORM_DEFAULTS="build-scripts/Muse-sdkconfig.defaults"
    ;;
  squeezeamp)
    PLATFORM_DEFAULTS="build-scripts/SqueezeAmp-sdkconfig.defaults"
    ;;
  *)
    PLATFORM_DEFAULTS="${PLATFORM_INPUT}"
    ;;
esac

if [[ ! -f "${PLATFORM_DEFAULTS}" ]]; then
  echo "Platform defaults file not found: ${PLATFORM_DEFAULTS}" >&2
  usage
  exit 2
fi

if [[ -z "${BUILD_DIR}" ]]; then
  base="$(basename "${PLATFORM_DEFAULTS}")"
  base="${base%-sdkconfig.defaults}"
  BUILD_DIR="build-recovery-${base}"
fi

DEFAULTS="${PLATFORM_DEFAULTS};sdkconfig.recovery.defaults"
echo "Using SDKCONFIG_DEFAULTS=${DEFAULTS}"
echo "Build directory: ${BUILD_DIR}"

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
