#include "volume/SparseVoxelVolume.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>

namespace cnc {
namespace {
// Keep arithmetic/float conversion well inside int64 and renderable mm ranges.
constexpr std::int64_t grid_limit = std::int64_t{1} << 40;
bool finite(glm::dvec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
}
std::size_t ChunkCoordHash::operator()(ChunkCoord c) const noexcept {
    auto hash = std::hash<std::int64_t>{}(c.x);
    for (auto v : {c.y, c.z}) hash ^= std::hash<std::int64_t>{}(v) + 0x9e3779b9u + (hash << 6) + (hash >> 2);
    return hash;
}
std::int64_t floor_div(std::int64_t value, std::int64_t divisor) {
    if (divisor <= 0) throw std::invalid_argument("floor divisor must be positive");
    return value / divisor - (value % divisor < 0 ? 1 : 0);
}
glm::dvec3 WorkpieceTransform::to_machine(glm::dvec3 local) const { return translation + orientation * local; }
glm::dvec3 WorkpieceTransform::to_local(glm::dvec3 machine) const { return glm::conjugate(orientation) * (machine - translation); }
glm::dmat4 WorkpieceTransform::matrix() const {
    return glm::translate(glm::dmat4(1), translation) * glm::mat4_cast(orientation);
}
SparseVoxelVolume::SparseVoxelVolume(SceneConfig config) : config_(config) {
    const auto h = config.volume.voxel_size_mm;
    if (!std::isfinite(h) || h <= 0 || config.volume.chunk_size == 0 || config.volume.chunk_size > 128)
        throw std::invalid_argument("voxel size must be finite and positive; chunk size must be 1..128");
    if (!finite(config.stock_size_mm) || !finite(config.workpiece.translation))
        throw std::invalid_argument("stock size and origin must be finite");
    const auto qlength = glm::length(config.workpiece.orientation);
    if (!std::isfinite(qlength) || qlength < 1e-12) throw std::invalid_argument("invalid workpiece orientation");
    config_.workpiece.orientation /= qlength;
    for (int i = 0; i < 3; ++i) {
        const double cells = config.stock_size_mm[i] / h;
        if (config.stock_size_mm[i] <= 0 || !std::isfinite(cells) || cells > static_cast<double>(grid_limit))
            throw std::invalid_argument("stock extent outside supported voxel range");
        // Snap floating-point noise near exact multiples; otherwise cover the box.
        const auto nearest = std::round(cells);
        dimensions_[i] = static_cast<std::int64_t>(std::ceil(
            nearest >= 1 && std::abs(cells - nearest) <=
                4 * std::numeric_limits<double>::epsilon() * cells ? nearest : cells));
        if (dimensions_[i] < 1 || !std::isfinite(static_cast<double>(dimensions_[i]) * h))
            throw std::invalid_argument("stock must occupy at least one finite voxel");
    }
}
bool SparseVoxelVolume::inside(VoxelCoord p) const {
    return p.x >= 0 && p.y >= 0 && p.z >= 0 && p.x < dimensions_.x && p.y < dimensions_.y && p.z < dimensions_.z;
}
VoxelValue SparseVoxelVolume::sample(VoxelCoord p) const {
    if (chunks_.empty()) return inside(p) ? 255 : 0;
    const auto it = chunks_.find(voxel_to_chunk(p));
    if (it == chunks_.end()) return inside(p) ? 255 : 0;
    const auto& chunk = it->second;
    if (chunk.state != ChunkState::Mixed) return chunk.state == ChunkState::Solid ? 255 : 0;
    const auto local = chunk_local(p);
    const auto n = config_.volume.chunk_size;
    return chunk.values[static_cast<std::size_t>(local.x + n * (local.y + n * local.z))];
}
VoxelCoord SparseVoxelVolume::chunk_origin(ChunkCoord c) const {
    const auto n = config_.volume.chunk_size;
    for (int i = 0; i < 3; ++i)
        if (c[i] < -grid_limit / n || c[i] > grid_limit / n)
            throw std::out_of_range("chunk outside supported grid range");
    return {c.x * n, c.y * n, c.z * n};
}
ChunkState SparseVoxelVolume::chunk_state(ChunkCoord c) const {
    const auto found = chunks_.find(c);
    if (found != chunks_.end()) return found->second.state;
    const auto p = chunk_origin(c);
    const auto n = config_.volume.chunk_size;
    bool full = true;
    for (int i = 0; i < 3; ++i) {
        if (p[i] >= dimensions_[i] || p[i] + n <= 0) return ChunkState::Empty;
        full &= p[i] >= 0 && p[i] + n <= dimensions_[i];
    }
    return full ? ChunkState::Solid : ChunkState::Mixed;
}
const VoxelChunk& SparseVoxelVolume::materialize_chunk(ChunkCoord c) {
    if (const auto it = chunks_.find(c); it != chunks_.end()) return it->second;
    VoxelChunk chunk;
    chunk.state = chunk_state(c);
    if (chunk.state == ChunkState::Mixed) {
        const auto n = config_.volume.chunk_size;
        const auto p = chunk_origin(c);
        chunk.values.reserve(static_cast<std::size_t>(n) * n * n);
        for (std::uint32_t z = 0; z < n; ++z)
            for (std::uint32_t y = 0; y < n; ++y)
                for (std::uint32_t x = 0; x < n; ++x)
                    chunk.values.push_back(inside({p.x + x, p.y + y, p.z + z}) ? 255 : 0);
    }
    chunk.occupied = chunk.state == ChunkState::Solid ?
        config_.volume.chunk_size * config_.volume.chunk_size * config_.volume.chunk_size :
        static_cast<std::uint32_t>(std::count(chunk.values.begin(), chunk.values.end(), VoxelValue{255}));
    return chunks_.emplace(c, std::move(chunk)).first->second;
}
bool SparseVoxelVolume::erase(VoxelCoord p) {
    if (!inside(p) || sample(p) != 255) return false;
    const auto c = voxel_to_chunk(p);
    materialize_chunk(c);
    auto& chunk = chunks_.at(c);
    const auto n = config_.volume.chunk_size;
    if (chunk.state == ChunkState::Solid) {
        chunk.values.assign(static_cast<std::size_t>(n) * n * n, 255);
        chunk.state = ChunkState::Mixed;
    }
    const auto local = chunk_local(p);
    chunk.values[static_cast<std::size_t>(local.x + n * (local.y + n * local.z))] = 0;
    ++version_; ++removed_; ++chunk.version;
    if (--chunk.occupied == 0) {
        chunk.state = ChunkState::Empty;
        std::vector<VoxelValue>().swap(chunk.values);
    }
    return true;
}
std::uint32_t SparseVoxelVolume::erase_chunk_voxels(ChunkCoord c, std::span<const std::uint32_t> indices) {
    const auto n=config_.volume.chunk_size, count=n*n*n;
    for (auto index : indices) if (index>=count) throw std::out_of_range("chunk voxel index out of range");
    if (indices.empty() || chunk_state(c)==ChunkState::Empty) return 0;
    materialize_chunk(c);
    auto& chunk=chunks_.at(c);
    if (chunk.state==ChunkState::Solid) {
        chunk.values.assign(count,255); chunk.state=ChunkState::Mixed;
    }
    std::uint32_t removed=0;
    for (auto index : indices) if (chunk.values[index]==255) { chunk.values[index]=0; ++removed; }
    chunk.occupied-=removed; chunk.version+=removed; version_+=removed; removed_+=removed;
    if (!chunk.occupied) { chunk.state=ChunkState::Empty; std::vector<VoxelValue>().swap(chunk.values); }
    return removed;
}
std::uint64_t SparseVoxelVolume::chunk_version(ChunkCoord c) const {
    const auto it = chunks_.find(c);
    return it == chunks_.end() ? 0 : it->second.version;
}
VoxelCoord SparseVoxelVolume::local_to_voxel(glm::dvec3 mm) const {
    VoxelCoord result;
    for (int i = 0; i < 3; ++i) {
        const double cells = mm[i] / config_.volume.voxel_size_mm;
        const auto nearest = std::round(cells);
        const double v = std::floor(std::abs(cells - nearest) <=
            4 * std::numeric_limits<double>::epsilon() * std::abs(cells) ? nearest : cells);
        if (!std::isfinite(v) || v < -static_cast<double>(grid_limit) || v > static_cast<double>(grid_limit))
            throw std::out_of_range("position outside supported voxel range");
        result[i] = static_cast<std::int64_t>(v);
    }
    return result;
}
glm::dvec3 SparseVoxelVolume::voxel_to_local(VoxelCoord p) const {
    return glm::dvec3(p.x, p.y, p.z) * config_.volume.voxel_size_mm;
}
ChunkCoord SparseVoxelVolume::voxel_to_chunk(VoxelCoord p) const {
    const auto n = config_.volume.chunk_size;
    return {floor_div(p.x, n), floor_div(p.y, n), floor_div(p.z, n)};
}
VoxelCoord SparseVoxelVolume::chunk_local(VoxelCoord p) const {
    const auto n = config_.volume.chunk_size;
    auto mod = [n](std::int64_t v) { const auto r = v % n; return r < 0 ? r + n : r; };
    return {mod(p.x), mod(p.y), mod(p.z)};
}
ChunkCoord SparseVoxelVolume::last_chunk() const {
    return voxel_to_chunk({dimensions_.x - 1, dimensions_.y - 1, dimensions_.z - 1});
}
std::size_t SparseVoxelVolume::stored_voxel_bytes() const {
    std::size_t total = 0;
    for (const auto& [coord, chunk] : chunks_) { (void)coord; total += chunk.values.size(); }
    return total;
}
} // namespace cnc
