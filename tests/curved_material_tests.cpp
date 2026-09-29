#include "VoxelReference.hpp"
#include "material/MaterialWorker.hpp"
#include "material/MotionCoalescer.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <numbers>

using namespace cnc;
using cnc::test::VoxelReference;
namespace {
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+": " #x); } while(false)
using Clock=std::chrono::steady_clock;
constexpr double feed=300.0/60, radius=5, cycle_seconds=.001;
const Scales scales{400,400,400,400};
const ToolDefinition tool{ToolKind::FlatEndMill,6,20};
const glm::dvec3 above{15,4,-5}, plunged{15,4,-12};
const std::filesystem::path fixtures{CURVED_FIXTURES};
struct Sample { unsigned tick{}; StepPositions steps{}; };
struct Segment { glm::dvec3 from, to; unsigned tick{}; };
struct Path { std::vector<Sample> samples; std::vector<Segment> segments; };
double ms(Clock::duration duration) { return std::chrono::duration<double,std::milli>(duration).count(); }
glm::dvec3 pose(const Sample& sample) { return tool_pose({sample.steps,scales,0}).tip_mm; }
std::shared_ptr<const WorkpieceSnapshot> stock() {
    auto raw=std::make_shared<WorkpieceSnapshot>();
    raw->config.size_mm={30,20,10}; raw->config.position_machine_mm={15,10,-10};
    raw->config.origin_offset_mm={15,10,10}; raw->config.voxel_size_mm=.1;
    raw->volume=build_workpiece(raw->config); raw->revision=1;
    return raw;
}
Path load_path() {
    std::ifstream in(fixtures/"arc.steps"); std::string line;
    CHECK(std::getline(in,line) && line=="CNC_CURVED_STEPS_V1");
    CHECK(std::getline(in,line) && line.starts_with("# tick"));
    Path path;
    while (std::getline(in,line)) {
        std::istringstream row(line); Sample sample; std::string extra;
        CHECK(row>>sample.tick>>sample.steps[0]>>sample.steps[1]>>sample.steps[2]); CHECK(!(row>>extra));
        CHECK(sample.tick==path.samples.size());
        if (!path.samples.empty() && sample.steps!=path.samples.back().steps)
            path.segments.push_back({pose(path.samples.back()),pose(sample),sample.tick});
        path.samples.push_back(sample);
    }
    const auto intervals=static_cast<unsigned>(std::ceil(std::numbers::pi*radius/feed/cycle_seconds));
    CHECK(path.samples.size()==intervals+1);
    CHECK(path.samples.front().steps==(StepPositions{6000,1600,-4800,0}));
    CHECK(path.samples.back().steps==(StepPositions{2000,1600,-4800,0}));
    return path;
}
std::vector<Segment> coalesced(const std::vector<Segment>& input) {
    std::vector<Segment> result;
    for (const auto& segment : input) {
        if (!result.empty() && result.back().to==segment.from &&
            exact_straight_extension(result.back().from,result.back().to,segment.to)) {
            result.back().to=segment.to; result.back().tick=segment.tick;
        } else result.push_back(segment);
    }
    return result;
}
std::unique_ptr<MaterialRemoval> serial(const std::vector<Segment>& segments) {
    auto cut=std::make_unique<MaterialRemoval>(*stock()->volume);
    cut->sweep(above,above,tool); // Same Enable stamp as the worker; above raw stock.
    CHECK(cut->stats().voxels_removed==0);
    cut->sweep(above,plunged,tool);
    for (const auto& segment : segments) cut->sweep(segment.from,segment.to,tool);
    return cut;
}
VoxelReference reference(bool arc) {
    return VoxelReference::read((fixtures/(arc ? "plunge-arc.rle" : "plunge.rle")).string());
}
void print_removal(const RemovalStats& s) {
    std::cout<<" sweeps="<<s.sweeps<<" chunks_tested="<<s.chunks_tested<<" chunks_changed="<<s.chunks_changed
             <<" voxels_tested="<<s.voxels_tested<<" removed="<<s.voxels_removed
             <<" volume_mm3="<<static_cast<double>(s.voxels_removed)*.001
             <<" broad_ms="<<s.broad_ms<<" narrow_ms="<<s.narrow_ms
             <<" mutation_ms="<<s.mutation_ms<<" invalidation_ms="<<s.invalidation_ms
             <<" containment_tests="<<s.containment_tests
             <<" batches="<<s.batch.batches<<" batch_segments="<<s.batch.segments
             <<" avg_segments="<<(s.batch.batches ? static_cast<double>(s.batch.segments)/static_cast<double>(s.batch.batches) : 0)
             <<" max_segments="<<s.batch.max_segments<<" candidate_chunks="<<s.batch.candidate_chunks
             <<" candidate_voxels="<<s.batch.candidate_voxels<<" occupied_candidates="<<s.batch.occupied_candidates
             <<" chunk_segment_refs="<<s.batch.chunk_segment_refs<<" batch_containment_tests="<<s.batch.containment_tests
             <<" envelope_rejects="<<s.batch.envelope_rejects<<" prefilter_skipped_refs="<<s.batch.prefilter_rejected_refs
             <<" first_hit_exits="<<s.batch.first_hit_exits<<" first_hit_skipped_refs="<<s.batch.first_hit_skipped_refs
             <<" batch_ms="<<s.batch.processing_ms;
}
void check_stats(const std::string& name, const MaterialRemoval& cut) {
    std::ifstream in(fixtures/"serial.stats"); std::string magic;
    CHECK(std::getline(in,magic) && magic=="CNC_CURVED_SERIAL_V1");
    std::string row, fingerprint;
    RemovalStats expected;
    while (in>>row>>expected.sweeps>>expected.chunks_tested>>expected.chunks_changed
             >>expected.voxels_tested>>expected.voxels_removed>>fingerprint) {
        if (row!=name) continue;
        const auto& actual=cut.stats();
        CHECK(actual.sweeps==expected.sweeps && actual.chunks_tested==expected.chunks_tested);
        CHECK(actual.chunks_changed==expected.chunks_changed && actual.voxels_tested==expected.voxels_tested);
        CHECK(actual.voxels_removed==expected.voxels_removed);
        CHECK(VoxelReference::capture(cut.volume()).fingerprint()==fingerprint);
        return;
    }
    throw std::runtime_error("missing serial baseline row: "+name);
}
void configuration(const Path& path) {
    const auto raw=stock();
    CHECK(raw->config.machine_min()==glm::dvec3(0,0,-20));
    CHECK(raw->config.machine_max()==glm::dvec3(30,20,-10));
    CHECK(raw->volume->dimensions()==(VoxelCoord{300,200,100}));
    CHECK(raw->volume->removed_voxels()==0 && raw->volume->stored_chunks()==0);
    CHECK(pose(path.samples.front())==plunged && pose(path.samples.back())==glm::dvec3(5,4,-12));
    validate_tool(tool);
    std::int64_t min_y=1600;
    for (std::size_t i=0; i<path.samples.size(); ++i) {
        const auto& p=path.samples[i].steps;
        CHECK(p[0]>=2000 && p[0]<=6000 && p[1]>=-400 && p[1]<=1600 && p[2]==-4800);
        if (i) CHECK(p[0]<=path.samples[i-1].steps[0]);
        min_y=std::min(min_y,p[1]);
    }
    CHECK(min_y==-400); // Clockwise lower half; intentionally crosses the stock's Y=0 boundary.
    CHECK(path.segments.size()==path.samples.size()-1); // Every sampled cycle changes XYZ for this fixture.
    // Fingerprint/exact comparison must ignore sparse insertion order/materialization.
    SparseVoxelVolume a(raw->volume->config()), b(raw->volume->config());
    a.erase({1,2,3}); a.erase({80,40,90}); b.erase({80,40,90}); b.erase({1,2,3});
    b.materialize_chunk({5,4,0});
    const auto capture=VoxelReference::capture(a); capture.require_equal(b);
    CHECK(capture.fingerprint()==VoxelReference::capture(b).fingerprint());
    std::cout<<"arc_samples="<<path.samples.size()<<" arc_events="<<path.segments.size()
             <<" theoretical_duration_ms="<<std::numbers::pi*radius/feed*1000
             <<" delivery_duration_ms="<<path.samples.back().tick<<" min_y_mm=-1\n";
}
struct Run { std::shared_ptr<const SparseVoxelVolume> volume; MaterialStatus status; double wall_ms{}; };
Run worker_run(const Path& path, bool arc, bool paced, unsigned workers, unsigned batch_ms=20) {
    const auto raw=stock(); // Fresh raw material for EVERY run, no pre-existing groove.
    const auto start=Clock::now();
    MaterialWorker worker(raw,{1,workers,batch_ms});
    MaterialEvent on; on.kind=MaterialEventKind::Enable; on.to=above; worker.enqueue(on);
    MaterialEvent plunge; plunge.from=above; plunge.to=plunged; worker.enqueue(plunge);
    const auto arc_start=Clock::now();
    if (arc) for (const auto& segment : path.segments) {
        if (paced) std::this_thread::sleep_until(arc_start+std::chrono::milliseconds(segment.tick));
        MaterialEvent event; event.from=segment.from; event.to=segment.to; worker.enqueue(event);
    }
    MaterialEvent capture; capture.kind=MaterialEventKind::Capture;
    capture.capture=std::make_shared<std::promise<std::shared_ptr<const SparseVoxelVolume>>>();
    auto future=capture.capture->get_future(); worker.enqueue(capture);
    worker.finish();
    Run run{future.get(),worker.status(),ms(Clock::now()-start)};
    const auto& s=run.status;
    CHECK(s.error.empty() && !worker.failed());
    CHECK(s.queue_depth==0 && s.mesh_queue_depth==0 && s.dirty_chunks==0 && s.lag_ms==0);
    CHECK(s.motion_received==1+(arc ? path.segments.size() : 0));
    CHECK(s.events_processed==s.motion_received+2); // Enable + Capture; no Reset/Tool events.
    CHECK(s.removal.sweeps==s.motion_coalesced+1); // Enable stamp, no omitted motion.
    // Deadline splits and conservative floating-point proofs depend on run boundaries;
    // offline greedy coalescing is not a scheduling-independent minimum count.
    CHECK(s.motion_coalesced>=(arc ? 3u : 1u) && s.motion_coalesced<=s.motion_received);
    CHECK(s.tool==tool);
    CHECK(s.mesh_rebuilds>=s.stale_mesh_jobs && s.mesh_queue_max<=560);
    const auto meshes=worker.meshes();
    CHECK(meshes && meshes->generation==1 && meshes->material_version==run.volume->version());
    CHECK(meshes->chunks.size()==280);
    return run;
}
void print_run(const Path& path, bool arc, bool paced, const Run& run) {
    const auto& s=run.status;
    const auto fingerprint=VoxelReference::capture(*run.volume).fingerprint();
    std::cout<<"case="<<(arc ? "plunge_arc" : "plunge")<<" mode="<<(paced ? "paced" : "burst")
             <<" generated_samples="<<(arc ? path.samples.size()+1 : 2)
             <<" generated_motion_events="<<(arc ? path.segments.size()+1 : 1)
             <<" motion_received="<<s.motion_received<<" coalesced="<<s.motion_coalesced
             <<" total_events="<<s.events_processed;
    print_removal(s.removal);
    std::cout<<" worker_ms="<<s.worker_ms<<" coalescing_ms="<<s.coalescing_ms
             <<" snapshot_ms="<<s.snapshot_ms<<" publication_ms="<<s.publication_ms
             <<" mesh_jobs="<<s.mesh_rebuilds<<" stale_jobs="<<s.stale_mesh_jobs
             <<" mesh_rebuilds="<<s.mesh_rebuilds<<" mesh_accepted="<<s.mesh_rebuilds-s.stale_mesh_jobs
             <<" mesh_ms="<<s.mesh_ms<<" queue_max="<<s.max_queue_depth
             <<" mesh_queue_peak="<<s.mesh_queue_max<<" mesh_queue_capacity=dynamic"
             <<" mesh_batch_bound=280 mesh_pending_bound=280"
             <<" final_lag_ms="<<s.lag_ms<<" wall_ms="<<run.wall_ms
             <<" material_workers="<<s.material_workers<<" mesh_workers="<<s.mesh_workers
             <<" material_batch_ms="<<s.material_batch_ms<<" parallel_peak="<<s.mesh_parallel_max<<" fingerprint="<<fingerprint<<'\n';
}
void record(const Path& path, const std::filesystem::path& directory) {
    // Explicit one-off reference capture; normal tests NEVER overwrite fixtures.
    CHECK(!std::filesystem::exists(directory));
    std::filesystem::create_directories(directory);
    std::ofstream stats(directory/"serial.stats"); stats<<"CNC_CURVED_SERIAL_V1\n";
    for (const auto& name : {"plunge","plunge_arc","coalesced_arc"}) {
        const std::string label(name);
        auto cut=serial(label=="plunge" ? std::vector<Segment>{} : label=="plunge_arc" ? path.segments : coalesced(path.segments));
        const auto volume=VoxelReference::capture(cut->volume());
        if (label!="coalesced_arc") volume.write((directory/(label=="plunge" ? "plunge.rle" : "plunge-arc.rle")).string());
        const auto& s=cut->stats();
        stats<<label<<' '<<s.sweeps<<' '<<s.chunks_tested<<' '<<s.chunks_changed<<' '
             <<s.voxels_tested<<' '<<s.voxels_removed<<' '<<volume.fingerprint()<<'\n';
        std::cout<<"reference="<<label; print_removal(s); std::cout<<" fingerprint="<<volume.fingerprint()<<'\n';
    }
    CHECK(stats.good());
}
} // namespace
int main(int argc, char** argv) {
    try {
        std::cout<<std::fixed<<std::setprecision(3);
        CHECK(argc>=2); const std::string mode(argv[1]); const auto path=load_path();
        if (mode=="record") { CHECK(argc==3); record(path,argv[2]); }
        else if (mode=="configuration") { CHECK(argc==2); configuration(path); }
        else if (mode=="plunge" || mode=="arc") {
            CHECK(argc==2); const bool arc=mode=="arc";
            const auto start=Clock::now(); auto cut=serial(arc ? path.segments : std::vector<Segment>{});
            const auto wall=ms(Clock::now()-start);
            reference(arc).require_equal(cut->volume()); check_stats(arc ? "plunge_arc" : "plunge",*cut);
            std::cout<<"serial="<<mode; print_removal(cut->stats()); std::cout<<" wall_ms="<<wall<<'\n';
            if (arc) {
                auto merged=serial(coalesced(path.segments));
                reference(true).require_equal(merged->volume()); check_stats("coalesced_arc",*merged);
                std::cout<<"offline_coalesced_arc_events="<<coalesced(path.segments).size(); print_removal(merged->stats()); std::cout<<'\n';
            }
        } else if (mode=="segmentation") {
            CHECK(argc==2); std::vector<Segment> split;
            for (const auto& segment : path.segments) {
                const auto middle=(segment.from+segment.to)*.5;
                // Only subdivide if the REPRESENTED midpoint lies exactly on this
                // original edge. Never resample the circle at different angles.
                if (middle!=segment.from && middle!=segment.to && exact_straight_extension(segment.from,middle,segment.to)) {
                    split.push_back({segment.from,middle,segment.tick}); split.push_back({middle,segment.to,segment.tick});
                } else split.push_back(segment);
            }
            CHECK(split.size()>path.segments.size());
            auto cut=serial(split); reference(true).require_equal(cut->volume());
            std::cout<<"original_edges="<<path.segments.size()<<" subdivided_edges="<<split.size(); print_removal(cut->stats()); std::cout<<'\n';
        } else if (mode=="repeat") {
            CHECK(argc==2); auto first=worker_run(path,true,false,4), second=worker_run(path,true,false,4);
            reference(true).require_equal(*first.volume); reference(true).require_equal(*second.volume);
            VoxelReference::capture(*first.volume).require_equal(*second.volume);
            print_run(path,true,false,first); print_run(path,true,false,second);
        } else if (mode=="chord") {
            CHECK(argc==2); auto chord=serial({{plunged,{5,4,-12},0}}); const auto arc=reference(true);
            const auto differences=arc.differences(chord->volume()); CHECK(differences>0);
            auto cell=[&](VoxelCoord p) { return arc.values.at(static_cast<std::size_t>(p.x+300*(p.y+200*p.z))); };
            CHECK(cell({100,40,80})==255 && chord->volume().sample({100,40,80})==0);
            CHECK(cell({100,0,80})==0 && chord->volume().sample({100,0,80})==255);
            std::cout<<"arc_chord_differing_voxels="<<differences<<" chord_removed="<<chord->stats().voxels_removed
                     <<" chord_fingerprint="<<VoxelReference::capture(chord->volume()).fingerprint()<<'\n';
        } else if (mode=="benchmark") {
            CHECK(argc==5 || argc==4 || argc==3); const std::string pacing(argv[2]); CHECK(pacing=="paced" || pacing=="burst");
            const unsigned workers=argc>=4 ? static_cast<unsigned>(std::stoul(argv[3])) : 4;
            const unsigned batch_ms=argc==5 ? static_cast<unsigned>(std::stoul(argv[4])) : 20;
            const bool paced=pacing=="paced";
            const auto plunge=worker_run(path,false,paced,workers,batch_ms); reference(false).require_equal(*plunge.volume);
            print_run(path,false,paced,plunge);
            const auto arc=worker_run(path,true,paced,workers,batch_ms); reference(true).require_equal(*arc.volume);
            print_run(path,true,paced,arc);
            const auto delta=arc.status.removal.voxels_removed-plunge.status.removal.voxels_removed;
            std::cout<<"arc_only_delta_removed="<<delta<<" arc_only_delta_mm3="<<static_cast<double>(delta)*.001
                     <<" independent_run_wall_delta_ms="<<arc.wall_ms-plunge.wall_ms<<'\n';
        } else throw std::runtime_error("unknown curved-material mode");
        std::cout<<"PASS: curved-material "<<mode<<'\n';
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
