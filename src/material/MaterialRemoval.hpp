#pragma once
#include "meshing/SurfaceMesher.hpp"
#include "tool/Tool.hpp"

namespace cnc {
struct RemovalStats {
    std::uint64_t sweeps{}, chunks_tested{}, chunks_changed{}, voxels_tested{}, voxels_removed{};
};
// Single material-worker owner; no graphics, synchronization or machine state.
class MaterialRemoval {
public:
    explicit MaterialRemoval(const SparseVoxelVolume& raw) : volume_(raw) {}
    void sweep(glm::dvec3 from, glm::dvec3 to, const ToolDefinition& tool);
    const SparseVoxelVolume& volume() const { return volume_; }
    const RemovalStats& stats() const { return stats_; }
    DirtyChunks& dirty() { return dirty_; }
    void dirty_all();
private:
    void invalidate(VoxelCoord p);
    SparseVoxelVolume volume_;
    RemovalStats stats_;
    DirtyChunks dirty_;
};
} // namespace cnc
