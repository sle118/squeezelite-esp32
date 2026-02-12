#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: build-scripts/lint_style.sh [check|format]

Commands:
  check   Verify C/C++ style with clang-format (default)
  format  Apply clang-format in place
EOF
}

MODE="${1:-check}"
if [[ "$MODE" != "check" && "$MODE" != "format" ]]; then
    usage
    exit 2
fi

if ! command -v clang-format >/dev/null 2>&1; then
    echo "clang-format is required but was not found in PATH." >&2
    exit 127
fi

REPO_ROOT="$(git rev-parse --show-toplevel)"
cd "$REPO_ROOT"

declare -a PATHSPECS=(
    "*.c"
    "*.h"
    "*.cc"
    "*.cpp"
    "*.hpp"
    ":(exclude)build/**"
    ":(exclude)managed_components/**"
    ":(exclude)components/codecs/inc/**"
    ":(exclude)components/esp-dsp/**"
    ":(exclude)components/esp_http_server/**"
    ":(exclude)components/spotify/cspot/**"
    ":(exclude)components/squeezelite/**"
    ":(exclude)components/telnet/libtelnet/**"
    ":(exclude)components/tjpgd/**"
    ":(exclude)components/wifi-manager/webapp/dist/**"
    ":(exclude)components/wifi-manager/webapp/src/js/proto/**"
)

mapfile -t FILES < <(git ls-files -- "${PATHSPECS[@]}" | grep -E '^(components|main|test)/.*\.(c|h|cc|cpp|hpp)$' || true)

if [[ ${#FILES[@]} -eq 0 ]]; then
    echo "No C/C++ files matched lint scope."
    exit 0
fi

if [[ "$MODE" == "format" ]]; then
    printf '%s\0' "${FILES[@]}" | xargs -0 -r clang-format -i
    echo "Formatted ${#FILES[@]} file(s)."
    exit 0
fi

declare -a FAILING=()
for file in "${FILES[@]}"; do
    if ! clang-format --dry-run --Werror "$file" >/dev/null 2>&1; then
        FAILING+=("$file")
    fi
done

if [[ ${#FAILING[@]} -gt 0 ]]; then
    echo "Style check failed in ${#FAILING[@]} file(s)."
    echo "First failing files:"
    printf '  - %s\n' "${FAILING[@]:0:30}"
    echo "Run: build-scripts/lint_style.sh format"
    exit 1
fi

echo "Style check passed for ${#FILES[@]} file(s)."
