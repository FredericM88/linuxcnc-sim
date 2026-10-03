#pragma once

#include <array>
#include <string>
#include <vector>
#include "material/MaterialWorker.hpp"
#include "io/VirtualSensors.hpp"

namespace cnc {
enum class AxisType { Linear, Rotary };
struct AxisConfig {
    AxisType type{AxisType::Linear};
    // Linear: steps per simulator linear unit (currently mm). Rotary: steps/degree.
    double steps_per_unit{default_scale};
};
struct NetworkConfig {
    std::string bind_address{"192.168.50.2"};
    std::uint16_t port{default_port};
};
struct VirtualIOConfig {
    std::vector<std::string> files;
    std::vector<IOCommand> commands;
};
struct SimulatorConfig {
    NetworkConfig network;
    std::string linear_units{"mm"};
    std::array<AxisConfig, axis_count> axes{{{}, {}, {}, {AxisType::Rotary, default_scale}}};
    // Legacy CLI labels remain independent of physical units, including A's "unit" default.
    std::array<std::string, axis_count> units{"mm", "mm", "mm", "unit"};
    unsigned stats_ms{};
    bool interactive{true}, verbose{}, render{}, print_config{};
    MaterialWorkerConfig workers;
    bool material_enabled{};
    ToolDefinition tool;
    WorkpieceConfig workpiece;
    glm::dvec3 stock_rotation_degrees{0};
    VirtualIOConfig io;
    // Prepared and validated without threads; directly consumed by Simulation.
    std::shared_ptr<const WorkpieceSnapshot> initial_workpiece;
    Scales scales() const;
};
// No sockets or simulation threads are created by configuration loading.
SimulatorConfig load_simulator_config(int argc, const char* const* argv, bool render_available);
std::string describe_config(const SimulatorConfig& config);
void simulator_usage();
} // namespace cnc
