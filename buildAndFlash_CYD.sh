#!/usr/bin/env bash
set -euo pipefail

# Build / flash ESP32-2432S028 (CYD classic) + NM-RF-HAT (ILI9341)

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
EXPORT_SCRIPT="${ESP_IDF_EXPORT_SCRIPT:-${HOME}/esp/esp-idf/export.sh}"

PORT="${ESPPORT:-}"
RUN_MONITOR=0
BUILD_ONLY=0

BOARD="esp32_cyd_nm_rf_hat"
BUILD_DIR="build_cyd"
IDF_TARGET="esp32"
SDK_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32_cyd"

usage() {
    cat <<EOF
Usage: $(basename "$0") [--port <device>] [--monitor] [--build-only]

Builds and flashes the Flipper Zero ESP32 port for:
  ESP32-2432S028 (CYD classic, ILI9341) + NM-RF-HAT

Options:
  --port <device>  Serial device (default: auto-detect /dev/ttyUSB* / /dev/ttyACM*)
  --monitor        Open idf.py monitor after flash
  --build-only     Build only, skip flash

Environment:
  ESPPORT                  Override serial device
  ESP_IDF_EXPORT_SCRIPT    Override IDF export.sh path
EOF
}

detect_port() {
    local matches=()
    shopt -s nullglob
    matches=(/dev/ttyUSB* /dev/ttyACM* /dev/cu.usbserial* /dev/cu.usbmodem*)
    shopt -u nullglob
    if [[ "${#matches[@]}" -eq 1 ]]; then
        printf '%s\n' "${matches[0]}"
    elif [[ "${#matches[@]}" -gt 1 ]]; then
        echo "Multiple ports found: ${matches[*]}. Specify with --port." >&2
        return 1
    else
        [[ "${BUILD_ONLY}" -eq 0 ]] && echo "No serial device found." >&2
        return 1
    fi
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -p|--port)    PORT="$2"; shift 2 ;;
        -m|--monitor) RUN_MONITOR=1; shift ;;
        --build-only) BUILD_ONLY=1; shift ;;
        -h|--help)    usage; exit 0 ;;
        *) echo "Unknown: $1"; usage; exit 1 ;;
    esac
done

if [[ -z "${PORT}" && "${BUILD_ONLY}" -eq 0 ]]; then
    PORT="$(detect_port || echo "")"
    [[ -z "${PORT}" ]] && exit 1
fi

[[ ! -f "${EXPORT_SCRIPT}" ]] && echo "IDF export script missing: ${EXPORT_SCRIPT}" >&2 && exit 1
# shellcheck source=/dev/null
source "${EXPORT_SCRIPT}"

cd "${SCRIPT_DIR}"
export IDF_TARGET="${IDF_TARGET}"
export FLIPPER_BOARD="${BOARD}"

# Drop stale root/build sdkconfig when chip OR board defaults changed.
# (Defaults like FREERTOS_USE_TRACE_FACILITY only apply on fresh sdkconfig.)
if [[ -f "sdkconfig" ]]; then
    CURRENT=$(grep -oP '(?<=CONFIG_IDF_TARGET=")[^"]+' sdkconfig 2>/dev/null || echo "")
    if [[ -z "${CURRENT}" || "${CURRENT}" != "${IDF_TARGET}" ]]; then
        echo "Root sdkconfig target mismatch ('${CURRENT}' vs '${IDF_TARGET}'); removing..."
        rm -f sdkconfig
    fi
fi
if [[ -f "${BUILD_DIR}/sdkconfig" ]]; then
    BD=$(grep -oP '(?<=CONFIG_IDF_TARGET=")[^"]+' "${BUILD_DIR}/sdkconfig" 2>/dev/null || echo "")
    TRACE=$(grep -c 'CONFIG_FREERTOS_USE_TRACE_FACILITY=y' "${BUILD_DIR}/sdkconfig" 2>/dev/null || echo 0)
    if [[ -z "${BD}" || "${BD}" != "${IDF_TARGET}" ]]; then
        echo "Build-dir sdkconfig mismatch; removing ${BUILD_DIR}/sdkconfig..."
        rm -f "${BUILD_DIR}/sdkconfig"
    elif [[ "${TRACE}" -eq 0 ]]; then
        echo "Build-dir sdkconfig missing TRACE_FACILITY; regenerating..."
        rm -f "${BUILD_DIR}/sdkconfig" sdkconfig
    fi
fi

# Common cmake options for every idf.py invocation — critical so set-target
# does not cache the default Waveshare board.
CMAKE_OPTS=(
    -B "${BUILD_DIR}"
    -DFLIPPER_BOARD="${BOARD}"
    -DSDKCONFIG_DEFAULTS="${SDK_DEFAULTS}"
)

echo "Building board=${BOARD} target=${IDF_TARGET} build_dir=${BUILD_DIR}"

idf.py "${CMAKE_OPTS[@]}" set-target "${IDF_TARGET}"

COMMANDS=(reconfigure build)
if [[ "${BUILD_ONLY}" -eq 0 ]]; then
    COMMANDS+=(flash)
    CMAKE_OPTS+=(-p "${PORT}")
    [[ "${RUN_MONITOR}" -eq 1 ]] && COMMANDS+=(monitor)
fi

idf.py "${CMAKE_OPTS[@]}" "${COMMANDS[@]}"
