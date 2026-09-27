#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace cnc {
using VoxelValue = std::uint8_t; // 0 empty, 255 material; intermediate values reserved.
struct GridCoord {
    std::int64_t x{}, y{}, z{};
    bool operator==(const GridCoord&) const = default;
    std::int64_t& operator[](int i) { return i == 0 ? x : i == 1 ? y : z; }
    std::int64_t operator[](int i) const { return i == 0 ? x : i == 1 ? y : z; }
};
using VoxelCoord = GridCoord;
using ChunkCoord = GridCoord;
struct ChunkCoordHash { std::size_t operator()(ChunkCoord c) const noexcept; };
std::int64_t floor_div(std::int64_t value, std::int64_t divisor);

struct VolumeConfig {
    double voxel_size_mm = 0.10;
    std::uint32_t chunk_size = 32;
};
struct WorkpieceTransform {
    glm::dvec3 translation{0, 0, -10};
    glm::dquat orientation{1, 0, 0, 0};
    glm::dvec3 to_machine(glm::dvec3 local) const;
    glm::dvec3 to_local(glm::dvec3 machine) const;
    glm::dmat4 matrix() const;
};
struct SceneConfig {
    VolumeConfig volume;
    glm::dvec3 stock_size_mm{50, 50, 10};
    WorkpieceTransform workpiece;
};
enum class ChunkState : std::uint8_t { Empty, Solid, Mixed };
struct VoxelChunk {
    ChunkState state{ChunkState::Empty};
    std::vector<VoxelValue> values; // Only Mixed allocates; x varies fastest.
};

// Simulation data, with no graphics dependency. Initial box is implicit even
// for partially occupied boundary chunks. Materialization does not change it.
class SparseVoxelVolume {
public:
    explicit SparseVoxelVolume(SceneConfig config = {});
    VoxelValue sample(VoxelCoord voxel) const;
    ChunkState chunk_state(ChunkCoord chunk) const;
    const VoxelChunk& materialize_chunk(ChunkCoord chunk);
    VoxelCoord local_to_voxel(glm::dvec3 mm) const;
    glm::dvec3 voxel_to_local(VoxelCoord voxel) const;
    ChunkCoord voxel_to_chunk(VoxelCoord voxel) const;
    VoxelCoord chunk_local(VoxelCoord voxel) const;
    VoxelCoord chunk_origin(ChunkCoord chunk) const;
    const SceneConfig& config() const { return config_; }
    VoxelCoord dimensions() const { return dimensions_; }
    glm::dvec3 extent_mm() const { return voxel_to_local(dimensions_); }
    ChunkCoord last_chunk() const;
    std::size_t stored_chunks() const { return chunks_.size(); }
    std::size_t stored_voxel_bytes() const;
private:
    bool inside(VoxelCoord voxel) const;
    SceneConfig config_;
    VoxelCoord dimensions_;
    std::unordered_map<ChunkCoord, VoxelChunk, ChunkCoordHash> chunks_;
};
} // namespace cnc
