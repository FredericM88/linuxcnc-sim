#include "material/MaterialRemoval.hpp"
#include "material/MaterialWorker.hpp"
#include "console/MaterialCommands.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>
#include <thread>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " #x); } while (false)
using namespace cnc;
SparseVoxelVolume stock(glm::dvec3 size = {8,8,8}, double h = .25, glm::dvec3 translation = {0,0,0}) {
    SceneConfig c; c.stock_size_mm = size; c.volume.voxel_size_mm = h; c.workpiece.translation = translation;
    return SparseVoxelVolume(c);
}
std::uint64_t count(const SparseVoxelVolume& v) {
    std::uint64_t result = 0; const auto d = v.dimensions();
    for (std::int64_t z=0; z<d.z; ++z) for (std::int64_t y=0; y<d.y; ++y) for (std::int64_t x=0; x<d.x; ++x)
        result += v.sample({x,y,z}) == 255;
    return result;
}
void equal(const SparseVoxelVolume& a, const SparseVoxelVolume& b) {
    CHECK(a.dimensions() == b.dimensions()); const auto d = a.dimensions();
    for (std::int64_t z=0; z<d.z; ++z) for (std::int64_t y=0; y<d.y; ++y) for (std::int64_t x=0; x<d.x; ++x)
        CHECK(a.sample({x,y,z}) == b.sample({x,y,z}));
}
void geometry() {
    auto raw = stock(); MaterialRemoval cut(raw); ToolDefinition tool{ToolKind::FlatEndMill,2,3};
    const auto before = count(raw);
    cut.sweep({4,4,9},{4,4,2},tool);
    CHECK(cut.volume().chunk_state({0,0,0}) == ChunkState::Mixed);
    CHECK(cut.volume().stored_voxel_bytes() == 32768);
    CHECK(cut.stats().voxels_removed == before-count(cut.volume()));
    CHECK(cut.stats().voxels_removed > 0 && cut.volume().version() == cut.stats().voxels_removed);
    CHECK(cut.volume().chunk_version({0,0,0}) == cut.volume().version());
    CHECK(cut.volume().sample({16,16,7}) == 255 && cut.volume().sample({16,16,8}) == 0);
    CHECK(cut.volume().sample({0,0,20}) == 255 && cut.dirty().size() == 1);
    const auto removed = cut.stats().voxels_removed, version = cut.volume().version();
    cut.dirty().take(); cut.sweep({4,4,9},{4,4,2},tool);
    CHECK(cut.volume().version() == version && cut.stats().voxels_removed == removed && cut.dirty().size() == 0);
    cut.sweep({-100,-100,-100},{-80,-80,-80},tool);
    CHECK(cut.volume().version() == version && cut.dirty().size() == 0);
    MaterialRemoval stamp(raw); stamp.sweep({4,4,2},{4,4,2},tool);
    CHECK(stamp.volume().sample({16,16,8}) == 0 && stamp.volume().sample({16,16,20}) == 255);
    stamp.sweep({4,4,-1},{4,4,-1},{ToolKind::FlatEndMill,30,20});
    CHECK(count(stamp.volume()) == 0 && stamp.volume().chunk_state({0,0,0}) == ChunkState::Empty);
    CHECK(stamp.volume().stored_voxel_bytes() == 0);
    // Partial chunk initialization preserves outside-stock emptiness.
    MaterialRemoval partial(stock({1.1,1.1,1.1}));
    partial.sweep({.125,.125,0},{.125,.125,0},{ToolKind::FlatEndMill,.1,1});
    CHECK(partial.volume().sample({0,0,0}) == 0 && partial.volume().sample({5,0,0}) == 0);
    CHECK(count(partial.volume()) == 121);
    // Tiny motion and closed radial/axial boundaries, including tangency.
    CHECK(swept_tool_contains(tool,{0,0,0},{1e-12,0,0},{1,0,3}));
    CHECK(!swept_tool_contains(tool,{0,0,0},{0,0,0},{1.001,0,3}));
    // XYZ: XY closest point alone is insufficient; it must also lie in the Z interval.
    CHECK(!swept_tool_contains({ToolKind::FlatEndMill,2,1},{0,0,0},{10,0,10},{1,0,9}));
    CHECK(swept_tool_contains({ToolKind::FlatEndMill,2,1},{0,0,0},{10,0,10},{8,0,9}));
    CHECK(swept_tool_contains(tool,{-1e9,4,2},{1e9,4,2},{4,4,2}));
}
void spacing() {
    auto raw = stock({16,12,8},.25,{-2,3,-8});
    ToolDefinition tool{ToolKind::FlatEndMill,2,3};
    for (const auto delta : {glm::dvec3(14,0,0),glm::dvec3(0,0,-12),glm::dvec3(14,8,-6),glm::dvec3(0),glm::dvec3(1e-10,0,0)}) {
        const glm::dvec3 a(-1,5,-1), b = a+delta;
        MaterialRemoval coarse(raw), fine(raw), reverse(raw);
        coarse.sweep(a,b,tool); reverse.sweep(b,a,tool);
        auto previous = a;
        for (int i=1; i<=127; ++i) {
            auto next = a+delta*(static_cast<double>(i)/127); fine.sweep(previous,next,tool); previous=next;
        }
        equal(coarse.volume(),fine.volume()); equal(coarse.volume(),reverse.volume());
    }
    // Test broad phase against exhaustive analytic inclusion, including legacy rotation.
    SceneConfig scene = raw.config();
    scene.workpiece.orientation = glm::angleAxis(.73,glm::normalize(glm::dvec3(1,2,3)));
    MaterialRemoval transformed{SparseVoxelVolume(scene)};
    const glm::dvec3 a(-3,5,-5), b(10,12,2);
    transformed.sweep(a,b,tool);
    const auto& v = transformed.volume(); const auto d = v.dimensions();
    for (std::int64_t z=0; z<d.z; ++z) for (std::int64_t y=0; y<d.y; ++y) for (std::int64_t x=0; x<d.x; ++x) {
        const auto p = v.config().workpiece.to_machine(v.voxel_to_local({x,y,z}) + glm::dvec3(.125));
        CHECK((v.sample({x,y,z})==0) == swept_tool_contains(tool,a,b,p));
    }
    // Random diagonal paths, compared as one sweep versus 19 collinear pieces.
    std::mt19937 random(510); std::uniform_real_distribution<double> coordinate(-3,12);
    for (int trial=0; trial<12; ++trial) {
        glm::dvec3 from(coordinate(random),coordinate(random),coordinate(random));
        glm::dvec3 to(coordinate(random),coordinate(random),coordinate(random));
        MaterialRemoval one(stock()), many(stock()); one.sweep(from,to,tool);
        for(int i=0; i<19; ++i) many.sweep(glm::mix(from,to,double(i)/19),glm::mix(from,to,double(i+1)/19),tool);
        equal(one.volume(),many.volume());
    }
}
double area(const ChunkMesh& m) {
    double sum = 0;
    for (std::size_t i=0; i<m.indices.size(); i+=3) {
        auto a=glm::dvec3(m.vertices[m.indices[i]].position), b=glm::dvec3(m.vertices[m.indices[i+1]].position),
             c=glm::dvec3(m.vertices[m.indices[i+2]].position);
        sum += glm::length(glm::cross(b-a,c-a))*.5;
    }
    return sum;
}
void boundary() {
    MaterialRemoval cut(stock({16,8,8})); SurfaceMesher mesher;
    const auto neighbour = mesher.build(cut.volume(),{1,0,0});
    // Remove only x-index 31: the unchanged solid neighbour must expose its -X face.
    cut.sweep({7.875,4,2},{7.875,4,2},{ToolKind::FlatEndMill,.3,2});
    CHECK(cut.volume().chunk_version({1,0,0}) == 0);
    const auto dirty = cut.dirty().take();
    CHECK(dirty.size()==2 && std::find(dirty.begin(),dirty.end(),ChunkCoord{1,0,0}) != dirty.end());
    CHECK(area(mesher.build(cut.volume(),{1,0,0})) > area(neighbour));
    cut.sweep({2,4,2},{14,4,2},{ToolKind::FlatEndMill,2,8});
    for (std::int64_t x=8; x<56; ++x) CHECK(cut.volume().sample({x,16,16})==0);
    double total_area = area(mesher.build(cut.volume(),{0,0,0}))+area(mesher.build(cut.volume(),{1,0,0}));
    std::uint64_t faces=0; const auto d=cut.volume().dimensions();
    for (std::int64_t z=0; z<d.z; ++z) for (std::int64_t y=0; y<d.y; ++y) for (std::int64_t x=0; x<d.x; ++x) {
        const VoxelCoord p{x,y,z}; if (!cut.volume().sample(p)) continue;
        for (int a=0;a<3;++a) for (int sign : {-1,1}) { auto q=p; q[a]+=sign; faces+=cut.volume().sample(q)==0; }
    }
    CHECK(std::abs(total_area-double(faces)*.25*.25)<1e-8);
    // The same face-neighbour rule also covers Y and Z chunk boundaries.
    for (int axis : {1,2}) {
        auto size=glm::dvec3(8); size[axis]=16; MaterialRemoval other(stock(size));
        glm::dvec3 tip(4.125); tip[axis]=7.875;
        other.sweep(tip,tip,{ToolKind::FlatEndMill,.3,.1});
        auto next=ChunkCoord{}; next[axis]=1; const auto list=other.dirty().take();
        CHECK(std::find(list.begin(),list.end(),next)!=list.end());
    }
}
void quantitative() {
    // Exact centre grid semantics for a 6 mm groove, 20 mm between centres, depth 2 mm.
    auto raw=stock({30,20,10},.1,{0,0,-20}); MaterialRemoval cut(raw);
    const auto before=count(raw); cut.sweep({5,10,-12},{25,10,-12},{ToolKind::FlatEndMill,6,20});
    const auto removed=cut.stats().voxels_removed;
    CHECK(before-count(cut.volume())==removed);
    const auto actual=double(removed)*.001, expected=(20*6+glm::pi<double>()*9)*2;
    // Boundary strip bound from voxel circumradius rho, with exact grid-aligned depth.
    const double rho=std::sqrt(2.)*.1/2, perimeter=40+6*glm::pi<double>();
    const double tolerance=2*(perimeter*rho+glm::pi<double>()*rho*rho);
    CHECK(std::abs(actual-expected)<=tolerance);
    for (std::int64_t x=50;x<250;++x) {
        CHECK(cut.volume().sample({x,70,80})==0 && cut.volume().sample({x,129,99})==0);
        CHECK(cut.volume().sample({x,69,80})==255 && cut.volume().sample({x,130,99})==255);
        CHECK(cut.volume().sample({x,100,79})==255);
    }
    std::cout << "voxels before=" << before << " after=" << count(cut.volume()) << " removed=" << removed
              << " volume=" << actual << " analytic=" << expected << " tolerance=" << tolerance << " mm3\n";
}
void queue_test() {
    MotionQueue<std::uint64_t> queue;
    // Deliberately accumulate backlog before starting consumption.
    constexpr std::uint64_t total=200000;
    for(std::uint64_t i=0;i<10000;++i) queue.push(i);
    CHECK(queue.depth()==10000 && queue.maximum()==10000);
    std::atomic<bool> valid{true};
    std::jthread consumer([&] {
        for(std::uint64_t i=0;i<total;++i) {
            std::uint64_t value;
            while(!queue.pop(value)) std::this_thread::yield();
            if(value!=i) valid=false;
        }
    });
    for(std::uint64_t i=10000;i<total;++i) queue.push(i);
    consumer.join(); CHECK(valid && queue.depth()==0 && queue.maximum()>=10000);
    // Worker queue preserves a bent path; no endpoint-only shortcut across its corner.
    auto raw=std::make_shared<WorkpieceSnapshot>(); raw->volume=std::make_shared<const SparseVoxelVolume>(stock());
    MaterialWorker worker(raw); MaterialRemoval reference(*raw->volume);
    MaterialEvent on; on.kind=MaterialEventKind::Enable; on.to={2,2,2}; worker.enqueue(on);
    reference.sweep(on.to,on.to,on.tool);
    glm::dvec3 previous=on.to;
    for(int i=0;i<2000;++i) {
        MaterialEvent e; e.from=previous; e.to=i%2 ? glm::dvec3(6,6,2):glm::dvec3(2,6,2);
        worker.enqueue(e); previous=e.to;
    }
    auto promise=std::make_shared<std::promise<std::shared_ptr<const SparseVoxelVolume>>>();
    auto result=promise->get_future(); MaterialEvent capture; capture.kind=MaterialEventKind::Capture; capture.capture=promise;
    worker.enqueue(capture);
    MaterialEvent reset; reset.kind=MaterialEventKind::Reset; worker.enqueue(reset);
    MaterialEvent change; change.kind=MaterialEventKind::Tool; change.tool={ToolKind::FlatEndMill,.5,.5};
    change.to={4,4,4}; worker.enqueue(change); // Tool change stamps the new geometry while ON.
    MaterialEvent off; off.kind=MaterialEventKind::Disable; worker.enqueue(off);
    MaterialEvent ignored; ignored.from={4,4,4}; ignored.to={0,0,0}; worker.enqueue(ignored);
    auto new_promise=std::make_shared<std::promise<std::shared_ptr<const SparseVoxelVolume>>>();
    auto new_result=new_promise->get_future(); capture.capture=new_promise; worker.enqueue(capture);
    worker.finish();
    previous=on.to;
    for(int i=0;i<2000;++i) {
        const auto next=i%2 ? glm::dvec3(6,6,2):glm::dvec3(2,6,2);
        reference.sweep(previous,next,on.tool); previous=next;
    }
    equal(*result.get(),reference.volume());
    MaterialRemoval fresh(*raw->volume); fresh.sweep(change.to,change.to,change.tool);
    equal(*new_result.get(),fresh.volume());
    auto s=worker.status(); CHECK(s.error.empty() && s.queue_depth==0 && s.removal.sweeps==1 && s.dirty_chunks==0);
    CHECK(s.events_processed==2007 && s.mesh_rebuilds>0);
    const auto meshes=worker.meshes(); CHECK(meshes && meshes->chunks.size()==1);
    CHECK(std::abs(area(*meshes->chunks.at({0,0,0}))-area(SurfaceMesher{}.build(fresh.volume(),{0,0,0})))<1e-9);
    std::cout << "worker max backlog=" << s.max_queue_depth << " events=" << s.events_processed << '\n';
}
void failure() {
    auto raw=std::make_shared<WorkpieceSnapshot>(); raw->volume=std::make_shared<const SparseVoxelVolume>(stock());
    MaterialWorker worker(raw);
    MaterialEvent invalid; invalid.kind=MaterialEventKind::Enable; invalid.to={2e12,0,0}; worker.enqueue(invalid);
    auto promise=std::make_shared<std::promise<std::shared_ptr<const SparseVoxelVolume>>>();
    auto future=promise->get_future(); MaterialEvent capture; capture.kind=MaterialEventKind::Capture; capture.capture=promise;
    worker.enqueue(capture);
    CHECK(future.wait_for(std::chrono::seconds(5))==std::future_status::ready);
    bool rejected=false; try { future.get(); } catch(const std::exception&) { rejected=true; }
    CHECK(rejected && worker.failed()); worker.finish();
    CHECK(worker.status().error.find("simulation incomplete")!=std::string::npos);
}
void runtime() {
    auto scene=stock({8,8,8},.25,{-4,-4,-4}).config();
    Simulation simulation("127.0.0.1",0,{100,100,100,100},{},scene);
    auto raw=simulation.workpiece_snapshot();
    CHECK(simulation.material_snapshot()->removed_voxels()==0);
    execute_material_command(simulation,"tool flat-end 2 3");
    execute_material_command(simulation,"material on");
    auto first=simulation.material_snapshot(); CHECK(first->removed_voxels()>0);
    int fd=socket(AF_INET,SOCK_DGRAM,0); CHECK(fd>=0);
    timeval timeout{2,0}; CHECK(setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout))==0);
    sockaddr_in target{}; target.sin_family=AF_INET; target.sin_port=htons(simulation.port()); target.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    auto send=[&](int id,int x,int y,int z) {
        Request request{}; request.pio_timing=235; request.packet_id=static_cast<std::uint8_t>(id);
        int deltas[]={x,y,z};
        for(int i=0;i<3;++i) if(deltas[i]) request.stepgen_command[i]=(deltas[i]>0?0x80000000u:0u)|(19494u<<10)|(static_cast<unsigned>(std::abs(deltas[i]))-1);
        request.checksum=calculate_checksum(&request,sizeof(request)-1);
        CHECK(sendto(fd,&request,sizeof(request),0,reinterpret_cast<sockaddr*>(&target),sizeof(target))==sizeof(request));
        Response response{}; CHECK(recv(fd,&response,sizeof(response),0)==sizeof(response)); CHECK(tx_checksum_ok(&response));
    };
    send(0,300,0,-200); send(1,0,300,0);
    auto after=simulation.material_snapshot(); CHECK(after->removed_voxels()>first->removed_voxels());
    MaterialRemoval reference(*raw->volume); ToolDefinition tool{ToolKind::FlatEndMill,2,3};
    reference.sweep({0,0,0},{0,0,0},tool); reference.sweep({0,0,0},{3,0,-2},tool); reference.sweep({3,0,-2},{3,3,-2},tool);
    equal(*after,reference.volume());
    CHECK(first->removed_voxels()<after->removed_voxels() && raw->volume->removed_voxels()==0);
    execute_material_command(simulation,"material off"); send(2,-600,0,0);
    equal(*simulation.material_snapshot(),*after);
    execute_material_command(simulation,"material on");
    auto enabled=simulation.material_snapshot(); CHECK(enabled->removed_voxels()>after->removed_voxels());
    // Enabling stamps only current pose, never joins the disabled travel gap.
    reference.sweep({-3,3,-2},{-3,3,-2},tool); equal(*enabled,reference.volume());
    execute_material_command(simulation,"material reset");
    auto reset=simulation.material_snapshot(); equal(*reset,*raw->volume);
    CHECK(simulation.workpiece_snapshot()==raw);
    CHECK(simulation.machine_snapshot().steps==(StepPositions{-300,300,-200,0}));
    execute_material_command(simulation,"material off");
    auto config=raw->config; config.position_machine_mm={20,20,20}; simulation.configure_workpiece(config);
    equal(*simulation.material_snapshot(),*simulation.volume());
    for(const auto* text : {"material on extra","material nonsense","tool flat-end 0 3","tool flat-end nan 3", "tool flat-end 2 inf", "tool flat-end 2 3mm", "tool ball-end 2 3"}) {
        bool rejected=false; try { execute_material_command(simulation,text); } catch(const std::exception&) { rejected=true; } CHECK(rejected);
    }
    close(fd); simulation.stop();
    const auto state=simulation.material_status(); CHECK(state.error.empty() && state.queue_depth==0 && state.dirty_chunks==0);
    CHECK(simulation.status().packets.accepted_packets==3 && simulation.status().packets.packet_id_gaps==0);
}
int main(int argc,char** argv) {
    try {
        CHECK(argc==2); std::string mode=argv[1];
        if(mode=="geometry") geometry(); else if(mode=="spacing") spacing(); else if(mode=="boundary") boundary();
        else if(mode=="quantitative") quantitative(); else if(mode=="queue") queue_test(); else if(mode=="runtime") runtime(); else if(mode=="failure") failure();
        else throw std::runtime_error("unknown material test");
        std::cout << "PASS: material " << mode << '\n';
    } catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
