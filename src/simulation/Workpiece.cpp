#include "simulation/Workpiece.hpp"
#include <cmath>
#include <stdexcept>

namespace cnc {
void validate_workpiece_volume(const SparseVoxelVolume& volume) {
    if (volume.config().volume.chunk_size != VolumeConfig{}.chunk_size)
        throw std::invalid_argument("workpiece chunk size must remain 32 x 32 x 32");
    const auto last = volume.last_chunk();
    const long double count = (static_cast<long double>(last.x) + 1) *
        (static_cast<long double>(last.y) + 1) * (static_cast<long double>(last.z) + 1);
    if (count > max_workpiece_chunks)
        throw std::invalid_argument("workpiece exceeds 65536 chunks; increase voxel size or reduce size");
    // Reserve ample float headroom for camera/matrix arithmetic as well as vertices.
    constexpr double limit = 1e12;
    for (int corner = 0; corner < 8; ++corner) {
        glm::dvec3 local(0);
        for (int i = 0; i < 3; ++i) if (corner & (1 << i)) local[i] = volume.extent_mm()[i];
        const auto world = volume.config().workpiece.to_machine(local);
        for (int i = 0; i < 3; ++i)
            if (!std::isfinite(world[i]) || std::abs(world[i]) > limit || volume.extent_mm()[i] > limit)
                throw std::invalid_argument("workpiece bounds exceed supported +/-1e12 mm range");
    }
}
std::shared_ptr<const SparseVoxelVolume> build_workpiece(const WorkpieceConfig& config) {
    for (int i = 0; i < 3; ++i) {
        if (!std::isfinite(config.size_mm[i]) || config.size_mm[i] <= 0)
            throw std::invalid_argument("workpiece size must be finite and positive");
        if (!std::isfinite(config.position_machine_mm[i]))
            throw std::invalid_argument("workpiece position must be finite");
        if (!std::isfinite(config.origin_offset_mm[i]) || config.origin_offset_mm[i] < 0 ||
            config.origin_offset_mm[i] > config.size_mm[i])
            throw std::invalid_argument("workpiece origin must be within [0, size] on each axis; change origin before shrinking size");
        if (!std::isfinite(config.machine_min()[i]) || !std::isfinite(config.machine_max()[i]))
            throw std::invalid_argument("workpiece bounds overflow");
    }
    SceneConfig scene;
    scene.stock_size_mm = config.size_mm;
    scene.volume.voxel_size_mm = config.voxel_size_mm;
    scene.workpiece.translation = config.machine_min();
    auto volume = std::make_shared<const SparseVoxelVolume>(scene);
    validate_workpiece_volume(*volume);
    return volume;
}
} // namespace cnc
