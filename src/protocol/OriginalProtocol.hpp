#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>

// The original C definitions and implementation remain unchanged in third_party.
extern "C" {
#include <transmission.h>
}

namespace cnc {
using Request = transmission_pc_pico_t;
using Response = transmission_pico_pc_t;
inline constexpr std::size_t axis_count = stepgens;
inline constexpr std::size_t encoder_count = encoders;
inline constexpr std::size_t request_size = sizeof(Request);
inline constexpr std::size_t response_size = sizeof(Response);
inline constexpr double default_scale = default_step_scale;
inline constexpr std::uint16_t default_port = DEFAULT_PORT;

static_assert(std::endian::native == std::endian::little,
              "The original native-memory wire format requires a little-endian host");
static_assert(axis_count == 4 && encoder_count == 3);
static_assert(breakout_board == 0 && use_timer_interrupt == 0 && raspberry_pi_spi == 0);
static_assert(pwm_count == 1 && use_pwm == 0);
static_assert(request_size == 37 && response_size == 61);
static_assert(alignof(Request) == 1 && alignof(Response) == 1);
static_assert(offsetof(Request, stepgen_command) == 0);
static_assert(offsetof(Request, outputs) == 16);
static_assert(offsetof(Request, pwm_duty) == 24);
static_assert(offsetof(Request, pwm_frequency) == 28);
static_assert(offsetof(Request, pio_timing) == 32);
static_assert(offsetof(Request, enc_control) == 34);
static_assert(offsetof(Request, packet_id) == 35);
static_assert(offsetof(Request, checksum) == 36);
static_assert(offsetof(Response, encoder_counter) == 0);
static_assert(offsetof(Response, encoder_velocity) == 12);
static_assert(offsetof(Response, encoder_timestamp) == 24);
static_assert(offsetof(Response, interrupt_data) == 36);
static_assert(offsetof(Response, inputs) == 37);
static_assert(offsetof(Response, jitter) == 53);
static_assert(offsetof(Response, step_ring_fill) == 57);
static_assert(offsetof(Response, step_ring_status) == 58);
static_assert(offsetof(Response, packet_id) == 59);
static_assert(offsetof(Response, checksum) == 60);
} // namespace cnc

// Generic original preprocessor names must not leak into C++ standard headers.
#undef low
#undef high
#undef version
