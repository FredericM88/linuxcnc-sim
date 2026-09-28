#include "material/MaterialWorker.hpp"

namespace cnc {
namespace {
using Clock = std::chrono::steady_clock;
double milliseconds(Clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); }
}
MaterialWorker::MaterialWorker(std::shared_ptr<const WorkpieceSnapshot> raw)
    : raw_(std::move(raw)), engine_(std::make_unique<MaterialRemoval>(*raw_->volume)) {
    engine_->dirty_all(); state_.generation = 1;
    worker_ = std::jthread([this] { run(); });
}
MaterialWorker::~MaterialWorker() { finish(); }
void MaterialWorker::finish() {
    finishing_.store(true, std::memory_order_release);
    if (worker_.joinable()) worker_.join();
}
MaterialStatus MaterialWorker::status() const {
    std::lock_guard lock(publication_);
    auto result = published_;
    result.queue_depth = queue_.depth(); result.max_queue_depth = queue_.maximum();
    return result;
}
std::shared_ptr<const MaterialMeshes> MaterialWorker::meshes() const {
    std::lock_guard lock(publication_); return published_meshes_;
}
void MaterialWorker::run() {
    SurfaceMesher mesher;
    std::uint64_t mesh_revision = 0;
    auto last_publish = Clock::now() - std::chrono::seconds(1);
    bool meshes_changed = true;
    bool finishing_meshes = false;
    auto publish = [&] {
        state_.removal = engine_->stats(); state_.dirty_chunks = engine_->dirty().size();
        const auto h = engine_->volume().config().volume.voxel_size_mm;
        state_.removed_volume_mm3 = static_cast<double>(state_.removal.voxels_removed) * h*h*h;
        std::shared_ptr<const MaterialMeshes> next;
        if (meshes_changed && engine_->dirty().size() == 0) next = std::make_shared<const MaterialMeshes>(MaterialMeshes{
            raw_, state_.generation, ++mesh_revision, state_.tool, meshes_});
        // Destruction of old mesh directories occurs outside the publication lock.
        std::shared_ptr<const MaterialMeshes> old;
        {
            std::lock_guard lock(publication_);
            published_ = state_;
            if (next) { old = std::move(published_meshes_); published_meshes_ = std::move(next); meshes_changed = false; }
        }
        last_publish = Clock::now();
    };
    try {
        for (;;) {
            const auto batch_start = Clock::now();
            MaterialEvent event;
            unsigned processed = 0;
            while (!finishing_meshes && processed < 256 && milliseconds(Clock::now() - batch_start) < 20 && queue_.pop(event)) {
                const auto start = Clock::now();
                state_.lag_ms = milliseconds(start - event.time);
                switch (event.kind) {
                case MaterialEventKind::Motion:
                    if (state_.enabled) engine_->sweep(event.from, event.to, state_.tool);
                    break;
                case MaterialEventKind::Enable:
                    if (!state_.enabled) engine_->sweep(event.to, event.to, state_.tool);
                    state_.enabled = true; break;
                case MaterialEventKind::Disable: state_.enabled = false; break;
                case MaterialEventKind::Tool:
                    state_.tool = event.tool; meshes_changed = true;
                    if (state_.enabled) engine_->sweep(event.to, event.to, state_.tool);
                    break;
                case MaterialEventKind::Workpiece:
                    raw_ = std::move(event.workpiece);
                    [[fallthrough]];
                case MaterialEventKind::Reset:
                    engine_ = std::make_unique<MaterialRemoval>(*raw_->volume);
                    engine_->dirty_all(); meshes_.clear(); meshes_changed = true;
                    ++state_.generation;
                    break;
                case MaterialEventKind::Capture:
                    event.capture->set_value(std::make_shared<const SparseVoxelVolume>(engine_->volume()));
                    break;
                }
                ++state_.events_processed; ++processed;
                state_.worker_ms += milliseconds(Clock::now() - start);
                event = {}; // Release potentially large event payloads on this worker.
            }
            // Finish this batch's complete halo update before consuming more motion.
            // This ensures coherent progressive render checkpoints even during sustained cuts.
            finishing_meshes = engine_->dirty().size() != 0;
            const auto mesh_start = Clock::now();
            ChunkCoord c;
            // Bounded slices allow diagnostic publication throughout large coherent rebuilds.
            for (int i = 0; i < 8 && engine_->dirty().pop(c); ++i) {
                auto mesh = std::make_shared<const ChunkMesh>(mesher.build(engine_->volume(), c));
                meshes_[c] = std::move(mesh); // Empty mesh explicitly removes old GPU geometry.
                ++state_.mesh_rebuilds; meshes_changed = true;
            }
            state_.mesh_ms += milliseconds(Clock::now() - mesh_start);
            if (engine_->dirty().size() == 0) finishing_meshes = false;
            const bool idle = queue_.depth() == 0 && engine_->dirty().size() == 0;
            if (idle) state_.lag_ms = 0;
            if (milliseconds(Clock::now() - last_publish) >= 20 || (!finishing_meshes && meshes_changed)) publish();
            if (finishing_.load(std::memory_order_acquire) && idle) break;
            if (!processed && idle) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        publish();
    } catch (const std::exception& e) {
        // Do not continue with an apparently valid prefix. Simulation observes this fatal error.
        state_.error = std::string("material worker failed; simulation incomplete: ") + e.what();
        { std::lock_guard lock(publication_); published_ = state_; }
        failed_.store(true, std::memory_order_release);
        // Unblock any diagnostic captures while the UDP producer is being stopped.
        while (!finishing_.load(std::memory_order_acquire) || queue_.depth()) {
            MaterialEvent event;
            if (queue_.pop(event)) {
                if (event.capture) event.capture->set_exception(std::make_exception_ptr(std::runtime_error(state_.error)));
            } else std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}
} // namespace cnc
