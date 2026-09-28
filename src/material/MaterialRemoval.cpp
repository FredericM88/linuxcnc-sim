#include "material/MaterialRemoval.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace cnc {
void MaterialRemoval::dirty_all() {
    const auto last = volume_.last_chunk();
    for (std::int64_t z = 0; z <= last.z; ++z)
        for (std::int64_t y = 0; y <= last.y; ++y)
            for (std::int64_t x = 0; x <= last.x; ++x) dirty_.mark({x,y,z});
}
void MaterialRemoval::invalidate(VoxelCoord p) {
    const auto c = volume_.voxel_to_chunk(p), local = volume_.chunk_local(p);
    const auto last = volume_.last_chunk();
    dirty_.mark(c);
    for (int axis = 0; axis < 3; ++axis) {
        auto neighbour = c;
        if (local[axis] == 0 && c[axis] > 0) { --neighbour[axis]; dirty_.mark(neighbour); }
        neighbour = c;
        if (local[axis] == volume_.config().volume.chunk_size - 1 && c[axis] < last[axis]) {
            ++neighbour[axis]; dirty_.mark(neighbour);
        }
    }
}
void MaterialRemoval::sweep(glm::dvec3 from, glm::dvec3 to, const ToolDefinition& tool) {
    validate_tool(tool);
    for (int i = 0; i < 3; ++i)
        if (!std::isfinite(from[i]) || !std::isfinite(to[i]) || std::abs(from[i]) > 1e12 || std::abs(to[i]) > 1e12)
            throw std::invalid_argument("material motion outside supported +/-1e12 mm range");
    ++stats_.sweeps;
    const auto& transform = volume_.config().workpiece;
    const double r = tool.diameter_mm / 2;
    const auto low = glm::min(from, to) - glm::dvec3(r,r,0);
    const auto high = glm::max(from, to) + glm::dvec3(r,r,tool.length_mm);
    glm::dvec3 local_low(std::numeric_limits<double>::max()), local_high(-std::numeric_limits<double>::max());
    // Transform all machine AABB corners: remains conservative for legacy rotations.
    for (int corner = 0; corner < 8; ++corner) {
        glm::dvec3 p;
        for (int i = 0; i < 3; ++i) p[i] = corner & (1 << i) ? high[i] : low[i];
        const auto local = transform.to_local(p);
        local_low = glm::min(local_low, local); local_high = glm::max(local_high, local);
    }
    // Outward rounding for transforms at large translations. Narrow phase decides inclusion.
    const double magnitude = std::max({1.0, glm::length(from), glm::length(to), glm::length(transform.translation),
                                       tool.diameter_mm, tool.length_mm});
    const double pad = 4 * sweep_tolerance_mm + 64 * std::numeric_limits<double>::epsilon() * magnitude;
    local_low -= glm::dvec3(pad); local_high += glm::dvec3(pad);
    const auto dims = volume_.dimensions();
    const auto h = volume_.config().volume.voxel_size_mm;
    VoxelCoord first, last;
    for (int i = 0; i < 3; ++i) {
        if (local_high[i] < 0 || local_low[i] > volume_.extent_mm()[i]) return;
        // Clamp in floating point before conversion, including very distant sweeps.
        first[i] = static_cast<std::int64_t>(std::clamp(std::floor(local_low[i]/h), 0.0, static_cast<double>(dims[i]-1)));
        last[i] = static_cast<std::int64_t>(std::clamp(std::floor(local_high[i]/h), 0.0, static_cast<double>(dims[i]-1)));
    }
    const auto c0 = volume_.voxel_to_chunk(first), c1 = volume_.voxel_to_chunk(last);
    const auto n = volume_.config().volume.chunk_size;
    for (auto zc = c0.z; zc <= c1.z; ++zc) for (auto yc = c0.y; yc <= c1.y; ++yc) for (auto xc = c0.x; xc <= c1.x; ++xc) {
        const ChunkCoord c{xc,yc,zc};
        ++stats_.chunks_tested;
        if (volume_.chunk_state(c) == ChunkState::Empty) continue;
        const auto origin = volume_.chunk_origin(c);
        VoxelCoord begin, end;
        for (int i = 0; i < 3; ++i) {
            begin[i] = std::max(first[i], origin[i]); end[i] = std::min(last[i], origin[i] + n - 1);
        }
        bool changed = false;
        for (auto z = begin.z; z <= end.z; ++z) for (auto y = begin.y; y <= end.y; ++y) for (auto x = begin.x; x <= end.x; ++x) {
            const VoxelCoord p{x,y,z};
            if (volume_.sample(p) != 255) continue;
            ++stats_.voxels_tested;
            const auto centre = transform.to_machine(volume_.voxel_to_local(p) + glm::dvec3(h/2));
            if (swept_tool_contains(tool, from, to, centre) && volume_.erase(p)) {
                changed = true; ++stats_.voxels_removed; invalidate(p);
            }
        }
        if (changed) ++stats_.chunks_changed;
    }
}
} // namespace cnc
