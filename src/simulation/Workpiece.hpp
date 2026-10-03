#pragma once
#include <memory>
#include "volume/SparseVoxelVolume.hpp"

namespace cnc {
// Console symbols resolve once; only numerical millimetres are authoritative.
struct WorkpieceConfig {
    glm::dvec3 size_mm = SceneConfig{}.stock_size_mm;
    glm::dvec3 position_machine_mm{0};
    glm::dvec3 origin_offset_mm{0, 0, SceneConfig{}.stock_size_mm.z};
    double voxel_size_mm = VolumeConfig{}.voxel_size_mm;
    glm::dvec3 machine_min() const { return position_machine_mm - origin_offset_mm; }
    glm::dvec3 machine_max() const { return machine_min() + size_mm; }
};
struct WorkpieceSnapshot {
    WorkpieceConfig config;
    std::shared_ptr<const SparseVoxelVolume> volume;
    std::uint64_t revision{};
    bool legacy_rotation{};
};
// Preserve legacy minimum-corner translation and rotation exactly.
std::shared_ptr<const WorkpieceSnapshot> workpiece_from_scene(const SceneConfig& scene);
// Same bounded job budget in graphical and headless simulation.
constexpr std::uint64_t max_workpiece_chunks = 65536;
std::shared_ptr<const SparseVoxelVolume> build_workpiece(const WorkpieceConfig& config);
void validate_workpiece_volume(const SparseVoxelVolume& volume);
} // namespace cnc
