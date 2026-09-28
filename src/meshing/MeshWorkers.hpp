#pragma once
#include <atomic>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <thread>
#include "meshing/SurfaceMesher.hpp"

namespace cnc {
struct MeshResult {
    std::shared_ptr<const ChunkMesh> mesh;
    std::exception_ptr error;
    double build_ms{};
};
// One immutable volume per batch. Each result slot has exactly one writer and is
// read only after remaining reaches zero with acquire/release synchronization.
struct MeshBatch {
    std::shared_ptr<const SparseVoxelVolume> volume;
    std::uint64_t generation{};
    std::vector<ChunkCoord> chunks;
    std::vector<MeshResult> results;
    std::atomic<std::size_t> remaining{};
    std::atomic<unsigned> active{}, parallel_max{};
};
class MeshWorkers {
public:
    explicit MeshWorkers(unsigned count);
    ~MeshWorkers();
    void submit(std::shared_ptr<MeshBatch> batch); // Sole material owner; previous batch completed.
private:
    void run();
    std::mutex mutex_;
    std::condition_variable wake_;
    std::shared_ptr<MeshBatch> batch_;
    std::size_t next_{};
    bool stopping_{};
    std::vector<std::jthread> threads_;
};
} // namespace cnc
