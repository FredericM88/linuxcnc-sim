#include "material/MaterialWorker.hpp"
#include "meshing/MeshWorkers.hpp"
#include <iostream>
#include <stdexcept>
using namespace cnc;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while(false)
bool equal_mesh(const ChunkMesh& a, const ChunkMesh& b) {
    if(a.indices!=b.indices || a.vertices.size()!=b.vertices.size()) return false;
    for(std::size_t i=0;i<a.vertices.size();++i)
        if(a.vertices[i].position!=b.vertices[i].position || a.vertices[i].normal!=b.vertices[i].normal) return false;
    return true;
}
std::future<std::shared_ptr<const SparseVoxelVolume>> capture(MaterialWorker& worker) {
    MaterialEvent e; e.kind=MaterialEventKind::Capture;
    e.capture=std::make_shared<std::promise<std::shared_ptr<const SparseVoxelVolume>>>();
    auto future=e.capture->get_future(); worker.enqueue(e); return future;
}
int main() {
    try {
        for (unsigned workers : {1u,2u,4u}) {
            SceneConfig scene; scene.stock_size_mm={16,16,8}; scene.volume.voxel_size_mm=.25;
            auto raw=std::make_shared<WorkpieceSnapshot>(); raw->volume=std::make_shared<const SparseVoxelVolume>(scene);
            MaterialWorker worker(raw,{1,workers});
            std::atomic<bool> done{false}, valid{true};
            std::jthread reader([&](std::stop_token stop) {
                std::uint64_t generation=0, version=0, revision=0;
                while(!done.load() && !stop.stop_requested()) {
                    const auto meshes=worker.meshes(); (void)worker.status();
                    if(meshes) {
                        if(meshes->generation<generation || meshes->revision<revision ||
                            (meshes->generation==generation && meshes->material_version<version)) valid=false;
                        generation=meshes->generation; version=meshes->material_version; revision=meshes->revision;
                        for(const auto& [c,m] : meshes->chunks) { (void)c; if(m->indices.size()%3) valid=false; }
                    }
                    std::this_thread::yield();
                }
            });
            MaterialEvent on; on.kind=MaterialEventKind::Enable; on.to={2,4,-5}; worker.enqueue(on);
            auto initial=capture(worker).get();
            for(int round=0;round<12;++round) {
                MaterialEvent e; e.from=on.to; e.to={14,4+double(round)/2,-5}; worker.enqueue(e);
                // A capture is an ordering barrier even while jobs from an earlier state run.
                auto snapshot=capture(worker).get(); CHECK(snapshot->removed_voxels()>0);
                MaterialEvent reset; reset.kind=MaterialEventKind::Reset; worker.enqueue(reset);
            }
            auto moved=std::make_shared<WorkpieceSnapshot>(*raw);
            scene.workpiece.translation={-2,-3,-8}; moved->volume=std::make_shared<const SparseVoxelVolume>(scene);
            MaterialEvent change; change.kind=MaterialEventKind::Workpiece; change.workpiece=moved; worker.enqueue(change);
            MaterialEvent tool; tool.kind=MaterialEventKind::Tool; tool.tool={ToolKind::FlatEndMill,1,3}; tool.to={4,4,-6}; worker.enqueue(tool);
            MaterialEvent cut; cut.from=tool.to; cut.to={12,8,-4}; worker.enqueue(cut);
            auto final=capture(worker); worker.finish(); done=true; reader.join();
            const auto volume=final.get(); const auto meshes=worker.meshes();
            CHECK(valid && initial->removed_voxels()>0 && raw->volume->removed_voxels()==0);
            CHECK(meshes && meshes->generation==14 && meshes->material_version==volume->version());
            MaterialRemoval reference(*moved->volume);
            reference.sweep(tool.to,tool.to,tool.tool); reference.sweep(cut.from,cut.to,tool.tool);
            CHECK(volume->version()==reference.volume().version());
            const auto dims=volume->dimensions();
            for(std::int64_t z=0;z<dims.z;++z) for(std::int64_t y=0;y<dims.y;++y) for(std::int64_t x=0;x<dims.x;++x)
                CHECK(volume->sample({x,y,z})==reference.volume().sample({x,y,z}));
            const auto last=volume->last_chunk();
            SurfaceMesher mesher;
            for(std::int64_t z=0;z<=last.z;++z) for(std::int64_t y=0;y<=last.y;++y) for(std::int64_t x=0;x<=last.x;++x)
                CHECK(equal_mesh(*meshes->chunks.at({x,y,z}),mesher.build(*volume,{x,y,z})));
            const auto status=worker.status();
            CHECK(status.error.empty() && status.queue_depth==0 && status.mesh_queue_depth==0 && status.dirty_chunks==0);
            CHECK(status.mesh_workers==workers);
            std::cout << "workers=" << workers << " stale=" << status.stale_mesh_jobs << " peak=" << status.mesh_parallel_max << '\n';
        }
        // Snapshot independence while original volume mutates and multiple jobs read.
        SceneConfig scene; scene.stock_size_mm={8,8,8}; scene.volume.voxel_size_mm=.25;
        SparseVoxelVolume volume(scene); auto snapshot=std::make_shared<const SparseVoxelVolume>(volume);
        MeshWorkers pool(4); auto batch=std::make_shared<MeshBatch>(); batch->volume=snapshot;
        batch->chunks.assign(32,{0,0,0}); batch->results.resize(32); batch->remaining=32;
        pool.submit(batch);
        for(int i=0;i<32;++i) volume.erase({i,0,0});
        while(batch->remaining.load(std::memory_order_acquire)) std::this_thread::yield();
        auto expected=SurfaceMesher{}.build(*snapshot,{0,0,0});
        for(const auto& result : batch->results) CHECK(!result.error && equal_mesh(*result.mesh,expected));
        CHECK(snapshot->version()==0 && volume.version()==32);
        std::cout << "PASS: material concurrency, generations, snapshots, complete current meshes\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
