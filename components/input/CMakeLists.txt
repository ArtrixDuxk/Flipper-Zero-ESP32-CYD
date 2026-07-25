# Target-specific input driver selection (must match furi_hal board)
if(DEFINED ENV{FLIPPER_BOARD} AND NOT "$ENV{FLIPPER_BOARD}" STREQUAL "")
    set(FLIPPER_BOARD "$ENV{FLIPPER_BOARD}" CACHE STRING "Target board" FORCE)
elseif(NOT DEFINED FLIPPER_BOARD OR FLIPPER_BOARD STREQUAL "")
    set(FLIPPER_BOARD "waveshare_c6_1.9" CACHE STRING "Target board" FORCE)
else()
    set(FLIPPER_BOARD "${FLIPPER_BOARD}" CACHE STRING "Target board" FORCE)
endif()

set(TARGET_INPUT_DIR "${CMAKE_CURRENT_LIST_DIR}/../../targets/${FLIPPER_BOARD}")

if(NOT EXISTS "${TARGET_INPUT_DIR}/target_input.c")
    message(FATAL_ERROR "No target_input.c found for board '${FLIPPER_BOARD}' at ${TARGET_INPUT_DIR}")
endif()

idf_component_register(
    SRCS
        "input.c"
        "${TARGET_INPUT_DIR}/target_input.c"
    INCLUDE_DIRS "." "${CMAKE_CURRENT_LIST_DIR}/../../targets"
    REQUIRES furi
    PRIV_REQUIRES furi_hal
)
