function(driver_contract name source)
    add_executable(${name} driver_contract.c)
    target_include_directories(${name} PRIVATE "${prepared}" ${hal_includes})
    target_compile_definitions(${name} PRIVATE DRIVER_SOURCE="${source}" USPACE RTAPI _GNU_SOURCE realtime __MODULE__ SIM)
    target_compile_options(${name} PRIVATE -UNDEBUG -Werror=implicit-function-declaration -Werror=incompatible-pointer-types)
    target_link_libraries(${name} PRIVATE m)
    add_test(NAME ${name} COMMAND ${name})
endfunction()
driver_contract(hal-driver-contract "${prepared}/stepgen-ninja.c")
add_test(NAME hal-source-preparation COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/preparation.py" "${STEPPER_NINJA_SOURCE}")
foreach(pin 0 9 10 11 40 65)
    add_test(NAME hal-create-failure-${pin} COMMAND hal-driver-contract ${pin})
endforeach()
# The unmodified baseline requires the old headers. Its transcript can also be
# compared with the API-1 executable from a second build directory.
if(NOT LINUXCNC_SOURCE_DIR)
    file(READ "${LINUXCNC_INCLUDE_DIR}/hal.h" hal_header)
    if(NOT hal_header MATCHES "#define HAL_API_VERSION")
        driver_contract(hal-driver-baseline "${STEPPER_NINJA_SOURCE}/hal-driver/stepgen-ninja.c")
        target_include_directories(hal-driver-baseline PRIVATE "${STEPPER_NINJA_SOURCE}/hal-driver")
        add_test(NAME hal-baseline-parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/compare.py" "$<TARGET_FILE:hal-driver-baseline>" "$<TARGET_FILE:hal-driver-contract>")
    endif()
endif()
set(HAL_BASELINE_EXECUTABLE "" CACHE FILEPATH "Optional API-0 baseline executable for cross-version parity")
if(HAL_BASELINE_EXECUTABLE)
    add_test(NAME hal-cross-version-parity COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/compare.py" "${HAL_BASELINE_EXECUTABLE}" "$<TARGET_FILE:hal-driver-contract>")
endif()

if(module_modinc)
    add_test(NAME hal-module-build-mode COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/build_modes.py"
        --cmake "${CMAKE_COMMAND}" --compiler "${CMAKE_C_COMPILER}"
        --upstream "${STEPPER_NINJA_SOURCE}" --modinc "${module_modinc}"
        --includes "${hal_includes}" --linuxcnc-source "${LINUXCNC_SOURCE_DIR}")
    set_tests_properties(hal-module-build-mode PROPERTIES TIMEOUT 120)
endif()
