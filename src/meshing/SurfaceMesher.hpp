#pragma once
#include <unordered_set>
#include "volume/SparseVoxelVolume.hpp"

namespace cnc {
struct Vertex { glm::vec3 position; glm::vec3 normal; };
struct ChunkMesh { std::vector<Vertex> vertices; std::vector<std::uint32_t> indices; };
class IMeshExtractor {
public:
    virtual ~IMeshExtractor() = default;
    virtual ChunkMesh build(const SparseVoxelVolume& volume, ChunkCoord chunk) const = 0;
};
class SurfaceMesher final : public IMeshExtractor {
public:
    ChunkMesh build(const SparseVoxelVolume& volume, ChunkCoord chunk) const override;
};
// Single-owner, deduplicated material/halo work list.
class DirtyChunks {
public:
    void mark(ChunkCoord c) { chunks_.insert(c); }
    std::vector<ChunkCoord> take();
    std::size_t size() const { return chunks_.size(); }
    bool pop(ChunkCoord& c) {
        if (chunks_.empty()) return false;
        auto it = chunks_.begin(); c = *it; chunks_.erase(it); return true;
    }
private:
    std::unordered_set<ChunkCoord, ChunkCoordHash> chunks_;
};
} // namespace cnc
