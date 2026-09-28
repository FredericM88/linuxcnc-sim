#include "meshing/MeshWorkers.hpp"
#include <chrono>
#include <stdexcept>

namespace cnc {
MeshWorkers::MeshWorkers(unsigned count) {
    if (!count || count>32) throw std::invalid_argument("mesh workers must be in 1..32");
    try {
        for (unsigned i=0; i<count; ++i) threads_.emplace_back([this] { run(); });
    } catch (...) {
        { std::lock_guard lock(mutex_); stopping_=true; }
        wake_.notify_all();
        for (auto& thread : threads_) thread.join();
        throw;
    }
}
MeshWorkers::~MeshWorkers() {
    { std::lock_guard lock(mutex_); stopping_=true; }
    wake_.notify_all();
    for (auto& thread : threads_) thread.join();
}
void MeshWorkers::submit(std::shared_ptr<MeshBatch> batch) {
    { std::lock_guard lock(mutex_); batch_=std::move(batch); next_=0; }
    wake_.notify_all();
}
void MeshWorkers::run() {
    SurfaceMesher mesher;
    for (;;) {
        std::shared_ptr<MeshBatch> batch;
        std::size_t index;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock,[&] { return stopping_ || (batch_ && next_<batch_->chunks.size()); });
            if (stopping_) return;
            batch=batch_; index=next_++;
        }
        const auto active=batch->active.fetch_add(1,std::memory_order_relaxed)+1;
        auto maximum=batch->parallel_max.load(std::memory_order_relaxed);
        while (maximum<active && !batch->parallel_max.compare_exchange_weak(maximum,active,std::memory_order_relaxed)) {}
        const auto start=std::chrono::steady_clock::now();
        auto& result=batch->results[index];
        try { result.mesh=std::make_shared<const ChunkMesh>(mesher.build(*batch->volume,batch->chunks[index])); }
        catch (...) { result.error=std::current_exception(); }
        result.build_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        batch->active.fetch_sub(1,std::memory_order_relaxed);
        batch->remaining.fetch_sub(1,std::memory_order_acq_rel);
    }
}
} // namespace cnc
