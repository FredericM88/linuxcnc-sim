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
    double length_mm{30};
};
} // namespace cnc
