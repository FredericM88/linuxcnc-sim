#include "VoxelReference.hpp"
#include "material/MaterialWorker.hpp"
#include "material/MotionCoalescer.hpp"
#include "console/MaterialCommands.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>
#include <unordered_set>

using namespace cnc;
using cnc::test::VoxelReference;
namespace {
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+": " #x); } while(false)
using Clock=std::chrono::steady_clock;
const ToolDefinition tool{ToolKind::FlatEndMill,6,20};
const glm::dvec3 above{15,4,-5}, plunged{15,4,-12};
struct TimedSegment { SweepSegment sweep; unsigned tick; };
std::shared_ptr<const WorkpieceSnapshot> stock(bool small=false) {
    auto raw=std::make_shared<WorkpieceSnapshot>();
    raw->config.size_mm=small ? glm::dvec3(8,8,4) : glm::dvec3(30,20,10);
    raw->config.position_machine_mm={0,0,-20}; raw->config.origin_offset_mm={0,0,0};
    raw->config.voxel_size_mm=small ? .25 : .1;
    raw->volume=build_workpiece(raw->config); raw->revision=1;
    return raw;
}
std::vector<TimedSegment> path() {
    std::ifstream in(std::string(CURVED_FIXTURES)+"/arc.steps"); std::string line;
    CHECK(std::getline(in,line) && line=="CNC_CURVED_STEPS_V1"); CHECK(std::getline(in,line));
    std::vector<TimedSegment> result{{{above,plunged},0}};
    glm::dvec3 previous=plunged;
    while (std::getline(in,line)) {
        unsigned tick; std::int64_t x,y,z; std::istringstream row(line); CHECK(row>>tick>>x>>y>>z);
        const glm::dvec3 next{static_cast<double>(x)/400,static_cast<double>(y)/400,static_cast<double>(z)/400};
        if (tick) result.push_back({{previous,next},tick});
        previous=next;
    }
    CHECK(result.size()==3143); return result;
}
VoxelReference reference(bool arc=true) {
    return VoxelReference::read(std::string(CURVED_FIXTURES)+(arc ? "/plunge-arc.rle" : "/plunge.rle"));
}
void same_metadata(const SparseVoxelVolume& a,const SparseVoxelVolume& b) {
    CHECK(a.version()==b.version() && a.removed_voxels()==b.removed_voxels());
    const auto last=a.last_chunk();
    for (std::int64_t z=0; z<=last.z; ++z) for (std::int64_t y=0; y<=last.y; ++y) for (std::int64_t x=0; x<=last.x; ++x)
        CHECK(a.chunk_version({x,y,z})==b.chunk_version({x,y,z}));
}
std::future<std::shared_ptr<const SparseVoxelVolume>> capture(MaterialWorker& worker) {
    MaterialEvent e; e.kind=MaterialEventKind::Capture;
    e.capture=std::make_shared<std::promise<std::shared_ptr<const SparseVoxelVolume>>>();
    auto future=e.capture->get_future(); worker.enqueue(e); return future;
}
void ready(std::future<std::shared_ptr<const SparseVoxelVolume>>& future) {
    // Completion guard only: instrumented 1-ms runs can exceed 30 seconds.
    // The independent CTest 180-second timeout still catches deadlocks.
    CHECK(future.wait_for(std::chrono::seconds(120))==std::future_status::ready);
}
void enqueue(MaterialWorker& worker, MaterialEventKind kind, glm::dvec3 to=above) {
    MaterialEvent e; e.kind=kind; e.to=to; worker.enqueue(e);
}
void motion(MaterialWorker& worker, const SweepSegment& s, Clock::time_point time) {
    MaterialEvent e; e.from=s.from; e.to=s.to; e.time=time; worker.enqueue(e);
}
void windows(unsigned interval) {
    const auto raw=stock(); const auto input=path(); const auto expected=reference();
    // Future timestamps isolate source-time partitioning from thread scheduling.
    // Capture is the explicit flush, including the final short window.
    const auto epoch=Clock::now()+std::chrono::hours(1);
    MaterialWorker worker(raw,{1,4,interval}); enqueue(worker,MaterialEventKind::Enable);
    for (const auto& s:input) motion(worker,s.sweep,epoch+std::chrono::milliseconds(s.tick));
    auto final=capture(worker); ready(final); worker.finish(); const auto volume=final.get();
    expected.require_equal(*volume);
    CHECK(volume->removed_voxels()==166400 && VoxelReference::capture(*volume).fingerprint()=="585ccf7e9d0561a2");
    const auto status=worker.status(); CHECK(status.error.empty());
    CHECK(status.removal.batch.batches==(input.back().tick/interval)+1);
    CHECK(status.removal.batch.segments==status.motion_coalesced);
    CHECK(status.motion_received==3143 && status.removal.batch.max_segments<=interval);
    CHECK(status.removal.containment_tests<=status.removal.voxels_tested*interval);
    const auto meshes=worker.meshes(); CHECK(meshes && meshes->material_version==volume->version());
    // Low-level batching retains EVERY input segment, independently of coalescing.
    MaterialRemoval direct(*raw->volume); std::vector<SweepSegment> batch; unsigned start=0;
    for (const auto& s:input) {
        if (s.tick-start>=interval) { direct.sweep_batch(batch,tool); batch.clear(); start=s.tick; }
        batch.push_back(s.sweep);
    }
    direct.sweep_batch(batch,tool); expected.require_equal(direct.volume()); same_metadata(*volume,direct.volume());
    // Separate plunge, short movement, same frozen 6-million-cell oracle.
    MaterialRemoval plunge(*raw->volume); const SweepSegment vertical{above,plunged}; plunge.sweep_batch({&vertical,1},tool);
    reference(false).require_equal(plunge.volume()); CHECK(plunge.stats().voxels_removed==56560);
    CHECK(VoxelReference::capture(plunge.volume()).fingerprint()=="95c98093e6e51d52");
    std::cout<<"interval="<<interval<<" batches="<<status.removal.batch.batches<<" segments="<<status.removal.batch.segments
             <<" max="<<status.removal.batch.max_segments<<" removed=166400 fingerprint=585ccf7e9d0561a2 exact_cells=6000000\n";
}
void subdivision() {
    const auto raw=stock(); auto input=path(); std::vector<SweepSegment> split;
    for (std::size_t i=1; i<input.size(); ++i) {
        const auto& s=input[i].sweep; const auto middle=(s.from+s.to)*.5;
        if (middle!=s.from && middle!=s.to && exact_straight_extension(s.from,middle,s.to)) {
            split.push_back({s.from,middle}); split.push_back({middle,s.to});
        } else split.push_back(s);
    }
    CHECK(split.size()==4534);
    for (int repeat=0; repeat<2; ++repeat) {
        MaterialRemoval cut(*raw->volume); cut.sweep_batch({&input.front().sweep,1},tool);
        for (std::size_t i=0; i<split.size(); i+=20) cut.sweep_batch(std::span(split).subspan(i,std::min(std::size_t(20),split.size()-i)),tool);
        reference().require_equal(cut.volume()); CHECK(cut.stats().voxels_removed==166400);
    }
    MaterialRemoval chord(*raw->volume); chord.sweep(above,plunged,tool); chord.sweep(plunged,{5,4,-12},tool);
    CHECK(reference().differences(chord.volume())==78560);
    MaterialRemoval straight(*raw->volume); const SweepSegment groove{{5,10,-12},{25,10,-12}};
    straight.sweep_batch({&groove,1},tool); CHECK(straight.stats().voxels_removed==296560);
}
void compare(const SparseVoxelVolume& raw,const std::vector<SweepSegment>& input,const ToolDefinition& cutter) {
    MaterialRemoval serial(raw), batch(raw);
    for (const auto& s:input) serial.sweep(s.from,s.to,cutter);
    batch.sweep_batch(input,cutter); VoxelReference::capture(serial.volume()).require_equal(batch.volume());
    same_metadata(serial.volume(),batch.volume());
    const auto a=serial.dirty().take(), b=batch.dirty().take();
    const std::unordered_set<ChunkCoord,ChunkCoordHash> da(a.begin(),a.end()),db(b.begin(),b.end()); CHECK(da==db);
    // Repeating a union is idempotent and creates no new versions/dirty work.
    const auto version=batch.volume().version(); batch.sweep_batch(input,cutter);
    CHECK(batch.volume().version()==version && batch.dirty().size()==0);
}
void geometry() {
    SceneConfig scene; scene.stock_size_mm={9,9,5}; scene.volume={.25,8}; scene.workpiece.translation={0,0,0};
    const ToolDefinition cutter{ToolKind::FlatEndMill,1,2};
    const std::vector<SweepSegment> corner{{{1,1,1},{7,1,1}},{{7,1,1},{7,7,1}}};
    compare(SparseVoxelVolume(scene),corner,cutter);
    MaterialRemoval bend{SparseVoxelVolume(scene)},chord{SparseVoxelVolume(scene)};
    bend.sweep_batch(corner,cutter); chord.sweep(corner.front().from,corner.back().to,cutter);
    CHECK(VoxelReference::capture(bend.volume()).differences(chord.volume())>0);
    compare(SparseVoxelVolume(scene),{{{1,1,1},{7,1,1}},{{7,1,1},{1,1,1}}},cutter);
    std::vector<SweepSegment> curve; glm::dvec3 previous{5,4,1};
    for(int i=1;i<=100;++i) { glm::dvec3 next{4+std::cos(i*.031),4+std::sin(i*.031),1}; curve.push_back({previous,next}); previous=next; }
    compare(SparseVoxelVolume(scene),curve,cutter);
    MaterialRemoval tight{SparseVoxelVolume(scene)}, shortcut{SparseVoxelVolume(scene)};
    tight.sweep_batch(curve,cutter); shortcut.sweep(curve.front().from,curve.back().to,cutter);
    CHECK(VoxelReference::capture(tight.volume()).differences(shortcut.volume())>0);
    // Exhaustive analytic oracle for rotated volumes, diagonal XYZ, tangent
    // boundaries, distant coordinates and partial chunks; not merely two AABBs.
    std::mt19937 rng(512); std::uniform_real_distribution<double> coordinate(-2,11);
    for(int trial=0;trial<12;++trial) {
        scene.workpiece.orientation=glm::angleAxis(.13*trial,glm::normalize(glm::dvec3(1,2,3)));
        scene.workpiece.translation=trial<10 ? glm::dvec3(0) : glm::dvec3(1e11,-1e11,1e11);
        std::vector<SweepSegment> segments;
        for(int j=0;j<9;++j) segments.push_back({scene.workpiece.translation+glm::dvec3(coordinate(rng),coordinate(rng),coordinate(rng)),
                                               scene.workpiece.translation+glm::dvec3(coordinate(rng),coordinate(rng),coordinate(rng))});
        SparseVoxelVolume raw(scene); compare(raw,segments,cutter); MaterialRemoval cut(raw); cut.sweep_batch(segments,cutter);
        const auto d=raw.dimensions();
        for(std::int64_t z=0;z<d.z;++z) for(std::int64_t y=0;y<d.y;++y) for(std::int64_t x=0;x<d.x;++x) {
            const auto centre=scene.workpiece.to_machine(raw.voxel_to_local({x,y,z})+glm::dvec3(.125));
            bool hit=false; for(const auto& s:segments) hit |= swept_tool_contains(cutter,s.from,s.to,centre);
            CHECK((cut.volume().sample({x,y,z})==0)==hit);
        }
    }
    scene.workpiece.orientation={1,0,0,0}; scene.workpiece.translation={0,0,0};
    compare(SparseVoxelVolume(scene),{{{.625,.125,.125},{.625,.125,.125}},{{-1e9,4,2},{1e9,4,2}}},cutter);
    // Exact halo-only invalidation at each chunk face.
    for(int axis=0;axis<3;++axis) {
        MaterialRemoval cut{SparseVoxelVolume(scene)}; glm::dvec3 tip{.875,.875,.875}; tip[axis]=1.875;
        const SweepSegment stamp{tip,tip}; cut.sweep_batch({&stamp,1},{ToolKind::FlatEndMill,.1,.1});
        auto neighbour=ChunkCoord{}; neighbour[axis]=1; const auto dirty=cut.dirty().take();
        CHECK(cut.volume().chunk_version(neighbour)==0 && std::find(dirty.begin(),dirty.end(),neighbour)!=dirty.end());
    }
}
void controls() {
    const auto raw=stock(true); MaterialWorker worker(raw,{1,2,20}); MaterialRemoval serial(*raw->volume);
    const auto epoch=Clock::now()+std::chrono::hours(1);
    const glm::dvec3 a{1,1,-18},b{6,1,-18},c{6,6,-18},d{1,6,-18};
    ToolDefinition cutter{ToolKind::FlatEndMill,1,2}; MaterialEvent change; change.kind=MaterialEventKind::Tool; change.tool=cutter; worker.enqueue(change);
    enqueue(worker,MaterialEventKind::Enable,a); serial.sweep(a,a,cutter);
    auto check=[&] { auto f=capture(worker); ready(f); const auto v=f.get(); VoxelReference::capture(serial.volume()).require_equal(*v); same_metadata(serial.volume(),*v); };
    motion(worker,{a,b},epoch); serial.sweep(a,b,cutter);
    enqueue(worker,MaterialEventKind::Disable,b); motion(worker,{b,c},epoch); check(); // OFF flush + ignored travel
    enqueue(worker,MaterialEventKind::Enable,c); serial.sweep(c,c,cutter); check();
    motion(worker,{c,d},epoch); serial.sweep(c,d,cutter);
    cutter.diameter_mm=.5; change.tool=cutter; change.to=d; worker.enqueue(change); serial.sweep(d,d,cutter);
    motion(worker,{d,a},epoch); serial.sweep(d,a,cutter); check(); // New tool cannot cut earlier segment.
    motion(worker,{a,c},epoch); serial.sweep(a,c,cutter); check(); // Capture closes a short window.
    motion(worker,{c,b},epoch+std::chrono::milliseconds(20));
    enqueue(worker,MaterialEventKind::Reset); serial=MaterialRemoval(*raw->volume); check();
    motion(worker,{a,b},epoch); serial.sweep(a,b,cutter);
    auto replacement=std::make_shared<WorkpieceSnapshot>(*raw); replacement->config.size_mm={9,7,3}; replacement->volume=build_workpiece(replacement->config);
    MaterialEvent workpiece; workpiece.kind=MaterialEventKind::Workpiece; workpiece.workpiece=replacement; worker.enqueue(workpiece);
    serial=MaterialRemoval(*replacement->volume); motion(worker,{a,c},epoch); serial.sweep(a,c,cutter); check();
    worker.finish(); const auto status=worker.status(); CHECK(status.error.empty() && status.generation==3);
    const auto meshes=worker.meshes(); CHECK(meshes && meshes->generation==3 && meshes->material_version==serial.volume().version());
    CHECK(status.queue_depth==0 && status.dirty_chunks==0 && status.lag_ms==0);
}
void boundaries() {
    const auto raw=stock(true); const auto epoch=Clock::now()+std::chrono::hours(1);
    // 0,19 share a window, 20 is EXACTLY the next start, then a backward
    // timestamp is a conservative boundary; a Capture does not wait an hour.
    MaterialWorker worker(raw,{1,1,20}); enqueue(worker,MaterialEventKind::Enable);
    const std::vector<SweepSegment> edges{{{1,1,-18},{4,1,-18}},{{4,1,-18},{4,4,-18}},{{4,4,-18},{1,4,-18}},{{1,4,-18},{1,1,-18}}};
    const int ticks[]{0,19,20,18}; MaterialRemoval serial(*raw->volume); serial.sweep(above,above,tool);
    for(std::size_t i=0;i<edges.size();++i) { motion(worker,edges[i],epoch+std::chrono::milliseconds(ticks[i])); serial.sweep(edges[i].from,edges[i].to,tool); }
    auto f=capture(worker); ready(f); worker.finish(); VoxelReference::capture(serial.volume()).require_equal(*f.get());
    CHECK(worker.status().removal.batch.batches==3 && worker.status().removal.batch.max_segments==2);
    // Repeated event timestamps cannot grow a retained union beyond 256 segments.
    MaterialWorker bounded(raw,{1,1,20}); enqueue(bounded,MaterialEventKind::Enable);
    for(int i=0;i<600;++i) motion(bounded,edges[static_cast<std::size_t>(i)%4],epoch);
    auto end=capture(bounded); ready(end); bounded.finish(); CHECK(bounded.status().removal.batch.batches==3);
    CHECK(bounded.status().removal.batch.max_segments==256); VoxelReference::capture(serial.volume()).require_equal(*end.get());
    // Shutdown drains an open future-dated window even without Capture.
    MaterialWorker shutdown(raw,{1,1,1000}); enqueue(shutdown,MaterialEventKind::Enable);
    motion(shutdown,edges.front(),epoch); shutdown.finish(); CHECK(shutdown.status().removal.batch.batches==1 && shutdown.status().removal.voxels_removed>0);
    MaterialWorker live_stop(raw,{1,1,20}); enqueue(live_stop,MaterialEventKind::Enable);
    motion(live_stop,edges.front(),Clock::now());
    const auto stop_deadline=Clock::now()+std::chrono::seconds(3);
    while(live_stop.status().removal.batch.batches==0 && Clock::now()<stop_deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK(live_stop.status().removal.batch.batches==1); live_stop.finish();
    // A short/stop window commits by source deadline, with no later command or
    // motion to flush it. Old source time proves this is not time since dequeue.
    for(unsigned interval : {1u,20u,1000u}) {
        MaterialWorker stopped(raw,{1,1,interval}); enqueue(stopped,MaterialEventKind::Enable);
        motion(stopped,edges.front(),Clock::now()-std::chrono::milliseconds(interval));
        const auto deadline=Clock::now()+std::chrono::milliseconds(700);
        while(stopped.status().removal.batch.batches==0 && Clock::now()<deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        CHECK(stopped.status().removal.batch.batches==1); auto state=capture(stopped); ready(state);
        MaterialRemoval expected(*raw->volume); expected.sweep(edges.front().from,edges.front().to,tool);
        VoxelReference::capture(expected.volume()).require_equal(*state.get()); stopped.finish();
    }
}
void failure() {
    MaterialWorker worker(stock(true),{1,1,20}); enqueue(worker,MaterialEventKind::Enable);
    motion(worker,{{1,1,-18},{2e12,1,-18}},Clock::now()+std::chrono::hours(1));
    // Keep the producer's promise alive: a lost Capture must not masquerade as
    // a successful failure path merely through destruction/broken_promise.
    MaterialEvent event; event.kind=MaterialEventKind::Capture;
    event.capture=std::make_shared<std::promise<std::shared_ptr<const SparseVoxelVolume>>>();
    auto future=event.capture->get_future(); worker.enqueue(event);
    CHECK(future.wait_for(std::chrono::seconds(5))==std::future_status::ready);
    bool rejected=false;
    try { future.get(); } catch(const std::runtime_error& error) {
        rejected=std::string(error.what()).find("simulation incomplete")!=std::string::npos;
    }
    CHECK(rejected && worker.failed()); worker.finish();
    CHECK(worker.status().error.find("outside supported")!=std::string::npos);
}
void bulk() {
    SceneConfig scene; scene.volume={1,4}; scene.stock_size_mm={5,5,5}; scene.workpiece.translation={0,0,0};
    SparseVoxelVolume serial(scene),batch(scene);
    std::vector<std::uint32_t> all; for(std::uint32_t i=0;i<64;++i) all.push_back(i); all.push_back(0);
    CHECK(batch.erase_chunk_voxels({0,0,0},all)==64); CHECK(batch.erase_chunk_voxels({0,0,0},all)==0);
    CHECK(batch.erase_chunk_voxels({1,1,1},all)==1); CHECK(batch.erase_chunk_voxels({-1,0,0},all)==0);
    for(int z=0;z<4;++z) for(int y=0;y<4;++y) for(int x=0;x<4;++x) serial.erase({x,y,z});
    serial.erase({4,4,4});
    VoxelReference::capture(serial).require_equal(batch); same_metadata(serial,batch);
    CHECK(batch.stored_voxel_bytes()==0);
    bool rejected=false; const std::uint32_t invalid[]{0,64};
    try { batch.erase_chunk_voxels({1,0,0},invalid); } catch(const std::out_of_range&) { rejected=true; }
    CHECK(rejected && batch.version()==65 && batch.sample({4,0,0})==255);
    CHECK(resolve_material_workers({1,1,0}).material_batch_ms==0 && resolve_material_workers({1,1,1000}).material_batch_ms==1000);
    rejected=false; try { resolve_material_workers({1,1,1001}); } catch(const std::invalid_argument&) { rejected=true; } CHECK(rejected);
    MaterialStatus status; CHECK(describe_material(status).find("Material batch interval: 20 ms")!=std::string::npos);
}
}
int main(int argc,char** argv) {
    try {
        CHECK(argc==2); const std::string mode=argv[1];
        if(mode=="subdivision") subdivision(); else if(mode=="geometry") geometry(); else if(mode=="controls") controls();
        else if(mode=="boundaries") boundaries(); else if(mode=="failure") failure(); else if(mode=="bulk") bulk(); else windows(static_cast<unsigned>(std::stoul(mode)));
        std::cout<<"PASS: material batch "<<mode<<'\n';
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
