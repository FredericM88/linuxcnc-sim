#include "console/WorkpieceCommands.hpp"
#include "simulation/Simulation.hpp"
#include "meshing/SurfaceMesher.hpp"
#include <atomic>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " #x); } while (false)
using namespace cnc;

template<class F> void rejects(F f) {
    bool caught = false;
    try { f(); } catch (const std::exception&) { caught = true; }
    CHECK(caught);
}
void apply(Simulation& sim, const std::string& text) {
    auto command = parse_workpiece_command(text, sim.workpiece_snapshot()->config);
    if (!command.show) sim.configure_workpiece(command.config);
}
void bounds(const WorkpieceSnapshot& s, glm::dvec3 low, glm::dvec3 high) {
    CHECK(s.config.machine_min() == low && s.config.machine_max() == high);
    CHECK(s.volume->config().workpiece.to_machine({0,0,0}) == low);
    CHECK(s.volume->config().workpiece.to_machine(s.config.size_mm) == high);
}
void geometry() {
    Simulation sim("127.0.0.1", 0, {400,400,400,400});
    apply(sim, "workpiece size 100 60 20");
    apply(sim, "workpiece origin min min max");
    bounds(*sim.workpiece_snapshot(), {0,0,-20}, {100,60,0});
    apply(sim, "workpiece origin center center max");
    bounds(*sim.workpiece_snapshot(), {-50,-30,-20}, {50,30,0});
    apply(sim, "workpiece position 50 30 0");
    apply(sim, "workpiece origin 10 15 20");
    bounds(*sim.workpiece_snapshot(), {40,15,-20}, {140,75,0});
    apply(sim, "workpiece origin 10 center max");
    bounds(*sim.workpiece_snapshot(), {40,0,-20}, {140,60,0});
    apply(sim, "workpiece origin 10.125 0.25 19.875");
    bounds(*sim.workpiece_snapshot(), {39.875,29.75,-19.875}, {139.875,89.75,.125});
    apply(sim, "workpiece origin .125 +2.5 1e1");
    CHECK(sim.workpiece_snapshot()->config.origin_offset_mm == glm::dvec3(.125,2.5,10));
    apply(sim, "workpiece origin max min center");
    apply(sim, "workpiece size 120 70 30");
    CHECK(sim.workpiece_snapshot()->config.origin_offset_mm == glm::dvec3(100,0,10));
    apply(sim, "workpiece reset");
    const WorkpieceConfig defaults;
    const auto s = sim.workpiece_snapshot();
    CHECK(s->config.size_mm == defaults.size_mm);
    CHECK(s->config.position_machine_mm == defaults.position_machine_mm);
    CHECK(s->config.origin_offset_mm == defaults.origin_offset_mm);
    CHECK(s->config.voxel_size_mm == defaults.voxel_size_mm);
    bounds(*s, defaults.machine_min(), defaults.machine_max());
}
void transactions() {
    Simulation sim("127.0.0.1", 0, {400,400,400,400});
    for (const auto* line : {
        "workpiece", "workpiece foo", "workpiece show extra", "workpiece reset extra",
        "workpiece size 0 60 20", "workpiece size -1 60 20", "workpiece size 1 1 1",
        "workpiece size 100 60", "workpiece size 100 60 20 extra", "workpiece size inf 60 20",
        "workpiece position nan 0 0", "workpiece position 0 inf 0", "workpiece position 0 0 -inf",
        "workpiece position 1 2 3mm", "workpiece position 1e309 0 0", "workpiece position 1e300 0 0",
        "workpiece origin foo center max", "workpiece origin -1 center max",
        "workpiece origin 51 center max", "workpiece origin nan 0 0", "workpiece origin min min inf",
        "workpiece origin min max", "workpiece origin min max center extra", "workpiece origin MIN min max",
        "workpiece voxel 0", "workpiece voxel -0.1", "workpiece voxel nan", "workpiece voxel inf",
        "workpiece voxel 1e-300", "workpiece voxel 0.00001", "workpiece voxel 1e300",
        "workpiece voxel .1 extra", "workpiece size 1e300 1e300 1e300"}) {
        const auto before = sim.workpiece_snapshot();
        rejects([&] { apply(sim, line); });
        CHECK(sim.workpiece_snapshot() == before); // Config, volume and revision unchanged.
    }
    const auto before = sim.workpiece_snapshot();
    apply(sim, "workpiece show");
    CHECK(sim.workpiece_snapshot() == before);
    auto c = before->config;
    c.position_machine_mm.x = std::numeric_limits<double>::infinity();
    rejects([&] { sim.configure_workpiece(c); });
    CHECK(sim.workpiece_snapshot() == before);
    c = before->config; c.origin_offset_mm.z = -1;
    rejects([&] { sim.configure_workpiece(c); });
    CHECK(sim.workpiece_snapshot() == before);
    apply(sim, "workpiece origin 0 0 0");
    apply(sim, "workpiece size .11 .21 .31");
    CHECK(glm::length(sim.volume()->extent_mm() - glm::dvec3(.2,.3,.4)) < 1e-12);
    CHECK(sim.workpiece_snapshot()->config.machine_max() == glm::dvec3(.11,.21,.31));
}
void runtime() {
    Simulation sim("127.0.0.1", 0, {400,400,400,400});
    apply(sim, "workpiece origin min min min");
    apply(sim, "workpiece size 4 4 4");
    const auto before = sim.workpiece_snapshot();
    std::atomic<bool> done{false}, coherent{true};
    std::jthread reader([&] {
        while (!done) {
            const auto s = sim.workpiece_snapshot();
            const auto& v = s->volume->config();
            if (v.stock_size_mm != s->config.size_mm || v.workpiece.translation != s->config.machine_min() ||
                v.volume.voxel_size_mm != s->config.voxel_size_mm) coherent = false;
            std::this_thread::yield();
        }
    });
    for (int i = 0; i < 100; ++i) {
        auto c = sim.workpiece_snapshot()->config;
        c.position_machine_mm = {i,-i,i}; c.voxel_size_mm = i % 2 ? .1 : .2;
        sim.configure_workpiece(c);
    }
    done = true; reader.join();
    CHECK(coherent);
    const auto after = sim.workpiece_snapshot();
    CHECK(after->revision == before->revision + 100 && after->volume != before->volume);
    CHECK(before->config.position_machine_mm == glm::dvec3(0));
    CHECK(before->volume->config().workpiece.translation == glm::dvec3(0));
    CHECK(after->volume->stored_chunks() == 0 && after->volume->stored_voxel_bytes() == 0);
    CHECK(sim.machine_snapshot().steps == StepPositions{});
    apply(sim, "workpiece voxel .2");
    CHECK(sim.volume()->dimensions() == (VoxelCoord{20,20,20}));
    SurfaceMesher mesher;
    CHECK(!mesher.build(*before->volume,{1,0,0}).indices.empty());
    CHECK(mesher.build(*sim.volume(),{1,0,0}).indices.empty());
    CHECK(!mesher.build(*sim.volume(),{0,0,0}).indices.empty());
    const auto revision = sim.workpiece_snapshot()->revision;
    apply(sim, "workpiece reset");
    CHECK(sim.workpiece_snapshot()->revision == revision + 1);
    SceneConfig legacy;
    legacy.workpiece.translation = {12,13,14};
    legacy.workpiece.orientation = glm::angleAxis(glm::radians(45.0), glm::dvec3(1,0,0));
    Simulation old("127.0.0.1", 0, {400,400,400,400}, {}, legacy);
    CHECK(old.workpiece_snapshot()->legacy_rotation);
    const auto legacy_snapshot = old.workpiece_snapshot();
    CHECK(old.volume()->config().workpiece.to_machine(legacy_snapshot->config.origin_offset_mm) ==
          legacy_snapshot->config.position_machine_mm);
    rejects([&] { apply(old, "workpiece voxel nan"); });
    CHECK(old.workpiece_snapshot() == legacy_snapshot);
    apply(old, "workpiece show");
    CHECK(old.workpiece_snapshot() == legacy_snapshot);
    CHECK(old.volume()->config().workpiece.translation == legacy.workpiece.translation);
    apply(old, "workpiece position 1 2 3");
    CHECK(!old.workpiece_snapshot()->legacy_rotation);
    CHECK(old.volume()->config().workpiece.orientation == glm::dquat(1,0,0,0));
    bounds(*old.workpiece_snapshot(), glm::dvec3(1,2,3) - old.workpiece_snapshot()->config.origin_offset_mm,
           glm::dvec3(1,2,3) - old.workpiece_snapshot()->config.origin_offset_mm + legacy.stock_size_mm);
}
int main(int argc, char** argv) {
    try {
        CHECK(argc == 2);
        const std::string test = argv[1];
        if (test == "geometry") geometry();
        else if (test == "transactions") transactions();
        else if (test == "runtime") runtime();
        else throw std::runtime_error("unknown test");
        std::cout << "PASS: workpiece " << test << '\n';
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
