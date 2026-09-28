#pragma once
#include "simulation/Simulation.hpp"
namespace cnc {
inline constexpr const char* material_help =
    "tool show | tool flat-end DIAMETER CUTTING_LENGTH\n"
    "material show | material on | material off | material reset";
std::string execute_material_command(Simulation& simulation, const std::string& line);
std::string describe_material(const MaterialStatus& status, bool compact = false);
} // namespace cnc
