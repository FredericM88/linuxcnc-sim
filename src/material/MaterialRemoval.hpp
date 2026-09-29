#pragma once
#include "meshing/SurfaceMesher.hpp"
#include "tool/Tool.hpp"

namespace cnc {
struct SweepSegment { glm::dvec3 from{}, to{}; };
struct SweepBatchStats {
    std::uint64_t batches{}, segments{}, max_segments{}, candidate_chunks{}, candidate_voxels{}, occupied_candidates{};
    std::uint64_t chunk_segment_refs{}, containment_tests{}, envelope_rejects{}, prefilter_rejected_refs{};
    std::uint64_t first_hit_exits{}, first_hit_skipped_refs{};
    double processing_ms{};
};
struct RemovalStats {
    std::uint64_t sweeps{}, chunks_tested{}, chunks_changed{}, voxels_tested{}, voxels_removed{};
    SweepBatchStats batch;
    std::uint64_t containment_tests{};
    double broad_ms{}, narrow_ms{}, mutation_ms{}, invalidation_ms{};
};
// Single material-worker owner; no graphics, synchronization or machine state.
class MaterialRemoval {
public:
    explicit MaterialRemoval(const SparseVoxelVolume& raw) : volume_(raw) {}
    void sweep(glm::dvec3 from, glm::dvec3 to, const ToolDefinition& tool);
    void sweep_batch(std::span<const SweepSegment> segments, const ToolDefinition& tool);
    const SparseVoxelVolume& volume() const { return volume_; }
    const RemovalStats& stats() const { return stats_; }
    DirtyChunks& dirty() { return dirty_; }
    void dirty_all();
private:
    SparseVoxelVolume volume_;
    RemovalStats stats_;
    DirtyChunks dirty_;
};
} // namespace cnc
