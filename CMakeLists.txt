cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "components")

# ---------------------------------------------------------------------------
# Patch libnet80211.a to allow raw TX of deauth/disassoc management frames.
# The closed-source WiFi blob rejects frame types 0xC0/0xA0; we patch
# ieee80211_raw_frame_sanity_check() to return 0. Idempotent — skips if
# already patched.  Runs at configure time (before library path resolution).
# Target-aware: classic ESP32 / S3 / C6 each have their own lib path + ar.
# ---------------------------------------------------------------------------
if(NOT DEFINED IDF_TARGET AND DEFINED ENV{IDF_TARGET})
    set(IDF_TARGET "$ENV{IDF_TARGET}")
endif()
if(NOT DEFINED IDF_TARGET)
    set(IDF_TARGET "esp32s3")
endif()

set(_wifi_lib "$ENV{IDF_PATH}/components/esp_wifi/lib/${IDF_TARGET}/libnet80211.a")
if(IDF_TARGET STREQUAL "esp32s3" OR IDF_TARGET STREQUAL "esp32")
    set(_xt_prefix "xtensa-${IDF_TARGET}-elf")
elseif(IDF_TARGET STREQUAL "esp32c6" OR IDF_TARGET STREQUAL "esp32c3")
    set(_xt_prefix "riscv32-esp-elf")
else()
    set(_xt_prefix "xtensa-esp32s3-elf")
endif()
find_program(_xt_ar NAMES ${_xt_prefix}-ar
    PATHS "$ENV{HOME}/.espressif/tools/xtensa-esp-elf/*/xtensa-esp-elf/bin"
          "$ENV{HOME}/.espressif/tools/riscv32-esp-elf/*/riscv32-esp-elf/bin"
    NO_DEFAULT_PATH)
find_program(_xt_objcopy NAMES ${_xt_prefix}-objcopy
    PATHS "$ENV{HOME}/.espressif/tools/xtensa-esp-elf/*/xtensa-esp-elf/bin"
          "$ENV{HOME}/.espressif/tools/riscv32-esp-elf/*/riscv32-esp-elf/bin"
    NO_DEFAULT_PATH)
# Fallback: toolchain is often named xtensa-esp-elf-* for both esp32 and s3
if(NOT _xt_ar)
    find_program(_xt_ar NAMES xtensa-esp-elf-ar
        PATHS "$ENV{HOME}/.espressif/tools/xtensa-esp-elf/*/xtensa-esp-elf/bin" NO_DEFAULT_PATH)
endif()
if(NOT _xt_objcopy)
    find_program(_xt_objcopy NAMES xtensa-esp-elf-objcopy
        PATHS "$ENV{HOME}/.espressif/tools/xtensa-esp-elf/*/xtensa-esp-elf/bin" NO_DEFAULT_PATH)
endif()

if(_xt_ar AND _xt_objcopy AND EXISTS "${_wifi_lib}")
    execute_process(
        COMMAND "${CMAKE_SOURCE_DIR}/tools/patch_wifi_lib.py"
            "${_wifi_lib}" "${_xt_objcopy}" "${_xt_ar}" "${_wifi_lib}"
        RESULT_VARIABLE _patch_rc OUTPUT_VARIABLE _patch_out ERROR_VARIABLE _patch_err)
    if(_patch_rc EQUAL 0)
        message(STATUS "${_patch_out}")
    else()
        message(WARNING "WiFi lib patch failed: ${_patch_err}")
    endif()
elseif(EXISTS "${_wifi_lib}")
    message(STATUS "WiFi lib patch skipped (tools not found for ${IDF_TARGET})")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(furi_esp32)
