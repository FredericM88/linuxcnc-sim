#pragma once
#include "machine/MachineState.hpp"
#include <glm/glm.hpp>

namespace cnc {
struct MachineSnapshot {
    StepPositions steps{};
    Scales scales{};
    std::uint64_t accepted_packets{};
};
struct ToolPose {
    glm::dvec3 tip_mm{}; // Machine space; tool extends from its tip towards +Z.
    double a_units{}; // Retained, but not interpreted as rotation in Phase 4A.
};
ToolPose tool_pose(const MachineSnapshot& snapshot);
enum class ToolKind { FlatEndMill, BallEndMill, VBit, ProbeSphere };
struct ToolDefinition {
    ToolKind kind{ToolKind::FlatEndMill};
    double diameter_mm{6};
    double length_mm{20}; // Cutting length, +machine Z from bottom-centre tip.
    bool operator==(const ToolDefinition&) const = default;
};
void validate_tool(const ToolDefinition& tool);
// Closed swept cylinder, with a fixed spatial tolerance (mm).
constexpr double sweep_tolerance_mm = 1e-9;
bool swept_tool_contains(const ToolDefinition& tool, glm::dvec3 a, glm::dvec3 b, glm::dvec3 point);
} // namespace cnc
