#pragma once

#include <array>
#include <cstdint>
#include "protocol/OriginalProtocol.hpp"

namespace cnc {
using StepPositions = std::array<std::int64_t, axis_count>;
using Scales = std::array<double, axis_count>;

class MachineState {
public:
    explicit MachineState(Scales scales = {default_scale, default_scale,
                                          default_scale, default_scale});
    // All axes commit together, or none do on signed overflow.
    bool integrate(const StepPositions& delta);
    const StepPositions& positions() const { return position_steps_; }
    const Scales& scales() const { return steps_per_unit_; }
    double position_units(std::size_t axis) const;
private:
    StepPositions position_steps_{}; // Startup reference; never received on the wire.
    Scales steps_per_unit_;
};
} // namespace cnc
