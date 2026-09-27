#pragma once

#include <future>
#include <mutex>
#include <thread>
#include "network/UdpServer.hpp"
#include "protocol/StepperNinjaProtocol.hpp"
#include "recorder/MotionRecorder.hpp"
#include "io/VirtualSensors.hpp"
#include "volume/SparseVoxelVolume.hpp"
#include "tool/Tool.hpp"

namespace cnc {
enum class Action { Begin, Stop, Clear, Save, Status, IO };
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
    const SparseVoxelVolume* volume() const { return volume_.get(); }
    // Only one UI caller; completion is at a packet boundary, never mid-packet.
    CommandResult command(Action action, IOCommand io = {});
    void stop();
private:
    struct Pending { Action action; IOCommand io; std::promise<CommandResult> completion; };
    void run(std::stop_token token);
    StepperNinjaProtocol device_;
    UdpServer server_;
    MotionRecorder recorder_;
    VirtualSensors sensors_;
    std::unique_ptr<const SparseVoxelVolume> volume_; // Immutable Phase-4A scene.
    Scales scales_;
    mutable std::mutex mailbox_;
    SimulationStatus published_;
    std::unique_ptr<Pending> pending_;
    bool finished_{};
    std::jthread worker_; // Last: all accessed members exist before it starts.
};
} // namespace cnc
