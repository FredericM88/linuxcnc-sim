#include "simulation/Simulation.hpp"

#include <chrono>
#include <stdexcept>

namespace cnc {
Simulation::Simulation(const std::string& address, std::uint16_t port, Scales scales,
                       const std::vector<IOCommand>& initial_io, std::optional<SceneConfig> scene)
    : device_(scales), server_(address, port),
      volume_(scene ? std::make_unique<const SparseVoxelVolume>(*scene) : nullptr), scales_(scales) {
    for (const auto& command : initial_io) sensors_.execute(command, device_.machine().positions());
    published_.io = sensors_.status();
    worker_ = std::jthread([this](std::stop_token token) { run(token); });
}
Simulation::~Simulation() { stop(); }
void Simulation::stop() {
    worker_.request_stop();
    if (worker_.joinable()) worker_.join();
}
SimulationStatus Simulation::status() const {
    std::lock_guard lock(mailbox_);
    return published_;
}
MachineSnapshot Simulation::machine_snapshot() const {
    std::lock_guard lock(mailbox_);
    return {published_.positions, scales_, published_.packets.accepted_packets};
}
CommandResult Simulation::command(Action action, IOCommand io) {
    auto request = std::make_unique<Pending>();
    request->action = action;
    request->io = io;
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
                    case Action::IO:
                        sensors_.execute(request->io, device_.machine().positions());
                        result.io = sensors_.status();
                        break;
                    }
                    publish();
                    request->completion.set_value(std::move(result));
                } catch (...) { request->completion.set_exception(std::current_exception()); }
            }
            auto datagram = server_.receive(10);
            const auto now = elapsed();
            if (datagram) {
                const auto previous = device_.machine().positions();
                const auto accepted = device_.accept({datagram->bytes.data(), datagram->size}, now);
                if (accepted) {
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
