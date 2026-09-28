#include "simulation/Simulation.hpp"

#include <chrono>
#include <stdexcept>

namespace cnc {
Simulation::Simulation(const std::string& address, std::uint16_t port, Scales scales,
                       const std::vector<IOCommand>& initial_io, std::optional<SceneConfig> scene, MaterialWorkerConfig workers)
    : device_(scales), server_(address, port),
      scales_(scales) {
    if (scene) {
        auto volume = std::make_shared<const SparseVoxelVolume>(*scene);
        validate_workpiece_volume(*volume);
        WorkpieceConfig config;
        config.size_mm = scene->stock_size_mm;
        const bool rotated = volume->config().workpiece.orientation != glm::dquat(1,0,0,0);
        // The minimum corner is invariant under a legacy rotation about local zero.
        // Selecting it keeps position a true G53 reference point even in legacy scenes.
        config.origin_offset_mm = rotated ? glm::dvec3(0) : glm::dvec3(0, 0, config.size_mm.z);
        config.position_machine_mm = scene->workpiece.translation + config.origin_offset_mm;
        config.voxel_size_mm = scene->volume.voxel_size_mm;
        workpiece_ = std::make_shared<const WorkpieceSnapshot>(WorkpieceSnapshot{
            config, volume, 1, rotated});
    } else configure_workpiece(WorkpieceConfig{});
    for (const auto& command : initial_io) sensors_.execute(command, device_.machine().positions());
    published_.io = sensors_.status();
    material_ = std::make_unique<MaterialWorker>(workpiece_snapshot(), workers);
    worker_ = std::jthread([this](std::stop_token token) { run(token); });
}
Simulation::~Simulation() { stop(); }
void Simulation::stop() {
    worker_.request_stop();
    if (worker_.joinable()) worker_.join();
    if (material_) material_->finish();
}
SimulationStatus Simulation::status() const {
    std::lock_guard lock(mailbox_);
    return published_;
}
MachineSnapshot Simulation::machine_snapshot() const {
    std::lock_guard lock(mailbox_);
    return {published_.positions, scales_, published_.packets.accepted_packets};
}
std::shared_ptr<const WorkpieceSnapshot> Simulation::workpiece_snapshot() const {
    std::lock_guard lock(workpiece_mutex_);
    return workpiece_;
}
void Simulation::configure_workpiece(const WorkpieceConfig& config) {
    // All validation/allocation precedes publication. Readers retain old snapshots.
    auto next = std::make_shared<WorkpieceSnapshot>();
    next->config = config;
    next->volume = build_workpiece(config);
    const auto current = workpiece_snapshot();
    next->revision = current ? current->revision + 1 : 1;
    if (material_) {
        MaterialEvent event; event.kind = MaterialEventKind::Workpiece; event.workpiece = next;
        material_command(std::move(event));
    }
    std::shared_ptr<const WorkpieceSnapshot> published = next;
    {
        std::lock_guard lock(workpiece_mutex_);
        workpiece_.swap(published);
    }
}
CommandResult Simulation::command(Action action, IOCommand io) {
    auto request = std::make_unique<Pending>();
    request->action = action;
    request->io = io;
    return submit(std::move(request));
}
void Simulation::material_command(MaterialEvent event) {
    if (event.kind == MaterialEventKind::Tool) validate_tool(event.tool);
    auto request = std::make_unique<Pending>();
    request->action = Action::Material; request->material = std::move(event);
    submit(std::move(request));
}
std::shared_ptr<const SparseVoxelVolume> Simulation::material_snapshot() {
    MaterialEvent event; event.kind = MaterialEventKind::Capture;
    event.capture = std::make_shared<std::promise<std::shared_ptr<const SparseVoxelVolume>>>();
    auto future = event.capture->get_future();
    material_command(std::move(event));
    return future.get();
}
CommandResult Simulation::submit(std::unique_ptr<Pending> request) {
    auto result = request->completion.get_future();
    {
        std::lock_guard lock(mailbox_);
        if (finished_) throw std::runtime_error("simulation is stopped");
        if (pending_) throw std::runtime_error("command already pending");
        pending_ = std::move(request);
    }
    return result.get(); // UI thread only. The UDP thread never waits for UI.
}

void Simulation::run(std::stop_token token) {
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    auto elapsed = [&] { return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count()); };
    SimulationStatus state;
    std::uint64_t last_publish = 0;
    auto publish = [&] {
        state.packets = device_.stats();
        state.positions = device_.machine().positions();
        state.recording = recorder_.active();
        state.recorder_failed = recorder_.failed();
        state.samples = recorder_.count();
        state.io = sensors_.status();
        state.now_us = elapsed();
        std::lock_guard lock(mailbox_);
        published_ = state;
        last_publish = state.now_us;
    };
    try {
        while (!token.stop_requested()) {
            if (material_->failed()) throw std::runtime_error("material worker failed; simulation incomplete (see material diagnostics)");
            std::unique_ptr<Pending> request;
            {
                std::lock_guard lock(mailbox_);
                request = std::move(pending_);
            }
            if (request) {
                try {
                    CommandResult result;
                    switch (request->action) {
                    case Action::Begin:
                        result.message = recorder_.begin(device_.machine().positions(), elapsed())
                            ? "Recording started (new recording; 1 start sample)." : "Already recording; unchanged.";
                        break;
                    case Action::Stop:
                        result.message = recorder_.active() ? "Recording stopped." : "Recorder already stopped.";
                        recorder_.stop();
                        break;
                    case Action::Clear:
                        recorder_.clear();
                        result.message = "Recording cleared; recorder stopped.";
                        break;
                    case Action::Save:
                        result.recording = recorder_.snapshot();
                        result.message = "Recording snapshot ready.";
                        break;
                    case Action::Status: result.message = "Status updated."; break;
                    case Action::Material:
                        request->material.to = tool_pose({device_.machine().positions(), scales_, 0}).tip_mm;
                        request->material.time = Clock::now();
                        material_->enqueue(std::move(request->material));
                        result.message = "Material event queued.";
                        break;
                    case Action::IO:
                        sensors_.execute(request->io, device_.machine().positions());
                        result.io = sensors_.status();
                        break;
                    }
                    publish();
                    request->completion.set_value(std::move(result));
                } catch (...) {
                    request->completion.set_exception(std::current_exception());
                    if (request->action == Action::Material) throw;
                }
            }
            auto datagram = server_.receive(10);
            const auto now = elapsed();
            if (datagram) {
                const auto previous = device_.machine().positions();
                const auto accepted = device_.accept({datagram->bytes.data(), datagram->size}, now);
                if (accepted) {
                    const auto current = device_.machine().positions();
                    if (previous[0] != current[0] || previous[1] != current[1] || previous[2] != current[2]) {
                        MaterialEvent event;
                        event.from = tool_pose({previous, scales_, 0}).tip_mm;
                        event.to = tool_pose({current, scales_, 0}).tip_mm;
                        material_->enqueue(std::move(event));
                    }
                    sensors_.update(previous, device_.machine().positions());
                    const auto response = StepperNinjaProtocol::make_response(*accepted, sensors_.inputs());
                    if (server_.send(response, datagram->sender)) ++state.sent;
                    else ++state.send_errors;
                    recorder_.observe(device_.machine().positions(), now);
                    state.last_valid_us = now;
                    state.peer = UdpServer::endpoint(datagram->sender);
                }
            }
            if (now - last_publish >= 20000) publish();
        }
    } catch (const std::exception& error) { state.fatal_error = error.what(); }
    publish();
    std::lock_guard lock(mailbox_);
    finished_ = true;
    if (pending_) {
        pending_->completion.set_exception(std::make_exception_ptr(std::runtime_error("simulation stopped")));
        pending_.reset();
    }
}
} // namespace cnc
