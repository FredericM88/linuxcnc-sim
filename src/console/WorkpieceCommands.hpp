#pragma once
#include <string>
#include "simulation/Workpiece.hpp"
namespace cnc {
struct WorkpieceCommand {
    bool show{};
    WorkpieceConfig config;
};
WorkpieceCommand parse_workpiece_command(const std::string& line, const WorkpieceConfig& current);
std::string describe_workpiece(const WorkpieceSnapshot& snapshot);
inline constexpr const char* workpiece_help =
    "workpiece show | reset; workpiece size X Y Z; workpiece position X Y Z (G53)\n"
    "workpiece origin X Y Z (mm or min|center|max per axis); workpiece voxel SIZE";
} // namespace cnc
