#pragma once
#include <chrono>
#include <future>
#include <mutex>
#include <thread>
#include "material/MaterialRemoval.hpp"
#include "material/MotionQueue.hpp"
#include "simulation/Workpiece.hpp"

namespace cnc {
using MeshDirectory = std::unordered_map<ChunkCoord, std::shared_ptr<const ChunkMesh>, ChunkCoordHash>;
struct MaterialMeshes {
    std::shared_ptr<const WorkpieceSnapshot> workpiece;
    std::uint64_t generation{}, revision{};
    ToolDefinition tool;
    MeshDirectory chunks; // Complete directory: skipped publications cannot lose updates.
    std::uint64_t material_version{};
};
struct MaterialWorkerConfig {
    unsigned material_workers{1};
    unsigned mesh_workers{}; // 0: automatic, reserve two logical CPUs, cap at four.
    unsigned material_batch_ms{20}; // 0: segmentwise reference; otherwise event-time windows, 1..1000 ms.
};
MaterialWorkerConfig resolve_material_workers(MaterialWorkerConfig config);
struct MaterialStatus {
    unsigned material_workers{1}, mesh_workers{1}, mesh_parallel_max{};
    unsigned material_batch_ms{20};
    RemovalStats removal;
    ToolDefinition tool;
    bool enabled{};
    std::uint64_t queue_depth{}, max_queue_depth{}, events_processed{}, dirty_chunks{}, mesh_rebuilds{}, generation{};
    std::uint64_t motion_received{}, motion_coalesced{}, mesh_queue_depth{}, mesh_queue_max{};
    std::uint64_t stale_mesh_jobs{};
    double snapshot_ms{}, coalescing_ms{};
    double worker_ms{}, mesh_ms{}, publication_ms{}, lag_ms{}, removed_volume_mm3{};
    std::string error;
};
enum class MaterialEventKind { Motion, Enable, Disable, Tool, Reset, Workpiece, Capture };
struct MaterialEvent {
    MaterialEventKind kind{MaterialEventKind::Motion};
    glm::dvec3 from{}, to{};
    ToolDefinition tool;
    std::shared_ptr<const WorkpieceSnapshot> workpiece;
    std::shared_ptr<std::promise<std::shared_ptr<const SparseVoxelVolume>>> capture;
    std::chrono::steady_clock::time_point time{std::chrono::steady_clock::now()};
};
class MaterialWorker {
public:
    explicit MaterialWorker(std::shared_ptr<const WorkpieceSnapshot> raw, MaterialWorkerConfig config = {});
    ~MaterialWorker();
    // Exactly one producer: Simulation's authoritative UDP thread.
    void enqueue(MaterialEvent event) { queue_.push(std::move(event)); }
    void finish(); // Producer must be stopped first; drains ALL queued events and dirty meshes.
    bool failed() const { return failed_.load(std::memory_order_acquire); }
    MaterialStatus status() const;
    std::shared_ptr<const MaterialMeshes> meshes() const;
private:
    void run();
    MotionQueue<MaterialEvent> queue_;
    std::shared_ptr<const WorkpieceSnapshot> raw_;
    std::unique_ptr<MaterialRemoval> engine_;
    MeshDirectory meshes_;
    MaterialStatus state_;
    mutable std::mutex publication_;
    MaterialStatus published_;
    std::shared_ptr<const MaterialMeshes> published_meshes_;
    std::atomic<bool> finishing_{false}, failed_{false};
    std::jthread worker_;
};
} // namespace cnc
