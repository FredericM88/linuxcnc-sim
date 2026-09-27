#include "meshing/SurfaceMesher.hpp"
#include <array>

namespace cnc {
std::vector<ChunkCoord> DirtyChunks::take() {
    std::vector<ChunkCoord> result(chunks_.begin(), chunks_.end());
    chunks_.clear();
    return result;
}
ChunkMesh SurfaceMesher::build(const SparseVoxelVolume& volume, ChunkCoord chunk) const {
    ChunkMesh mesh;
    const auto state = volume.chunk_state(chunk);
    if (state == ChunkState::Empty) return mesh;
    const auto origin = volume.chunk_origin(chunk);
    const int n = static_cast<int>(volume.config().volume.chunk_size);
    std::vector<std::uint8_t> mask(static_cast<std::size_t>(n * n));
    for (int axis = 0; axis < 3; ++axis) {
        const int u = (axis + 1) % 3, v = (axis + 2) % 3;
        for (int sign : {-1, 1}) {
            for (int slice = 0; slice < n; ++slice) {
                if (state == ChunkState::Solid && slice != (sign < 0 ? 0 : n - 1)) continue;
                for (int j = 0; j < n; ++j) for (int i = 0; i < n; ++i) {
                    auto p = origin;
                    p[axis] += slice; p[u] += i; p[v] += j;
                    auto neighbour = p; neighbour[axis] += sign;
                    mask[static_cast<std::size_t>(j * n + i)] =
                        volume.sample(p) != 0 && volume.sample(neighbour) == 0;
                }
                // Merge coplanar exposed faces inside each chunk. Neighbour
                // samples cross chunk boundaries, so no interior faces appear.
                for (int j = 0; j < n; ++j) for (int i = 0; i < n;) {
                    if (!mask[static_cast<std::size_t>(j * n + i)]) { ++i; continue; }
                    int width = 1, height = 1;
                    while (i + width < n && mask[static_cast<std::size_t>(j * n + i + width)]) ++width;
                    bool extend = true;
                    while (j + height < n && extend) {
                        for (int k = 0; k < width; ++k)
                            if (!mask[static_cast<std::size_t>((j + height) * n + i + k)]) extend = false;
                        if (extend) ++height;
                    }
                    auto p = origin;
                    p[axis] += slice + (sign > 0 ? 1 : 0); p[u] += i; p[v] += j;
                    auto pu = p, pv = p, puv = p;
                    pu[u] += width; pv[v] += height; puv[u] += width; puv[v] += height;
                    glm::vec3 normal(0); normal[axis] = static_cast<float>(sign);
                    const auto index = static_cast<std::uint32_t>(mesh.vertices.size());
                    // Convert identical integer corners identically across chunks.
                    for (auto point : {p, pu, puv, pv}) mesh.vertices.push_back({glm::vec3(volume.voxel_to_local(point)), normal});
                    for (auto offset : (sign > 0 ? std::array{0u,1u,2u,0u,2u,3u} : std::array{0u,2u,1u,0u,3u,2u}))
                        mesh.indices.push_back(index + offset);
                    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x)
                        mask[static_cast<std::size_t>((j + y) * n + i + x)] = 0;
                    i += width;
                }
            }
        }
    }
    return mesh;
}
} // namespace cnc
