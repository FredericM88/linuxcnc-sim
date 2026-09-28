#include "tool/Tool.hpp"
#include <cmath>
#include <algorithm>
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
void validate_tool(const ToolDefinition& tool) {
    if (tool.kind != ToolKind::FlatEndMill || !std::isfinite(tool.diameter_mm) ||
        !std::isfinite(tool.length_mm) || tool.diameter_mm <= 0 || tool.length_mm <= 0 ||
        tool.diameter_mm > 1e12 || tool.length_mm > 1e12)
        throw std::invalid_argument("tool requires flat-end diameter and cutting length in (0, 1e12] mm");
}
bool swept_tool_contains(const ToolDefinition& tool, glm::dvec3 a, glm::dvec3 b, glm::dvec3 p) {
    // Use extended precision for clipping/projection; no segment-length-dependent epsilon.
    const long double dx = static_cast<long double>(b.x) - a.x;
    const long double dy = static_cast<long double>(b.y) - a.y;
    const long double dz = static_cast<long double>(b.z) - a.z;
    const long double px = static_cast<long double>(p.x) - a.x;
    const long double py = static_cast<long double>(p.y) - a.y;
    const long double pz = static_cast<long double>(p.z) - a.z;
    const long double e = sweep_tolerance_mm;
    long double lo = 0, hi = 1;
    if (dz == 0) {
        if (pz < -e || pz > tool.length_mm + e) return false;
    } else {
        auto t0 = (pz - tool.length_mm - e) / dz;
        auto t1 = (pz + e) / dz;
        if (t0 > t1) std::swap(t0, t1);
        lo = std::max(lo, t0); hi = std::min(hi, t1);
        if (lo > hi) return false;
    }
    const auto speed2 = dx*dx + dy*dy;
    const auto t = speed2 == 0 ? lo : std::clamp((px*dx + py*dy) / speed2, lo, hi);
    const auto x = px - t*dx, y = py - t*dy;
    const long double r = tool.diameter_mm * .5L + e;
    return x*x + y*y <= r*r;
}
} // namespace cnc
