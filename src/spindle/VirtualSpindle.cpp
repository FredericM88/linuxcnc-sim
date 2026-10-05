#include "spindle/VirtualSpindle.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace cnc {
namespace {
constexpr long double counter_modulus = 4294967296.0L;
}

VirtualSpindle::VirtualSpindle(SpindleConfig config) : config_(config) {
    if (!std::isfinite(config_.max_rpm) || config_.max_rpm <= 0.0)
        throw std::invalid_argument("spindle MAX_RPM must be finite and > 0");
    if (config_.encoder_counts_per_revolution == 0)
        throw std::invalid_argument("spindle ENCODER_COUNTS_PER_REV must be > 0");
}

void VirtualSpindle::add_encoder_counts(long double counts) {
    const long double total = state_.fractional_encoder_counts + counts;
    const long double nearest = std::round(total);
    const long double rounding_tolerance = 64.0L * std::numeric_limits<long double>::epsilon() *
        std::max(1.0L, std::abs(total));
    const long double whole = std::abs(total - nearest) <= rounding_tolerance
        ? nearest : std::trunc(total);
    state_.fractional_encoder_counts = total - whole;

    long double modulo = std::fmod(whole, counter_modulus);
    if (modulo < 0.0L) modulo += counter_modulus;
    encoder_counter_bits_ += static_cast<std::uint32_t>(modulo);
    state_.encoder_counter = std::bit_cast<std::int32_t>(encoder_counter_bits_);
}

void VirtualSpindle::reset_encoder_at_index() {
    encoder_counter_bits_ = 0;
    state_.encoder_counter = 0;
    state_.fractional_encoder_counts = 0.0L;
    state_.index_event = true;
}

void VirtualSpindle::advance(std::uint64_t now_us) {
    if (!have_timestamp_) {
        have_timestamp_ = true;
        state_.last_update_us = now_us;
        return;
    }
    if (now_us <= state_.last_update_us) {
        state_.last_update_us = now_us;
        return;
    }

    const auto elapsed_us = now_us - state_.last_update_us;
    state_.last_update_us = now_us;
    const long double delta_revolutions = static_cast<long double>(state_.actual_rpm) *
        static_cast<long double>(elapsed_us) / 60000000.0L;
    const long double start = state_.angular_position_revolutions;
    const long double finish = start + delta_revolutions;

    bool crossing = false;
    long double last_index_position{};
    if (state_.index_armed && delta_revolutions > 0.0L) {
        const long double first_index_position = std::floor(start) + 1.0L;
        crossing = finish >= first_index_position;
        last_index_position = std::floor(finish);
    } else if (state_.index_armed && delta_revolutions < 0.0L) {
        const long double first_index_position = std::ceil(start) - 1.0L;
        crossing = finish <= first_index_position;
        last_index_position = std::ceil(finish);
    }

    const long double counts_per_revolution = config_.encoder_counts_per_revolution;
    if (crossing) {
        // The pinned firmware disables an individual index IRQ in its callback,
        // then immediately re-enables it while the command level remains high.
        // Resetting at the last crossing is equivalent to processing every
        // crossing in this interval; index_event coalesces them into one bit.
        reset_encoder_at_index();
        add_encoder_counts((finish - last_index_position) * counts_per_revolution);
    } else {
        add_encoder_counts(delta_revolutions * counts_per_revolution);
    }
    state_.angular_position_revolutions = finish;
}

void VirtualSpindle::update(const SpindleCommand& command, std::uint64_t now_us) {
    state_.index_event = false;
    advance(now_us);

    state_.enabled = command.enabled;
    state_.forward = command.forward;
    state_.pwm_fraction = std::isfinite(command.pwm_fraction)
        ? std::clamp(command.pwm_fraction, 0.0, 1.0) : 0.0;
    const double magnitude = state_.enabled ? state_.pwm_fraction * config_.max_rpm : 0.0;
    state_.commanded_rpm = magnitude == 0.0 ? 0.0 : state_.forward ? magnitude : -magnitude;
    // B2 has no acceleration, load, or slip model, but retains the distinction.
    state_.actual_rpm = state_.commanded_rpm;

    // This newly received level applies only to the next elapsed interval. A
    // high level continuously enables index detection; no low edge is required
    // before firmware can re-enable detection after an index callback.
    state_.index_armed = command.index_enable;
}

SpindleFeedback VirtualSpindle::feedback() const {
    return {state_.encoder_counter, static_cast<std::uint32_t>(state_.last_update_us), state_.index_event};
}
} // namespace cnc
