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
struct Bounds { VoxelCoord first{}, last{}; bool valid{}; };
Bounds sweep_bounds(const SparseVoxelVolume& volume, glm::dvec3 from, glm::dvec3 to, const ToolDefinition& tool) {
    const auto& transform = volume.config().workpiece;
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
    const auto dims = volume.dimensions();
    const auto h = volume.config().volume.voxel_size_mm;
    VoxelCoord first, last;
    for (int i = 0; i < 3; ++i) {
        if (local_high[i] < 0 || local_low[i] > volume.extent_mm()[i]) {
            return {};
        }
        // Clamp in floating point before conversion, including very distant sweeps.
        first[i] = static_cast<std::int64_t>(std::clamp(std::floor(local_low[i]/h), 0.0, static_cast<double>(dims[i]-1)));
        last[i] = static_cast<std::int64_t>(std::clamp(std::floor(local_high[i]/h), 0.0, static_cast<double>(dims[i]-1)));
    }
    return {first,last,true};
}
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
    const auto bounds=sweep_bounds(volume_,from,to,tool);
    if (!bounds.valid) { stats_.broad_ms+=ms(Clock::now()-broad_start); return; }
    const auto first=bounds.first, last=bounds.last;
    const auto& transform=volume_.config().workpiece;
    const auto h=volume_.config().volume.voxel_size_mm;
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
            ++stats_.voxels_tested; ++stats_.containment_tests;
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
// Union membership uses the unchanged analytic predicate. Bounds/envelopes only
// reject impossible hits; all original (or exactly coalesced) segments survive.
void MaterialRemoval::sweep_batch(std::span<const SweepSegment> segments, const ToolDefinition& tool) {
    const auto start = Clock::now();
    validate_tool(tool);
    if (segments.empty()) return;
    for (const auto& s : segments) for (int axis=0; axis<3; ++axis)
        if (!std::isfinite(s.from[axis]) || !std::isfinite(s.to[axis]) ||
            std::abs(s.from[axis])>1e12 || std::abs(s.to[axis])>1e12)
            throw std::invalid_argument("material motion outside supported +/-1e12 mm range");
    struct Envelope {
        glm::dvec3 low{std::numeric_limits<double>::max()}, high{-std::numeric_limits<double>::max()};
        double pad{};
    };
    struct Prepared { Bounds bounds; Envelope envelope; };
    struct Candidates { std::vector<std::size_t> refs; Bounds bounds; Envelope envelope; };
    const auto& transform=volume_.config().workpiece;
    auto merge_envelope=[](Envelope& a, const Envelope& b) {
        a.low=glm::min(a.low,b.low); a.high=glm::max(a.high,b.high); a.pad=std::max(a.pad,b.pad);
    };
    auto merge_bounds=[](Bounds& a, const Bounds& b) {
        if (!a.valid) { a=b; return; }
        for (int i=0; i<3; ++i) { a.first[i]=std::min(a.first[i],b.first[i]); a.last[i]=std::max(a.last[i],b.last[i]); }
    };
    // Every point of a segment lies in its endpoint box. Distance to that box
    // is a conservative lower bound on XY distance to ANY contained segment.
    auto outside=[&](const Envelope& e, glm::dvec3 p) {
        if (p.z<e.low.z-e.pad || p.z>e.high.z+tool.length_mm+e.pad) return true;
        const double dx=std::max({e.low.x-p.x,0.0,p.x-e.high.x});
        const double dy=std::max({e.low.y-p.y,0.0,p.y-e.high.y});
        const double radius=tool.diameter_mm/2+e.pad;
        return dx*dx+dy*dy > radius*radius;
    };
    std::vector<Prepared> prepared; prepared.reserve(segments.size());
    std::unordered_map<ChunkCoord,Candidates,ChunkCoordHash> chunks;
    auto& stats=stats_.batch;
    for (std::size_t i=0; i<segments.size(); ++i) {
        const auto& s=segments[i];
        const double magnitude=std::max({1.0,glm::length(s.from),glm::length(s.to),glm::length(transform.translation),tool.diameter_mm,tool.length_mm});
        Prepared p{sweep_bounds(volume_,s.from,s.to,tool),
                   {glm::min(s.from,s.to),glm::max(s.from,s.to),4*sweep_tolerance_mm+64*std::numeric_limits<double>::epsilon()*magnitude}};
        prepared.push_back(p);
        if (!p.bounds.valid) continue;
        const auto c0=volume_.voxel_to_chunk(p.bounds.first), c1=volume_.voxel_to_chunk(p.bounds.last);
        for (auto z=c0.z; z<=c1.z; ++z) for (auto y=c0.y; y<=c1.y; ++y) for (auto x=c0.x; x<=c1.x; ++x) {
            auto& c=chunks[{x,y,z}]; c.refs.push_back(i);
            merge_bounds(c.bounds,p.bounds); merge_envelope(c.envelope,p.envelope); ++stats.chunk_segment_refs;
        }
    }
    stats_.broad_ms+=ms(Clock::now()-start);
    ++stats.batches; stats.segments+=segments.size(); stats.max_segments=std::max(stats.max_segments,static_cast<std::uint64_t>(segments.size()));
    stats_.sweeps+=segments.size(); stats.candidate_chunks+=chunks.size();
    const auto n=volume_.config().volume.chunk_size;
    const auto h=volume_.config().volume.voxel_size_mm;
    std::vector<std::uint32_t> hits;
    for (const auto& [c,candidates] : chunks) {
        ++stats_.chunks_tested;
        if (volume_.chunk_state(c)==ChunkState::Empty) continue;
        const auto origin=volume_.chunk_origin(c);
        VoxelCoord begin,end;
        for (int i=0; i<3; ++i) { begin[i]=std::max(origin[i],candidates.bounds.first[i]); end[i]=std::min(origin[i]+n-1,candidates.bounds.last[i]); }
        hits.clear(); unsigned boundary=0;
        const auto narrow_start=Clock::now();
        for (auto z=begin.z; z<=end.z; ++z) for (auto y=begin.y; y<=end.y; ++y) for (auto x=begin.x; x<=end.x; ++x) {
            ++stats.candidate_voxels;
            const VoxelCoord p{x,y,z};
            if (volume_.sample(p)!=255) continue;
            ++stats.occupied_candidates; ++stats_.voxels_tested;
            const auto centre=transform.to_machine(volume_.voxel_to_local(p)+glm::dvec3(h/2));
            if (outside(candidates.envelope,centre)) { ++stats.envelope_rejects; stats.prefilter_rejected_refs+=candidates.refs.size(); continue; }
            for (std::size_t j=0; j<candidates.refs.size(); ++j) {
                const auto index=candidates.refs[j]; const auto& b=prepared[index].bounds;
                bool in_bounds=true;
                for (int axis=0; axis<3; ++axis) in_bounds &= p[axis]>=b.first[axis] && p[axis]<=b.last[axis];
                if (!in_bounds || outside(prepared[index].envelope,centre)) { ++stats.prefilter_rejected_refs; continue; }
                ++stats.containment_tests; ++stats_.containment_tests;
                const auto& segment=segments[index];
                if (!swept_tool_contains(tool,segment.from,segment.to,centre)) continue;
                ++stats.first_hit_exits; stats.first_hit_skipped_refs+=candidates.refs.size()-j-1;
                hits.push_back(static_cast<std::uint32_t>((x-origin.x)+n*((y-origin.y)+n*(z-origin.z))));
                for (int axis=0; axis<3; ++axis) {
                    if (p[axis]==origin[axis]) boundary |= 1u<<(2*axis);
                    if (p[axis]==origin[axis]+n-1) boundary |= 1u<<(2*axis+1);
                }
                break;
            }
        }
        stats_.narrow_ms+=ms(Clock::now()-narrow_start);
        const auto mutation_start=Clock::now();
        const auto removed=volume_.erase_chunk_voxels(c,hits);
        stats_.mutation_ms+=ms(Clock::now()-mutation_start);
        const auto invalidation_start=Clock::now();
        if (removed) {
            dirty_.mark(c);
            const auto last=volume_.last_chunk();
            for (int axis=0; axis<3; ++axis) for (int side=0; side<2; ++side) {
                if (!(boundary & (1u<<(2*axis+side)))) continue;
                auto neighbour=c; neighbour[axis]+=side ? 1 : -1;
                if (neighbour[axis]>=0 && neighbour[axis]<=last[axis]) dirty_.mark(neighbour);
            }
            ++stats_.chunks_changed; stats_.voxels_removed+=removed;
        }
        stats_.invalidation_ms+=ms(Clock::now()-invalidation_start);
    }
    stats.processing_ms+=ms(Clock::now()-start);
}
} // namespace cnc
