#include "material/MaterialWorker.hpp"
#include <iostream>
#include <iomanip>
#include <stdexcept>
using namespace cnc;
int main(int argc, char** argv) {
    const int segments = argc > 1 ? std::stoi(argv[1]) : 4000;
    auto raw = std::make_shared<WorkpieceSnapshot>();
    SceneConfig scene; scene.stock_size_mm={30,20,10}; scene.workpiece.translation={0,0,-20};
    raw->volume = std::make_shared<const SparseVoxelVolume>(scene);
    const auto start = std::chrono::steady_clock::now();
#ifdef CNC_BENCH_BASELINE
    MaterialWorker worker(raw);
#else
    MaterialWorker worker(raw, {1,argc>2 ? static_cast<unsigned>(std::stoul(argv[2])) : 0});
#endif
    const unsigned pace_us=argc>3 ? static_cast<unsigned>(std::stoul(argv[3])) : 0;
    MaterialEvent on; on.kind=MaterialEventKind::Enable; on.to={5,10,-5}; worker.enqueue(on);
    MaterialEvent plunge; plunge.from=on.to; plunge.to={5,10,-12}; worker.enqueue(plunge);
    auto previous=plunge.to;
    const auto motion_start=std::chrono::steady_clock::now();
    for (int i=1; i<=segments; ++i) {
        MaterialEvent e; e.from=previous; e.to={5+20.*i/segments,10,-12};
        worker.enqueue(e); previous=e.to;
        if (pace_us) std::this_thread::sleep_until(motion_start+std::chrono::microseconds(static_cast<std::int64_t>(i)*pace_us));
    }
    MaterialEvent capture; capture.kind=MaterialEventKind::Capture;
    capture.capture=std::make_shared<std::promise<std::shared_ptr<const SparseVoxelVolume>>>();
    auto future=capture.capture->get_future(); worker.enqueue(capture);
    worker.finish(); const auto s=worker.status();
    const auto wall_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    const auto snapshot=future.get();
    // Exact binary occupancy comparison, outside benchmark timing.
    MaterialRemoval reference(*raw->volume);
    reference.sweep({5,10,-5},{5,10,-12},{});
    reference.sweep({5,10,-12},{25,10,-12},{});
    const auto dims=snapshot->dimensions();
    for (std::int64_t z=0;z<dims.z;++z) for (std::int64_t y=0;y<dims.y;++y) for (std::int64_t x=0;x<dims.x;++x)
        if (snapshot->sample({x,y,z})!=reference.volume().sample({x,y,z})) throw std::runtime_error("occupancy regression");
    if (!s.error.empty() || s.removal.voxels_removed!=296560 || s.queue_depth || s.dirty_chunks)
        throw std::runtime_error("benchmark regression");
    std::cout << std::fixed << std::setprecision(3) << "segments=" << segments
        << " events=" << s.events_processed << " sweeps=" << s.removal.sweeps
        << " tested=" << s.removal.voxels_tested << " removed=" << s.removal.voxels_removed
        << " chunks_tested=" << s.removal.chunks_tested << " chunks_changed=" << s.removal.chunks_changed
        << " queue_max=" << s.max_queue_depth << " rebuilds=" << s.mesh_rebuilds
        << " material_ms=" << s.worker_ms << " mesh_ms=" << s.mesh_ms
        << " wall_ms=" << wall_ms
#ifndef CNC_BENCH_BASELINE
        << " motions=" << s.motion_received << " coalesced=" << s.motion_coalesced
        << " mesh_queue_max=" << s.mesh_queue_max << " material_workers=" << s.material_workers
        << " mesh_workers=" << s.mesh_workers << " parallel_peak=" << s.mesh_parallel_max
        << " broad_ms=" << s.removal.broad_ms << " narrow_ms=" << s.removal.narrow_ms
        << " mutation_ms=" << s.removal.mutation_ms << " invalidation_ms=" << s.removal.invalidation_ms
        << " coalescing_ms=" << s.coalescing_ms << " snapshot_ms=" << s.snapshot_ms
        << " publication_ms=" << s.publication_ms << " stale_jobs=" << s.stale_mesh_jobs
#endif
        << '\n';
}
