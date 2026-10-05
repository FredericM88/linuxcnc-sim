#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

#include "protocol/StepperNinjaProtocol.hpp"
#include "spindle/VirtualSpindle.hpp"

#define CHECK(condition) do { if (!(condition)) throw std::runtime_error(\
    std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " #condition); } while (false)

namespace {
constexpr double epsilon = 1e-9;

bool near(long double actual, long double expected, long double tolerance = epsilon) {
    return std::abs(actual - expected) <= tolerance;
}

std::vector<std::uint8_t> bytes(cnc::Request request) {
    request.checksum = calculate_checksum(&request, sizeof(request) - 1);
    std::vector<std::uint8_t> result(sizeof(request));
    std::memcpy(result.data(), &request, result.size());
    return result;
}

cnc::SpindleCommand command(bool enabled, bool forward, double fraction, bool index = false) {
    cnc::SpindleCommand result;
    result.enabled = enabled;
    result.forward = forward;
    result.pwm_fraction = fraction;
    result.index_enable = index;
    return result;
}

void decoding_and_speed() {
    cnc::Request request{};
    request.outputs[0] = 0x1u;
    request.outputs[1] = 0xffffffffu; // GPIO identity and word 1 are not spindle selectors.
    request.pwm_frequency[0] = 10000;
    request.pwm_duty[0] = 10000; // 200 MHz / 10 kHz = 20000 wrap.
    auto decoded = cnc::decode_spindle_command(request);
    CHECK(decoded.enabled && decoded.forward && near(decoded.pwm_fraction, 0.5));

    request.outputs[0] = 0x2u;
    decoded = cnc::decode_spindle_command(request);
    CHECK(!decoded.enabled && !decoded.forward);
    request.outputs[0] = 0x3u;
    request.enc_control = 0x1u;
    decoded = cnc::decode_spindle_command(request);
    CHECK(decoded.enabled && !decoded.forward && decoded.index_enable);

    struct PwmCase { std::uint32_t frequency; std::uint16_t wrap; std::uint32_t duty; };
    // The surprising narrowed wraps from 1908..3051 intentionally reproduce
    // pinned Stepper-Ninja integer arithmetic; they are not simulator mistakes.
    for (const auto sample : {
            PwmCase{1907, 65535, 32768}, PwmCase{1908, 39285, 19643},
            PwmCase{3000, 1130, 565}, PwmCase{3051, 16, 8},
            PwmCase{3052, 65530, 32765}, PwmCase{10000, 20000, 10000}}) {
        CHECK(cnc::decode_pwm_wrap(sample.frequency) == sample.wrap);
        CHECK(near(cnc::decode_pwm_fraction(sample.duty, sample.frequency),
                   static_cast<long double>(sample.duty) / sample.wrap));
        CHECK(cnc::decode_pwm_fraction(static_cast<std::uint32_t>(sample.wrap) + 1u,
                                       sample.frequency) == 1.0);
    }
    CHECK(cnc::decode_pwm_wrap(0) == 0 && cnc::decode_pwm_fraction(1, 0) == 0.0);
    CHECK(std::isfinite(cnc::decode_pwm_fraction(std::numeric_limits<std::uint32_t>::max(), 0)));
    CHECK(cnc::decode_pwm_wrap(std::numeric_limits<std::uint32_t>::max()) == 0);
    CHECK(cnc::decode_pwm_fraction(std::numeric_limits<std::uint32_t>::max(),
                                   std::numeric_limits<std::uint32_t>::max()) == 0.0);
    CHECK(cnc::decode_pwm_fraction(20000, 10000) == 1.0);
    CHECK(cnc::decode_pwm_fraction(20001, 10000) == 1.0);

    cnc::VirtualSpindle spindle;
    spindle.update(command(false, true, 0.5), 0);
    CHECK(!spindle.state().enabled && spindle.state().pwm_fraction == 0.5);
    CHECK(spindle.state().commanded_rpm == 0.0 && spindle.state().actual_rpm == 0.0);
    spindle.update(command(true, true, 0.0), 1000);
    CHECK(spindle.state().enabled && spindle.state().pwm_fraction == 0.0);
    CHECK(spindle.state().commanded_rpm == 0.0 && spindle.state().actual_rpm == 0.0);
    spindle.update(command(true, true, 0.5), 2000);
    CHECK(spindle.state().commanded_rpm == 12000.0);
    spindle.update(command(true, false, 1.0), 3000);
    CHECK(spindle.state().commanded_rpm == -24000.0);
}

void motion_and_fractional_counts() {
    cnc::VirtualSpindle spindle({60.0, 10});
    const auto forward = command(true, true, 1.0);
    spindle.update(forward, 0);
    spindle.update(forward, 500000);
    CHECK(near(spindle.state().angular_position_revolutions, 0.5L));
    CHECK(spindle.state().encoder_counter == 5);

    const auto reverse = command(true, false, 1.0);
    spindle.update(reverse, 500000); // New direction applies after this timestamp.
    spindle.update(reverse, 1000000);
    CHECK(near(spindle.state().angular_position_revolutions, 0.0L));
    CHECK(spindle.state().encoder_counter == 0);

    cnc::VirtualSpindle fractional({60.0, 10});
    fractional.update(forward, 0);
    for (std::uint64_t time = 40000; time <= 200000; time += 40000)
        fractional.update(forward, time);
    CHECK(fractional.state().encoder_counter == 2);
    CHECK(near(fractional.state().fractional_encoder_counts, 0.0L));
    CHECK(near(fractional.state().angular_position_revolutions, 0.2L));
}

void signed_counter_wrap() {
    cnc::VirtualSpindle forward({60.0, 1});
    const auto positive = command(true, true, 1.0);
    forward.update(positive, 0);
    constexpr std::uint64_t wrap_time = (std::uint64_t{1} << 31) * 1000000u;
    forward.update(positive, wrap_time);
    CHECK(forward.state().encoder_counter == std::numeric_limits<std::int32_t>::min());
    forward.update(positive, wrap_time + 1000000u);
    CHECK(forward.state().encoder_counter == std::numeric_limits<std::int32_t>::min() + 1);

    cnc::VirtualSpindle reverse({60.0, 1});
    const auto negative = command(true, false, 1.0);
    reverse.update(negative, 0);
    reverse.update(negative, wrap_time);
    CHECK(reverse.state().encoder_counter == std::numeric_limits<std::int32_t>::min());
    reverse.update(negative, wrap_time + 1000000u);
    CHECK(reverse.state().encoder_counter == std::numeric_limits<std::int32_t>::max());
}

void index_handling() {
    const auto armed_forward = command(true, true, 1.0, true);
    cnc::VirtualSpindle forward({60.0, 100});
    forward.update(armed_forward, 0);
    CHECK(forward.state().index_armed);
    forward.update(armed_forward, 500000);
    CHECK(forward.state().encoder_counter == 50 && !forward.state().index_event);
    forward.update(armed_forward, 1000000);
    CHECK(forward.state().encoder_counter == 0 && forward.state().index_event);
    CHECK(forward.state().index_armed);
    forward.update(armed_forward, 2000000);
    CHECK(forward.state().encoder_counter == 0 && forward.state().index_event);
    CHECK(forward.state().index_armed); // Held high re-enables without a low packet.
    forward.update(armed_forward, 2000000);
    CHECK(!forward.state().index_event); // Event is one response, not persistent state.

    forward.update(command(true, true, 1.0, false), 2500000);
    CHECK(forward.state().encoder_counter == 50 && !forward.state().index_event);
    CHECK(!forward.state().index_armed);
    forward.update(command(true, true, 1.0, false), 3500000);
    CHECK(forward.state().encoder_counter == 150 && !forward.state().index_event);

    const auto armed_reverse = command(true, false, 1.0, true);
    cnc::VirtualSpindle reverse({60.0, 100});
    reverse.update(armed_reverse, 0);
    reverse.update(armed_reverse, 1000000);
    CHECK(near(reverse.state().angular_position_revolutions, -1.0L));
    CHECK(reverse.state().encoder_counter == 0 && reverse.state().index_event);
    CHECK(reverse.state().index_armed);
    reverse.update(armed_reverse, 2000000);
    CHECK(reverse.state().encoder_counter == 0 && reverse.state().index_event);

    cnc::VirtualSpindle multiple({60.0, 100});
    multiple.update(armed_forward, 0);
    multiple.update(armed_forward, 3500000);
    CHECK(near(multiple.state().angular_position_revolutions, 3.5L));
    CHECK(multiple.state().encoder_counter == 50 && multiple.state().index_event);
    multiple.update(armed_forward, 4500000);
    CHECK(multiple.state().encoder_counter == 50 && multiple.state().index_event);

    cnc::VirtualSpindle reverse_multiple({60.0, 100});
    reverse_multiple.update(armed_reverse, 0);
    reverse_multiple.update(armed_reverse, 3500000);
    CHECK(reverse_multiple.state().encoder_counter == -50 && reverse_multiple.state().index_event);

    cnc::VirtualSpindle unarmed({60.0, 100});
    const auto plain = command(true, true, 1.0);
    unarmed.update(plain, 0);
    unarmed.update(plain, 1000000);
    CHECK(unarmed.state().encoder_counter == 100 && !unarmed.state().index_event);

    // A newly received high level cannot affect the interval that just elapsed.
    cnc::VirtualSpindle ordering({60.0, 100});
    ordering.update(plain, 0);
    ordering.update(armed_forward, 1000000);
    CHECK(ordering.state().encoder_counter == 100 && !ordering.state().index_event);
    ordering.update(armed_forward, 2000000);
    CHECK(ordering.state().encoder_counter == 0 && ordering.state().index_event);
}

void accepted_packet_pipeline() {
    static_assert(cnc::request_size == 37);
    static_assert(cnc::response_size == 61);
    cnc::StepperNinjaProtocol protocol;
    cnc::VirtualSpindle spindle({60.0, 100});
    cnc::Request request{};
    request.pio_timing = 235;
    request.outputs[0] = 0x1u;
    request.pwm_frequency[0] = 10000;
    request.pwm_duty[0] = 20000;
    request.stepgen_command[0] = 0x80000002u; // Three positive X steps.

    auto accepted = protocol.accept(bytes(request), 0);
    CHECK(accepted && accepted->id == 0 && accepted->spindle_command.enabled);
    spindle.update(accepted->spindle_command, accepted->elapsed_us);
    auto feedback = spindle.feedback();
    auto response = cnc::StepperNinjaProtocol::make_response(*accepted, {}, &feedback);
    CHECK(response.packet_id == 0 && tx_checksum_ok(&response));
    CHECK(response.encoder_counter[0] == 0 && response.encoder_velocity[0] == 0);
    CHECK(response.encoder_counter[1] == 0 && response.encoder_counter[2] == 0);
    CHECK(response.encoder_velocity[1] == 0 && response.encoder_velocity[2] == 0);
    CHECK(protocol.machine().positions()[0] == 3);

    const auto before = spindle.state();
    auto invalid = bytes(request);
    invalid.back() ^= 1u;
    CHECK(!protocol.accept(invalid, 1000000));
    CHECK(spindle.state().angular_position_revolutions == before.angular_position_revolutions);
    CHECK(spindle.state().encoder_counter == before.encoder_counter);
    CHECK(spindle.state().actual_rpm == before.actual_rpm);
    CHECK(protocol.machine().positions()[0] == 3);

    request.packet_id = 1;
    request.outputs[0] = 0;
    request.stepgen_command[0] = 0;
    accepted = protocol.accept(bytes(request), (std::uint64_t{1} << 32) + 17);
    CHECK(accepted && accepted->id == 1 && protocol.stats().packet_id_gaps == 0);
    spindle.update(accepted->spindle_command, accepted->elapsed_us);
    feedback = spindle.feedback();
    response = cnc::StepperNinjaProtocol::make_response(*accepted, {}, &feedback);
    CHECK(response.packet_id == 1 && tx_checksum_ok(&response));
    CHECK(response.encoder_timestamp[0] == 17);
    CHECK(response.encoder_timestamp[1] == 17 && response.encoder_timestamp[2] == 17);
    CHECK(response.encoder_velocity[0] == 0);

    cnc::VirtualSpindle indexed({60.0, 100});
    indexed.update(command(true, true, 1.0, true), 0);
    indexed.update(command(true, true, 1.0, true), 3500000);
    feedback = indexed.feedback();
    response = cnc::StepperNinjaProtocol::make_response(*accepted, {}, &feedback);
    CHECK(response.encoder_counter[0] == 50);
    CHECK(response.interrupt_data == 0x1u && tx_checksum_ok(&response));
    indexed.update(command(true, true, 1.0, true), 3500000);
    feedback = indexed.feedback();
    response = cnc::StepperNinjaProtocol::make_response(*accepted, {}, &feedback);
    CHECK(response.interrupt_data == 0 && tx_checksum_ok(&response));
}
} // namespace

int main() {
    try {
        decoding_and_speed();
        motion_and_fractional_counts();
        signed_counter_wrap();
        index_handling();
        accepted_packet_pipeline();
        std::cout << "PASS: spindle decode, RPM, integration, counter wrap, index, accepted-packet response\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
