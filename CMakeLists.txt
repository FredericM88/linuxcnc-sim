cmake_minimum_required(VERSION 3.20)
project(cnc_sim LANGUAGES C CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "Phase 1 requires Linux/POSIX sockets and network namespaces")
endif()
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(NINJA_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stepper-ninja")
add_library(stepper_ninja_original STATIC
    "${NINJA_SOURCE}/firmware/modules/transmission.c")
# SYSTEM confines warnings from the unchanged C headers (notably kbmatrix.h).
target_include_directories(stepper_ninja_original SYSTEM PUBLIC
    "${NINJA_SOURCE}/firmware/inc"
    "${NINJA_SOURCE}/firmware/modules/inc")

add_library(cnc_sim_core STATIC
    src/protocol/StepperNinjaProtocol.cpp
    src/machine/MachineState.cpp
    src/network/UdpServer.cpp
    src/recorder/MotionRecorder.cpp
    src/simulation/Simulation.cpp
    src/console/TerminalUI.cpp
    src/console/IOCommands.cpp
    src/io/VirtualIO.cpp
    src/io/Sensors.cpp
    src/io/VirtualSensors.cpp)
target_include_directories(cnc_sim_core PUBLIC src)
find_package(Threads REQUIRED)
target_link_libraries(cnc_sim_core PUBLIC stepper_ninja_original Threads::Threads)
target_compile_options(cnc_sim_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion)

add_executable(cnc-sim src/main.cpp)
target_link_libraries(cnc-sim PRIVATE cnc_sim_core)
target_compile_options(cnc-sim PRIVATE -Wall -Wextra -Wpedantic -Wconversion)

include(CTest)
if(BUILD_TESTING)
    add_executable(protocol-tests tests/protocol_tests.cpp)
    target_link_libraries(protocol-tests PRIVATE cnc_sim_core)
    target_compile_options(protocol-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME protocol-unit COMMAND protocol-tests)
    add_executable(recorder-tests tests/recorder_tests.cpp)
    target_link_libraries(recorder-tests PRIVATE cnc_sim_core)
    target_compile_options(recorder-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME recorder-unit COMMAND recorder-tests)
    add_executable(io-tests tests/io_tests.cpp)
    target_link_libraries(io-tests PRIVATE cnc_sim_core)
    target_compile_options(io-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME io-unit COMMAND io-tests)
    # Tests execute the original HAL fragment directly; it is never copied or patched.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/stepper-ninja/hal-driver/modules/breakoutboard_hal_0.c")
        add_executable(original-hal-inputs tests/original_hal_inputs.c)
        target_include_directories(original-hal-inputs PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
        target_link_libraries(original-hal-inputs PRIVATE stepper_ninja_original)
        add_test(NAME original-hal-inputs COMMAND original-hal-inputs)
    endif()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME phase3-configuration COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/phase3_configuration.py")
    foreach(test io_integration io_terminal)
        add_test(NAME ${test} COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/${test}.py" "$<TARGET_FILE:cnc-sim>")
        set_tests_properties(${test} PROPERTIES TIMEOUT 30)
    endforeach()
    foreach(test recorder terminal)
        add_test(NAME ${test}-integration COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/${test}_integration.py" "$<TARGET_FILE:cnc-sim>")
        set_tests_properties(${test}-integration PROPERTIES TIMEOUT 30)
    endforeach()
    add_test(NAME udp-integration COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/udp_integration.py" "$<TARGET_FILE:cnc-sim>")
    set_tests_properties(udp-integration PROPERTIES TIMEOUT 20)
    add_test(NAME network-namespace COMMAND bash
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/network_namespace.sh" "$<TARGET_FILE:cnc-sim>")
    set_tests_properties(network-namespace PROPERTIES TIMEOUT 25 SKIP_RETURN_CODE 77)
    add_test(NAME vendor-integrity COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/vendor_integrity.py")
    add_test(NAME shell-syntax COMMAND bash -n
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/setup-veth.sh")
    foreach(script teardown-veth run-simulator network-common)
        add_test(NAME shell-syntax-${script} COMMAND bash -n
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/${script}.sh")
    endforeach()
endif()
