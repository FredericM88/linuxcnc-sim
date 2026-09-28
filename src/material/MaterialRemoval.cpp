#include "material/MaterialRemoval.hpp"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <limits>
#include <stdexcept>

namespace cnc {
namespace {
using Clock = std::chrono::steady_clock;
double ms(Clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); }
}

void MaterialRemoval::dirty_all() {
    const auto last = volume_.last_chunk();
    for (std::int64_t z = 0; z <= last.z; ++z)
        for (std::int64_t y = 0; y <= last.y; ++y)
            for (std::int64_t x = 0; x <= last.x; ++x) dirty_.mark({x,y,z});
}
void MaterialRemoval::sweep(glm::dvec3 from, glm::dvec3 to, const ToolDefinition& tool) {
    const auto broad_start = Clock::now();
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
        if (local_high[i] < 0 || local_low[i] > volume_.extent_mm()[i]) {
            stats_.broad_ms += ms(Clock::now() - broad_start); return;
        }
        // Clamp in floating point before conversion, including very distant sweeps.
        first[i] = static_cast<std::int64_t>(std::clamp(std::floor(local_low[i]/h), 0.0, static_cast<double>(dims[i]-1)));
        last[i] = static_cast<std::int64_t>(std::clamp(std::floor(local_high[i]/h), 0.0, static_cast<double>(dims[i]-1)));
    }
    const auto c0 = volume_.voxel_to_chunk(first), c1 = volume_.voxel_to_chunk(last);
    const auto n = volume_.config().volume.chunk_size;
    stats_.broad_ms += ms(Clock::now() - broad_start);
    std::vector<VoxelCoord> hits; // Reused per chunk; no per-voxel clock reads.
    for (auto zc = c0.z; zc <= c1.z; ++zc) for (auto yc = c0.y; yc <= c1.y; ++yc) for (auto xc = c0.x; xc <= c1.x; ++xc) {
        const ChunkCoord c{xc,yc,zc};
        ++stats_.chunks_tested;
        if (volume_.chunk_state(c) == ChunkState::Empty) continue;
        const auto origin = volume_.chunk_origin(c);
        VoxelCoord begin, end;
        for (int i = 0; i < 3; ++i) {
            begin[i] = std::max(first[i], origin[i]); end[i] = std::min(last[i], origin[i] + n - 1);
        }
        hits.clear();
        const auto narrow_start = Clock::now();
        for (auto z = begin.z; z <= end.z; ++z) for (auto y = begin.y; y <= end.y; ++y) for (auto x = begin.x; x <= end.x; ++x) {
            const VoxelCoord p{x,y,z};
            if (volume_.sample(p) != 255) continue;
            ++stats_.voxels_tested;
            const auto centre = transform.to_machine(volume_.voxel_to_local(p) + glm::dvec3(h/2));
            if (swept_tool_contains(tool, from, to, centre)) hits.push_back(p);
        }
        stats_.narrow_ms += ms(Clock::now() - narrow_start);
        const auto mutation_start = Clock::now();
        for (const auto p : hits) volume_.erase(p);
        stats_.mutation_ms += ms(Clock::now() - mutation_start);
        const auto invalidation_start = Clock::now();
        if (!hits.empty()) {
            unsigned boundary = 0;
            for (const auto p : hits) for (int axis=0; axis<3; ++axis) {
                if (p[axis]==origin[axis]) boundary |= 1u << (2*axis);
                if (p[axis]==origin[axis]+n-1) boundary |= 1u << (2*axis+1);
            }
            dirty_.mark(c);
            const auto last_chunk = volume_.last_chunk();
            for (int axis=0; axis<3; ++axis) for (int side=0; side<2; ++side) {
                if (!(boundary & (1u << (2*axis+side)))) continue;
                auto neighbour=c; neighbour[axis] += side ? 1 : -1;
                if (neighbour[axis]>=0 && neighbour[axis]<=last_chunk[axis]) dirty_.mark(neighbour);
            }
        }
        stats_.invalidation_ms += ms(Clock::now() - invalidation_start);
        stats_.voxels_removed += hits.size();
        if (!hits.empty()) ++stats_.chunks_changed;
    }
}
} // namespace cnc
