#pragma once

#include <optional>
#include <span>
#include "machine/MachineState.hpp"
#include "spindle/VirtualSpindle.hpp"

namespace cnc {
struct StepBurst {
    bool active{};
    bool direction{};
    std::uint32_t steps{};
    std::uint32_t timing{};
    std::int64_t delta() const {
        return direction ? static_cast<std::int64_t>(steps) : -static_cast<std::int64_t>(steps);
    }
};
StepBurst decode_step_burst(std::uint32_t word);
std::uint16_t decode_pwm_wrap(std::uint32_t frequency);
double decode_pwm_fraction(std::uint32_t duty, std::uint32_t frequency);
SpindleCommand decode_spindle_command(const Request& request);

class PacketSequence {
public:
    // Firmware starts with expected ID 0 and synchronizes on any mismatch.
    bool observe(std::uint8_t id);
    std::uint8_t expected() const { return expected_; }
private:
    std::uint8_t expected_{};
};

struct Statistics {
    std::uint64_t received_packets{};
    std::uint64_t accepted_packets{};
    std::uint64_t invalid_packets{};
    std::uint64_t length_errors{};
    std::uint64_t checksum_errors{};
    std::uint64_t timing_errors{};
    std::uint64_t position_overflows{};
    std::uint64_t packet_id_gaps{}; // Discontinuities, not a count of missing packets.
};

struct AcceptedPacket {
    std::uint8_t id{};
    std::uint64_t elapsed_us{};
    std::uint32_t jitter{};
    SpindleCommand spindle_command;
};

class StepperNinjaProtocol {
public:
    explicit StepperNinjaProtocol(Scales scales = {default_scale, default_scale,
                                                 default_scale, default_scale}) : machine_(scales) {}
    // elapsed_us is monotonic time since process startup. Invalid requests have no reply.
    std::optional<Response> process(std::span<const std::uint8_t> packet, std::uint64_t elapsed_us);
    // Validate and integrate atomically. Simulation evaluates sensors after this
    // succeeds and before building the response for the same packet.
    std::optional<AcceptedPacket> accept(std::span<const std::uint8_t> packet, std::uint64_t elapsed_us);
    static Response make_response(const AcceptedPacket& packet,
                                  const std::array<std::uint32_t, 4>& inputs = {},
                                  const SpindleFeedback* spindle = nullptr);
    const MachineState& machine() const { return machine_; }
    const Statistics& stats() const { return stats_; }
private:
    MachineState machine_;
    PacketSequence sequence_;
    Statistics stats_;
    std::uint64_t last_packet_us_{};
};
} // namespace cnc
