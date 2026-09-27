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
    src/simulation/Workpiece.cpp
    src/console/WorkpieceCommands.cpp
    src/console/TerminalUI.cpp
    src/console/IOCommands.cpp
    src/io/VirtualIO.cpp
    src/io/Sensors.cpp
    src/io/VirtualSensors.cpp
    src/volume/SparseVoxelVolume.cpp
    src/meshing/SurfaceMesher.cpp
    src/tool/Tool.cpp)
target_include_directories(cnc_sim_core PUBLIC src)
find_package(Threads REQUIRED)
find_package(glm REQUIRED)
target_link_libraries(cnc_sim_core PUBLIC stepper_ninja_original Threads::Threads glm::glm)
target_compile_options(cnc_sim_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion)

add_executable(cnc-sim src/main.cpp)
target_link_libraries(cnc-sim PRIVATE cnc_sim_core)
target_compile_options(cnc-sim PRIVATE -Wall -Wextra -Wpedantic -Wconversion)

option(CNC_SIM_RENDER "Build the optional OpenGL 3.3 renderer" ON)
if(CNC_SIM_RENDER)
    find_package(OpenGL REQUIRED)
    find_package(glfw3 3.3 REQUIRED)
    add_library(cnc_sim_render STATIC src/render/Renderer.cpp src/render/Camera.cpp)
    target_link_libraries(cnc_sim_render PUBLIC cnc_sim_core PRIVATE OpenGL::GL glfw)
    target_compile_options(cnc_sim_render PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
    target_link_libraries(cnc-sim PRIVATE cnc_sim_render)
    target_compile_definitions(cnc-sim PRIVATE CNC_SIM_RENDER=1)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(workpiece-tests tests/workpiece_tests.cpp)
    target_link_libraries(workpiece-tests PRIVATE cnc_sim_core)
    target_compile_options(workpiece-tests PRIVATE -Wall -Wextra -Wpedantic)
    foreach(test geometry transactions runtime)
        add_test(NAME workpiece-${test} COMMAND workpiece-tests ${test})
        set_tests_properties(workpiece-${test} PROPERTIES TIMEOUT 30)
    endforeach()
    add_executable(phase4a-tests tests/phase4a_tests.cpp)
    target_link_libraries(phase4a-tests PRIVATE cnc_sim_core)
    target_compile_options(phase4a-tests PRIVATE -Wall -Wextra -Wpedantic)
    foreach(test volume mesher snapshot)
        add_test(NAME phase4a-${test} COMMAND phase4a-tests ${test})
        set_tests_properties(phase4a-${test} PROPERTIES TIMEOUT 30)
    endforeach()
    option(CNC_SIM_RENDER_TESTS "Run desktop-dependent renderer tests" OFF)
    if(CNC_SIM_RENDER_TESTS)
        if(NOT CNC_SIM_RENDER)
            message(FATAL_ERROR "CNC_SIM_RENDER_TESTS requires CNC_SIM_RENDER")
        endif()
        add_executable(render-smoke tests/render_smoke.cpp)
        target_link_libraries(render-smoke PRIVATE cnc_sim_render OpenGL::GL glfw)
        add_test(NAME render-smoke COMMAND render-smoke)
        set_tests_properties(render-smoke PROPERTIES LABELS renderer TIMEOUT 60)
    endif()
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
    add_test(NAME workpiece-cli COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/workpiece_cli.py" "$<TARGET_FILE:cnc-sim>")
    set_tests_properties(workpiece-cli PROPERTIES TIMEOUT 30)
    add_test(NAME phase4a-cli COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/phase4a_cli.py" "$<TARGET_FILE:cnc-sim>")
    set_tests_properties(phase4a-cli PROPERTIES TIMEOUT 30)
    if(CNC_SIM_RENDER_TESTS)
        add_test(NAME render-integration COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/render_integration.py" "$<TARGET_FILE:cnc-sim>")
        set_tests_properties(render-integration PROPERTIES LABELS renderer TIMEOUT 60)
    endif()
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
