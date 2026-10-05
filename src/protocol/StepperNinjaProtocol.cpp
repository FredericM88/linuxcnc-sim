#include "protocol/StepperNinjaProtocol.hpp"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <pio_settings.h>

namespace cnc {
StepBurst decode_step_burst(std::uint32_t word) {
    if (word == 0) return {};
    return {true, (word >> 31) != 0, (word & 0x3ffu) + 1u,
            (word & 0x7fffffffu) >> 10};
}

std::uint16_t decode_pwm_wrap(std::uint32_t frequency) {
    if (frequency == 0) return 0;
    std::uint32_t raw_wrap = 200000000u / frequency;
    if (frequency < 1908) raw_wrap = 65535;
    // Intentional: the pinned driver and firmware both narrow to uint16_t.
    return static_cast<std::uint16_t>(raw_wrap);
}

double decode_pwm_fraction(std::uint32_t duty, std::uint32_t frequency) {
    const auto wrap = decode_pwm_wrap(frequency);
    if (wrap == 0) return 0.0;
    return std::clamp(static_cast<double>(duty) / static_cast<double>(wrap), 0.0, 1.0);
}

SpindleCommand decode_spindle_command(const Request& request) {
    SpindleCommand command;
    command.enabled = (request.outputs[0] & 0x1u) != 0;
    command.forward = (request.outputs[0] & 0x2u) == 0;
    command.pwm_duty = request.pwm_duty[0];
    command.pwm_frequency = request.pwm_frequency[0];
    command.pwm_fraction = decode_pwm_fraction(command.pwm_duty, command.pwm_frequency);
    command.index_enable = (request.enc_control & 0x1u) != 0;
    return command;
}

bool PacketSequence::observe(std::uint8_t id) {
    const bool gap = id != expected_;
    expected_ = static_cast<std::uint8_t>(id + 1u);
    return gap;
}

std::optional<Response> StepperNinjaProtocol::process(
    std::span<const std::uint8_t> packet, std::uint64_t elapsed_us) {
    const auto accepted = accept(packet, elapsed_us);
    if (!accepted) return std::nullopt;
    return make_response(*accepted);
}

std::optional<AcceptedPacket> StepperNinjaProtocol::accept(
    std::span<const std::uint8_t> packet, std::uint64_t elapsed_us) {
    ++stats_.received_packets;
    if (packet.size() != request_size) {
        ++stats_.invalid_packets;
        ++stats_.length_errors;
        return std::nullopt;
    }
    Request request{};
    // Copy into the original packed type; never cast unaligned network storage.
    std::memcpy(&request, packet.data(), sizeof(request));
    if (!rx_checksum_ok(&request)) {
        ++stats_.invalid_packets;
        ++stats_.checksum_errors;
        return std::nullopt;
    }
    if (request.pio_timing >= std::size(pio_settings)) {
        ++stats_.invalid_packets;
        ++stats_.timing_errors;
        return std::nullopt;
    }
    StepPositions delta{};
    for (std::size_t i = 0; i < axis_count; ++i) {
        delta[i] = decode_step_burst(request.stepgen_command[i]).delta();
    }
    if (!machine_.integrate(delta)) {
        ++stats_.invalid_packets;
        ++stats_.position_overflows;
        return std::nullopt;
    }
    if (sequence_.observe(request.packet_id)) ++stats_.packet_id_gaps;
    ++stats_.accepted_packets;

    AcceptedPacket accepted{request.packet_id, elapsed_us,
                            static_cast<std::uint32_t>(elapsed_us - last_packet_us_),
                            decode_spindle_command(request)};
    last_packet_us_ = elapsed_us;
    return accepted;
}

Response StepperNinjaProtocol::make_response(const AcceptedPacket& packet,
                                             const std::array<std::uint32_t, 4>& inputs,
                                             const SpindleFeedback* spindle) {
    Response response{}; // Encoder counts/velocities/index flags and ring remain zero.
    for (std::size_t i = 0; i < inputs.size(); ++i) response.inputs[i] = inputs[i];
    for (std::size_t i = 0; i < encoder_count; ++i) {
        response.encoder_timestamp[i] = static_cast<std::uint32_t>(packet.elapsed_us);
    }
    if (spindle) {
        response.encoder_counter[0] = spindle->encoder_counter;
        response.encoder_velocity[0] = 0;
        response.encoder_timestamp[0] = spindle->encoder_timestamp;
        if (spindle->index_event) response.interrupt_data |= 0x1u;
    }
    response.jitter = packet.jitter;
    response.packet_id = packet.id; // Echo current request, then expect ID+1.
    response.checksum = calculate_checksum(&response, static_cast<std::uint8_t>(sizeof(response) - 1));
    return response;
}
} // namespace cnc
