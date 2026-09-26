#include "machine/MachineState.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace cnc {
MachineState::MachineState(Scales scales) : steps_per_unit_(scales) {
    for (const auto scale : scales) {
        if (!std::isfinite(scale) || scale == 0.0) {
            throw std::invalid_argument("steps-per-unit must be finite and nonzero");
        }
    }
}

bool MachineState::integrate(const StepPositions& delta) {
    for (std::size_t i = 0; i < axis_count; ++i) {
        if ((delta[i] > 0 && position_steps_[i] > std::numeric_limits<std::int64_t>::max() - delta[i]) ||
            (delta[i] < 0 && position_steps_[i] < std::numeric_limits<std::int64_t>::min() - delta[i])) {
            return false;
        }
    }
    for (std::size_t i = 0; i < axis_count; ++i) position_steps_[i] += delta[i];
    return true;
}

double MachineState::position_units(std::size_t axis) const {
    return static_cast<double>(position_steps_.at(axis)) / steps_per_unit_.at(axis);
}
} // namespace cnc
