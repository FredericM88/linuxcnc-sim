#pragma once

#include <future>
#include <mutex>
#include <thread>
#include "network/UdpServer.hpp"
#include "protocol/StepperNinjaProtocol.hpp"
#include "recorder/MotionRecorder.hpp"
#include "io/VirtualSensors.hpp"
#include "simulation/Workpiece.hpp"
#include "tool/Tool.hpp"
#include "material/MaterialWorker.hpp"

namespace cnc {
enum class Action { Begin, Stop, Clear, Save, Status, IO, Material };
struct SimulationStatus {
    Statistics packets;
    StepPositions positions{};
    std::uint64_t sent{}, send_errors{}, samples{}, last_valid_us{}, now_us{};
    bool recording{}, recorder_failed{};
    std::string peer, fatal_error;
    IOStatus io;
};
struct CommandResult {
    std::string message;
    MotionSnapshot recording;
    IOStatus io;
};

class Simulation {
public:
    Simulation(const std::string& address, std::uint16_t port, Scales scales,
               const std::vector<IOCommand>& initial_io = {},
               std::optional<SceneConfig> scene = std::nullopt);
    ~Simulation();
    Simulation(const Simulation&) = delete;
    Simulation& operator=(const Simulation&) = delete;
    std::uint16_t port() const { return server_.port(); }
    SimulationStatus status() const;
    MachineSnapshot machine_snapshot() const;
    std::shared_ptr<const SparseVoxelVolume> volume() const { return workpiece_snapshot()->volume; }
    std::shared_ptr<const WorkpieceSnapshot> workpiece_snapshot() const;
    // Validation/allocation on the sole console caller; reset queued at a packet boundary.
    void configure_workpiece(const WorkpieceConfig& config);
    // Only one UI caller; completion is at a packet boundary, never mid-packet.
    CommandResult command(Action action, IOCommand io = {});
    void stop();
    void material_command(MaterialEvent event);
    MaterialStatus material_status() const { return material_->status(); }
    std::shared_ptr<const MaterialMeshes> material_meshes() const { return material_->meshes(); }
    // Diagnostic/test barrier: copy current material on worker, never in UDP.
    std::shared_ptr<const SparseVoxelVolume> material_snapshot();
private:
    struct Pending { Action action; IOCommand io; MaterialEvent material; std::promise<CommandResult> completion; };
    CommandResult submit(std::unique_ptr<Pending> request);
    void run(std::stop_token token);
    StepperNinjaProtocol device_;
    UdpServer server_;
    MotionRecorder recorder_;
    VirtualSensors sensors_;
    mutable std::mutex workpiece_mutex_; // Never acquired by UDP.
    std::shared_ptr<const WorkpieceSnapshot> workpiece_;
    Scales scales_;
    mutable std::mutex mailbox_;
    SimulationStatus published_;
    std::unique_ptr<Pending> pending_;
    bool finished_{};
    std::unique_ptr<MaterialWorker> material_;
    std::jthread worker_; // Last: all accessed members exist before it starts.
};
} // namespace cnc
