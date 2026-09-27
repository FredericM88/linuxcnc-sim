#include "tool/Tool.hpp"
#include <cmath>
#include <stdexcept>

namespace cnc {
ToolPose tool_pose(const MachineSnapshot& snapshot) {
    std::array<double, axis_count> units{};
    for (std::size_t i = 0; i < axis_count; ++i) {
        if (!std::isfinite(snapshot.scales[i]) || snapshot.scales[i] == 0)
            throw std::invalid_argument("invalid snapshot scale");
        units[i] = static_cast<double>(snapshot.steps[i]) / snapshot.scales[i];
        if (!std::isfinite(units[i])) throw std::overflow_error("tool position outside finite range");
    }
    return {{units[0], units[1], units[2]}, units[3]};
}
} // namespace cnc
