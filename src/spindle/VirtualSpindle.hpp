#pragma once

#include <cstdint>

namespace cnc {
struct SpindleConfig {
    double max_rpm{24000.0};
    std::uint32_t encoder_counts_per_revolution{1024};
};

// Decoded hardware command. Direction output low is forward/positive; high is
// reverse/negative. PWM is a physical duty fraction, not a LinuxCNC S value.
struct SpindleCommand {
    bool enabled{};
    bool forward{true};
    std::uint32_t pwm_duty{};
    std::uint32_t pwm_frequency{};
    double pwm_fraction{};
    bool index_enable{};
};

struct SpindleFeedback {
    std::int32_t encoder_counter{};
    std::uint32_t encoder_timestamp{};
    bool index_event{};
};

struct SpindleState {
    bool enabled{};
    bool forward{true};
    double pwm_fraction{};
    double commanded_rpm{};
    double actual_rpm{};
    long double angular_position_revolutions{};
    long double fractional_encoder_counts{};
    std::int32_t encoder_counter{};
    bool index_armed{};
    bool index_event{};
    std::uint64_t last_update_us{};
};

class VirtualSpindle {
public:
    explicit VirtualSpindle(SpindleConfig config = {});

    // Each call represents one accepted command packet. Motion through now_us
    // uses the previous command; the new command and index request apply after.
    void update(const SpindleCommand& command, std::uint64_t now_us);
    const SpindleState& state() const { return state_; }
    SpindleFeedback feedback() const;

private:
    void advance(std::uint64_t now_us);
    void add_encoder_counts(long double counts);
    void reset_encoder_at_index();

    SpindleConfig config_;
    SpindleState state_;
    std::uint32_t encoder_counter_bits_{};
    bool have_timestamp_{};
};
} // namespace cnc
