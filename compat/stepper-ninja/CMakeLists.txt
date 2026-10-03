cmake_minimum_required(VERSION 3.20)
project(stepper_ninja_hal_compat LANGUAGES C)

# Standalone and opt-in: normal simulator builds need neither HAL nor upstream.
set(STEPPER_NINJA_SOURCE "" CACHE PATH "Separate pinned upstream Stepper-Ninja checkout")
set(LINUXCNC_SOURCE_DIR "" CACHE PATH "Compile-check only against an uninstalled LinuxCNC source tree")
if(NOT STEPPER_NINJA_SOURCE)
    message(FATAL_ERROR "Set STEPPER_NINJA_SOURCE to the pinned upstream checkout root")
endif()
find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
find_program(PATCH_PROGRAM patch REQUIRED)
set(prepared "${CMAKE_CURRENT_BINARY_DIR}/prepared")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    prepare.py hal-api.patch hal_compat.h
    "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/stepper-ninja/SHA256SUMS")
set(upstream_inputs "${STEPPER_NINJA_SOURCE}/hal-driver/stepgen-ninja.c"
    "${STEPPER_NINJA_SOURCE}/hal-driver/hal_pin_macros.h")
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/stepper-ninja/SHA256SUMS" manifest)
foreach(entry IN LISTS manifest)
    string(REGEX REPLACE "^[0-9a-f]+  " "" relative "${entry}")
    list(APPEND upstream_inputs "${STEPPER_NINJA_SOURCE}/${relative}")
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${upstream_inputs})
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/prepare.py"
    --source "${STEPPER_NINJA_SOURCE}" --output "${prepared}"
    COMMAND_ERROR_IS_FATAL ANY)

if(LINUXCNC_SOURCE_DIR)
    # No installed LinuxCNC includes, modinc, libraries or generated headers.
    # These are the userspace realtime-module defines, not an ABI/link test.
    foreach(header src/hal/hal.h src/rtapi/rtapi.h)
        if(NOT EXISTS "${LINUXCNC_SOURCE_DIR}/${header}")
            message(FATAL_ERROR "Missing LinuxCNC source header: ${header}")
        endif()
    endforeach()
    set(hal_includes "${LINUXCNC_SOURCE_DIR}/src/hal" "${LINUXCNC_SOURCE_DIR}/src/rtapi")
    add_library(stepgen-ninja OBJECT "${prepared}/stepgen-ninja.c")
    target_include_directories(stepgen-ninja PRIVATE "${prepared}" ${hal_includes})
    target_compile_definitions(stepgen-ninja PRIVATE USPACE RTAPI _GNU_SOURCE realtime __MODULE__ SIM)
    target_compile_options(stepgen-ninja PRIVATE -fno-fast-math -Werror=implicit-function-declaration
        -Werror=incompatible-pointer-types)
    set_target_properties(stepgen-ninja PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)
    message(STATUS "Compile check only; no loadable module or install target")
else()
    find_path(LINUXCNC_INCLUDE_DIR hal.h PATHS /usr/include/linuxcnc /usr/local/include/linuxcnc REQUIRED)
    find_file(LINUXCNC_MODINC Makefile.modinc PATHS /usr/share/linuxcnc /etc/linuxcnc /usr/lib/linuxcnc REQUIRED)
    find_program(MAKE_PROGRAM NAMES gmake make REQUIRED)
    set(hal_includes "${LINUXCNC_INCLUDE_DIR}")
    configure_file(module.mk.in "${CMAKE_CURRENT_BINARY_DIR}/Makefile.hal" @ONLY)
    file(GLOB_RECURSE prepared_inputs "${prepared}/*")
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/stepgen-ninja.so"
        COMMAND "${MAKE_PROGRAM}" -f Makefile.hal "CC=${CMAKE_C_COMPILER}" modules
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        DEPENDS ${prepared_inputs} "${CMAKE_CURRENT_BINARY_DIR}/Makefile.hal"
        BYPRODUCTS stepgen-ninja.o stepgen-ninja.tmp stepgen-ninja.ver
        VERBATIM)
    add_custom_target(stepgen-ninja ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/stepgen-ninja.so")
    message(STATUS "HAL headers: ${LINUXCNC_INCLUDE_DIR}; build rules: ${LINUXCNC_MODINC}")
endif()

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
