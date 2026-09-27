#include "meshing/SurfaceMesher.hpp"
#include "simulation/Simulation.hpp"
#include <atomic>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " #x); } while (false)
using namespace cnc;
template<class F> void rejects(F f) { bool caught = false; try { f(); } catch (const std::exception&) { caught = true; } CHECK(caught); }
void volume_test() {
    SparseVoxelVolume volume;
    CHECK(volume.config().volume.chunk_size == 32);
    CHECK(volume.dimensions() == (VoxelCoord{500,500,100}));
    CHECK(volume.sample({0,0,0}) == 255 && volume.sample({499,499,99}) == 255);
    CHECK(volume.sample({500,0,0}) == 0 && volume.sample({0,500,0}) == 0 && volume.sample({0,0,100}) == 0);
    CHECK(volume.sample({-1,0,0}) == 0 && volume.sample({0,-1,0}) == 0 && volume.sample({0,0,-1}) == 0);
    CHECK(volume.local_to_voxel({0.3, -0.3, -0.00001}) == (VoxelCoord{3,-3,-1}));
    CHECK(volume.local_to_voxel({3.2, -3.2, -3.20001}) == (VoxelCoord{32,-32,-33}));
    CHECK(volume.local_to_voxel({-1e-16,0,0}).x == -1);
    CHECK(volume.voxel_to_chunk({-1,-32,-33}) == (ChunkCoord{-1,-1,-2}));
    CHECK(volume.chunk_local({-1,-32,-33}) == (VoxelCoord{31,0,31}));
    for (std::int64_t x = -100; x <= 100; ++x) {
        const VoxelCoord p{x, x+1, x-1};
        const auto origin = volume.chunk_origin(volume.voxel_to_chunk(p));
        const auto local = volume.chunk_local(p);
        for (int i = 0; i < 3; ++i) { CHECK(local[i] >= 0 && local[i] < 32); CHECK(origin[i] + local[i] == p[i]); }
    }
    CHECK(floor_div(std::numeric_limits<std::int64_t>::min(),32) == std::numeric_limits<std::int64_t>::min()/32);
    CHECK(volume.chunk_local({std::numeric_limits<std::int64_t>::min(),std::numeric_limits<std::int64_t>::max(),0}) == (VoxelCoord{0,31,0}));
    rejects([] { floor_div(1,0); });
    CHECK(volume.chunk_state({0,0,0}) == ChunkState::Solid);
    CHECK(volume.chunk_state({15,15,3}) == ChunkState::Mixed);
    CHECK(volume.chunk_state({16,0,0}) == ChunkState::Empty);
    CHECK(volume.stored_chunks() == 0 && volume.stored_voxel_bytes() == 0);
    CHECK(volume.materialize_chunk({0,0,0}).values.empty());
    CHECK(volume.materialize_chunk({-1,0,0}).values.empty());
    const auto& boundary = volume.materialize_chunk({15,15,3});
    CHECK(boundary.state == ChunkState::Mixed && boundary.values.size() == 32768);
    CHECK(volume.stored_voxel_bytes() == 32768 && volume.stored_chunks() == 3);
    volume.materialize_chunk({15,15,3}); CHECK(volume.stored_chunks() == 3);
    CHECK(volume.sample({499,499,99}) == 255 && volume.sample({500,499,99}) == 0);
    CHECK(volume.sample({499,500,99}) == 0 && volume.sample({499,499,100}) == 0);
    SceneConfig config;
    config.volume.voxel_size_mm = 0.25; config.stock_size_mm = {1.01,2,3};
    config.workpiece.translation = {10,-20,5};
    config.workpiece.orientation = glm::angleAxis(glm::radians(90.0),glm::dvec3(1,0,0));
    SparseVoxelVolume transformed(config);
    CHECK(transformed.dimensions() == (VoxelCoord{5,8,12}));
    CHECK(glm::length(transformed.extent_mm() - glm::dvec3(1.25,2,3)) < 1e-10);
    const auto& transform = transformed.config().workpiece;
    CHECK(glm::length(transform.to_machine({0,0,0}) - glm::dvec3(10,-20,5)) < 1e-10);
    CHECK(glm::length(transform.to_machine({0,1,0}) - glm::dvec3(10,-20,6)) < 1e-10);
    CHECK(glm::length(transform.to_local(transform.to_machine({-1,2,3})) - glm::dvec3(-1,2,3)) < 1e-10);
    CHECK(glm::length(glm::dvec3(transform.matrix()*glm::dvec4(0,1,0,1)) - transform.to_machine({0,1,0})) < 1e-10);
    config.volume.voxel_size_mm = 0; rejects([&] { SparseVoxelVolume bad(config); });
    config.volume.voxel_size_mm = 0.1; config.stock_size_mm.x = -1; rejects([&] { SparseVoxelVolume bad(config); });
    rejects([&] { volume.local_to_voxel({std::numeric_limits<double>::infinity(),0,0}); });
    rejects([&] { volume.chunk_origin({std::numeric_limits<std::int64_t>::max(),0,0}); });
    config = {}; config.stock_size_mm = {10000,10000,10000};
    SparseVoxelVolume huge(config); CHECK(huge.stored_chunks() == 0 && huge.stored_voxel_bytes() == 0);
}
double area(const ChunkMesh& mesh) {
    double total = 0;
    CHECK(mesh.indices.size() % 3 == 0);
    for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
        const auto& a = mesh.vertices.at(mesh.indices[i]);
        const auto& b = mesh.vertices.at(mesh.indices[i+1]);
        const auto& c = mesh.vertices.at(mesh.indices[i+2]);
        const auto cross = glm::cross(b.position-a.position,c.position-a.position);
        CHECK(glm::dot(cross,a.normal) > 0); // All winding and normals point out.
        total += glm::length(cross) * 0.5;
    }
    return total;
}
void mesh_test() {
    SceneConfig config; config.volume.voxel_size_mm = 1; config.stock_size_mm = {1,1,1};
    SurfaceMesher mesher;
    SparseVoxelVolume single(config);
    CHECK(mesher.build(single,{-1,0,0}).vertices.empty());
    CHECK(mesher.build(single,{1,0,0}).indices.empty());
    auto mesh = mesher.build(single,{0,0,0});
    CHECK(mesh.vertices.size() == 24 && mesh.indices.size() == 36 && area(mesh) == 6);
    config.stock_size_mm = {2,1,1};
    CHECK(area(mesher.build(SparseVoxelVolume(config),{0,0,0})) == 10);
    // Join across x=32: neither side emits a face in the shared plane.
    config.stock_size_mm = {64,32,32};
    SparseVoxelVolume joined(config);
    double total = 0;
    for (int x = 0; x < 2; ++x) {
        const auto half = mesher.build(joined,{x,0,0}); total += area(half);
        CHECK(half.indices.size() == 30);
        for (const auto& v : half.vertices) CHECK(!(v.position.x == 32 && v.normal.x != 0));
    }
    CHECK(total == 2*(64*32 + 64*32 + 32*32));
    config.stock_size_mm = {96,96,96};
    CHECK(mesher.build(SparseVoxelVolume(config),{1,1,1}).indices.empty());
    // Non-chunk-aligned box, all three seams, default decimal voxel pitch.
    config.volume.voxel_size_mm = .1; config.stock_size_mm = {3.5,3.3,3.4};
    SparseVoxelVolume partial(config); total = 0;
    for (int z=0;z<2;++z) for (int y=0;y<2;++y) for (int x=0;x<2;++x) {
        const auto m = mesher.build(partial,{x,y,z}); total += area(m);
        for (const auto& vertex : m.vertices) for (int axis=0;axis<3;++axis)
            if (vertex.normal[axis] != 0) CHECK(vertex.position[axis] == 0 ||
                vertex.position[axis] == static_cast<float>(partial.extent_mm()[axis]));
    }
    CHECK(std::abs(total - 2*(3.5*3.3 + 3.5*3.4 + 3.3*3.4)) < 1e-4);
    CHECK(partial.stored_voxel_bytes() == 0);
    DirtyChunks dirty; dirty.mark({-1,0,0}); dirty.mark({-1,0,0}); dirty.mark({0,0,0});
    CHECK(dirty.take().size() == 2 && dirty.take().empty());
}
void snapshot_test() {
    const Scales scales{400,400,400,100};
    const MachineSnapshot initial{{4000,-4000,800,-100},scales,1};
    const auto pose = tool_pose(initial);
    CHECK(pose.tip_mm == glm::dvec3(10,-10,2) && pose.a_units == -1);
    CHECK(initial.steps == (StepPositions{4000,-4000,800,-100}));
    rejects([] { tool_pose({}); });
    Simulation simulation("127.0.0.1",0,scales,{},SceneConfig{});
    CHECK(simulation.volume() && simulation.volume()->stored_chunks() == 0);
    CHECK(simulation.machine_snapshot().steps == StepPositions{});
    const int fd = socket(AF_INET,SOCK_DGRAM,0); CHECK(fd >= 0);
    timeval timeout{2,0}; CHECK(setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout)) == 0);
    sockaddr_in target{}; target.sin_family = AF_INET; target.sin_port = htons(simulation.port()); target.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    std::atomic<bool> done{false}, coherent{true};
    std::jthread reader([&] {
        while (!done) {
            const auto snapshot = simulation.machine_snapshot();
            const auto n = static_cast<std::int64_t>(snapshot.accepted_packets);
            if (snapshot.steps != (StepPositions{4*n,-4*n,2*n,n}) || snapshot.scales != scales) coherent = false;
            const auto rendered = tool_pose(snapshot);
            if (rendered.tip_mm.x != -rendered.tip_mm.y) coherent = false;
            std::this_thread::yield();
        }
    });
    bool exchanges_ok = true;
    for (unsigned i = 0; i < 1000; ++i) {
        Request request{}; request.pio_timing = 235; request.packet_id = static_cast<std::uint8_t>(i);
        request.stepgen_command[0] = 0x80000000u | (19494u << 10) | 3;
        request.stepgen_command[1] = (19494u << 10) | 3;
        request.stepgen_command[2] = 0x80000000u | (19494u << 10) | 1;
        request.stepgen_command[3] = 0x80000000u | (19494u << 10);
        request.checksum = calculate_checksum(&request,sizeof(request)-1);
        Response response{};
        if (sendto(fd,&request,sizeof(request),0,reinterpret_cast<sockaddr*>(&target),sizeof(target)) != sizeof(request) ||
            recv(fd,&response,sizeof(response),0) != sizeof(response) || response.packet_id != request.packet_id) { exchanges_ok = false; break; }
    }
    done = true; reader.join(); close(fd);
    CHECK(exchanges_ok && coherent);
    simulation.command(Action::Status);
    const auto final = simulation.machine_snapshot();
    CHECK(final.steps == (StepPositions{4000,-4000,2000,1000}) && final.accepted_packets == 1000);
    CHECK(simulation.status().positions == final.steps);
    CHECK(tool_pose(final).tip_mm == glm::dvec3(10,-10,5));
    CHECK(initial.steps[0] == 4000); // Earlier copy stays independent.
}
int main(int argc, char** argv) {
    try {
        CHECK(argc == 2);
        const std::string test = argv[1];
        if (test == "volume") volume_test();
        else if (test == "mesher") mesh_test();
        else if (test == "snapshot") snapshot_test();
        else throw std::runtime_error("unknown test");
        std::cout << "PASS: Phase 4A " << test << '\n'; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
