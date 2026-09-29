#include "material/MaterialWorker.hpp"
#include "material/MotionCoalescer.hpp"
#include "meshing/MeshWorkers.hpp"

namespace cnc {
namespace {
using Clock = std::chrono::steady_clock;
double milliseconds(Clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); }
}
MaterialWorkerConfig resolve_material_workers(MaterialWorkerConfig config) {
    if (config.material_workers!=1) throw std::invalid_argument("material workers must be 1 (single authoritative material owner)");
    if (config.mesh_workers>32) throw std::invalid_argument("mesh workers must be in 0..32 (0: automatic)");
    if (config.material_batch_ms>1000) throw std::invalid_argument("material batch interval must be in 0..1000 ms (0: segmentwise reference)");
    if (!config.mesh_workers) {
        const auto cpus=std::thread::hardware_concurrency();
        config.mesh_workers=std::min(4u,cpus>2 ? cpus-2 : 1u);
    }
    return config;
}
MaterialWorker::MaterialWorker(std::shared_ptr<const WorkpieceSnapshot> raw, MaterialWorkerConfig config)
    : raw_(std::move(raw)), engine_(std::make_unique<MaterialRemoval>(*raw_->volume)) {
    config=resolve_material_workers(config);
    state_.material_workers=config.material_workers; state_.mesh_workers=config.mesh_workers;
    state_.material_batch_ms=config.material_batch_ms;
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
    std::unique_ptr<MeshWorkers> mesh_workers;
    std::shared_ptr<MeshBatch> batch;
    std::uint64_t mesh_revision = 0;
    auto last_publish = Clock::now() - std::chrono::seconds(1);
    bool meshes_changed = true;
    auto publish = [&] {
        const auto publication_start = Clock::now();
        state_.removal = engine_->stats(); state_.dirty_chunks = engine_->dirty().size() + (batch ? batch->chunks.size() : 0);
        const auto h = engine_->volume().config().volume.voxel_size_mm;
        state_.removed_volume_mm3 = static_cast<double>(state_.removal.voxels_removed) * h*h*h;
        std::shared_ptr<const MaterialMeshes> next;
        if (meshes_changed && !batch && engine_->dirty().size() == 0) next = std::make_shared<const MaterialMeshes>(MaterialMeshes{
            raw_, state_.generation, ++mesh_revision, state_.tool, meshes_, engine_->volume().version()});
        // Destruction of old mesh directories occurs outside the publication lock.
        std::shared_ptr<const MaterialMeshes> old;
        {
            std::lock_guard lock(publication_);
            published_ = state_;
            if (next) { old = std::move(published_meshes_); published_meshes_ = std::move(next); meshes_changed = false; }
        }
        last_publish = Clock::now();
        state_.publication_ms += milliseconds(last_publish - publication_start);
    };
    MaterialEvent pending_motion;
    bool has_pending_motion = false;
    Clock::time_point pending_since;
    std::vector<SweepSegment> sweep_segments;
    std::shared_ptr<std::promise<std::shared_ptr<const SparseVoxelVolume>>> active_capture;
    bool window_open=false;
    Clock::time_point window_start, last_motion_time;
    const auto interval=std::chrono::milliseconds(state_.material_batch_ms);
    auto apply_union=[&] {
        if (!sweep_segments.empty()) {
            const auto start=Clock::now();
            engine_->sweep_batch(sweep_segments,state_.tool);
            state_.worker_ms+=milliseconds(Clock::now()-start);
            sweep_segments.clear();
        }
        window_open=false;
    };
    try {
        sweep_segments.reserve(256);
        mesh_workers=std::make_unique<MeshWorkers>(state_.mesh_workers);
        for (;;) {
            const auto batch_start = Clock::now();
            MaterialEvent event;
            unsigned processed = 0;
            const auto prior_worker_ms=state_.worker_ms;
            auto process = [&](MaterialEvent event) {
                const auto start = Clock::now();
                state_.lag_ms = milliseconds(start - event.time);
                switch (event.kind) {
                case MaterialEventKind::Motion:
                    ++state_.motion_coalesced;
                    if (state_.enabled) {
                        if (state_.material_batch_ms) sweep_segments.push_back({event.from,event.to});
                        else engine_->sweep(event.from,event.to,state_.tool);
                    }
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
                state_.worker_ms += milliseconds(Clock::now() - start);
            };
            auto flush = [&] {
                if (has_pending_motion) { process(std::move(pending_motion)); pending_motion = {}; has_pending_motion = false; }
            };
            while (processed < 65536 && milliseconds(Clock::now() - batch_start) < 20 && queue_.pop(event)) {
                ++state_.events_processed; ++processed;
                active_capture=event.capture; // A preceding union may fail before this Capture executes.
                if (event.kind == MaterialEventKind::Motion) {
                    ++state_.motion_received;
                    // Half-open windows use source event time, never worker processing
                    // duration. A whole input segment belongs to its endpoint event.
                    if (state_.material_batch_ms) {
                        if (window_open && (event.time-window_start>=interval || event.time<last_motion_time)) {
                            flush(); apply_union();
                        }
                        if (!window_open) { window_start=event.time; window_open=true; }
                        last_motion_time=event.time;
                    }
                    if (has_pending_motion && pending_motion.to == event.from &&
                        exact_straight_extension(pending_motion.from, pending_motion.to, event.to)) {
                        pending_motion.to = event.to;
                    } else {
                        flush();
                        // Bound retained geometry even for a stalled/repeated event clock.
                        if (sweep_segments.size()>=256) { apply_union(); window_start=event.time; window_open=true; }
                        pending_motion = std::move(event); has_pending_motion = true; pending_since = Clock::now();
                    }
                } else { flush(); apply_union(); process(std::move(event)); }
                event = {}; active_capture.reset();
            }
            if (state_.material_batch_ms) {
                // Idle/stop commits a short window at its event-time deadline. A
                // transient empty FIFO between samples does not split the window.
                if (window_open && queue_.depth()==0 &&
                    (finishing_.load(std::memory_order_acquire) || Clock::now()-window_start>=interval)) {
                    flush(); apply_union();
                }
            } else if (has_pending_motion && (finishing_.load(std::memory_order_acquire) ||
                milliseconds(Clock::now() - pending_since) >= 20)) flush();
            state_.coalescing_ms += milliseconds(Clock::now()-batch_start) - (state_.worker_ms-prior_worker_ms);
            if (window_open) state_.lag_ms=std::max(0.0,milliseconds(Clock::now()-window_start));
            else if (has_pending_motion) state_.lag_ms=std::max(0.0,milliseconds(Clock::now()-pending_motion.time));
            // Collect finished jobs without ever waiting for meshing. Compare the
            // owner chunk AND its face halo; halo-only changes invalidate meshes too.
            if (batch && batch->remaining.load(std::memory_order_acquire)==0) {
                for (std::size_t i=0; i<batch->chunks.size(); ++i) {
                    auto& result=batch->results[i];
                    if (result.error) std::rethrow_exception(result.error);
                    state_.mesh_ms += result.build_ms; ++state_.mesh_rebuilds;
                    const auto c=batch->chunks[i];
                    bool valid=batch->generation==state_.generation;
                    if (valid) {
                        valid=batch->volume->chunk_version(c)==engine_->volume().chunk_version(c);
                        for (int axis=0; axis<3; ++axis) for (int sign : {-1,1}) {
                            auto neighbour=c; neighbour[axis]+=sign;
                            valid &= batch->volume->chunk_version(neighbour)==engine_->volume().chunk_version(neighbour);
                        }
                        if (valid) { meshes_[c]=std::move(result.mesh); meshes_changed=true; }
                        else engine_->dirty().mark(c);
                    }
                    if (!valid) ++state_.stale_mesh_jobs;
                }
                state_.mesh_parallel_max=std::max(state_.mesh_parallel_max,batch->parallel_max.load());
                batch.reset();
            }
            if (!batch && engine_->dirty().size()) {
                const auto snapshot_start=Clock::now();
                batch=std::make_shared<MeshBatch>();
                batch->volume=std::make_shared<const SparseVoxelVolume>(engine_->volume());
                batch->generation=state_.generation;
                batch->chunks=engine_->dirty().take();
                batch->results.resize(batch->chunks.size());
                batch->remaining.store(batch->chunks.size(), std::memory_order_relaxed);
                state_.snapshot_ms += milliseconds(Clock::now()-snapshot_start);
                state_.mesh_queue_max=std::max(state_.mesh_queue_max,static_cast<std::uint64_t>(batch->chunks.size()));
                mesh_workers->submit(batch);
            }
            state_.mesh_queue_depth=engine_->dirty().size() + (batch ? batch->remaining.load(std::memory_order_acquire) : 0);
            state_.mesh_queue_max=std::max(state_.mesh_queue_max,state_.mesh_queue_depth);
            const bool idle = queue_.depth() == 0 && !has_pending_motion && !window_open && !batch && engine_->dirty().size() == 0;
            if (idle) state_.lag_ms = 0;
            if (milliseconds(Clock::now() - last_publish) >= 20 || (!batch && meshes_changed)) publish();
            if (finishing_.load(std::memory_order_acquire) && idle) break;
            if (!processed) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        publish();
    } catch (const std::exception& e) {
        // Do not continue with an apparently valid prefix. Simulation observes this fatal error.
        state_.error = std::string("material worker failed; simulation incomplete: ") + e.what();
        { std::lock_guard lock(publication_); published_ = state_; }
        failed_.store(true, std::memory_order_release);
        // Also fail a Capture already popped from the FIFO. Its producer may
        // still own the promise, so destroying the event would not unblock it.
        if (active_capture) {
            active_capture->set_exception(std::make_exception_ptr(std::runtime_error(state_.error)));
            active_capture.reset();
        }
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
