#include "protocol/StepperNinjaProtocol.hpp"

#include <cstring>
#include <iterator>
#include <pio_settings.h>

namespace cnc {
StepBurst decode_step_burst(std::uint32_t word) {
    if (word == 0) return {};
    return {true, (word >> 31) != 0, (word & 0x3ffu) + 1u,
            (word & 0x7fffffffu) >> 10};
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

    AcceptedPacket accepted{request.packet_id, elapsed_us, static_cast<std::uint32_t>(elapsed_us - last_packet_us_)};
    last_packet_us_ = elapsed_us;
    return accepted;
}

Response StepperNinjaProtocol::make_response(const AcceptedPacket& packet,
                                           const std::array<std::uint32_t, 4>& inputs) {
    Response response{}; // Encoder counts/velocities/index flags and ring remain zero.
    for (std::size_t i = 0; i < inputs.size(); ++i) response.inputs[i] = inputs[i];
    for (std::size_t i = 0; i < encoder_count; ++i) {
        response.encoder_timestamp[i] = static_cast<std::uint32_t>(packet.elapsed_us);
    }
    response.jitter = packet.jitter;
    response.packet_id = packet.id; // Echo current request, then expect ID+1.
    response.checksum = calculate_checksum(&response, static_cast<std::uint8_t>(sizeof(response) - 1));
    return response;
}
} // namespace cnc
