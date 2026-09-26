#include "io/Sensors.hpp"
#include <limits>
#include <stdexcept>

namespace cnc {
void AxisSwitch::configure(SwitchConfig config) {
    if (config.axis >= 3 || config.input >= 128 || config.hysteresis < 0)
        throw std::invalid_argument("limit requires axis X/Y/Z, input 0..127, hysteresis >= 0 steps");
    if ((config.minimum && config.trigger > std::numeric_limits<std::int64_t>::max() - config.hysteresis) ||
        (!config.minimum && config.trigger < std::numeric_limits<std::int64_t>::min() + config.hysteresis))
        throw std::invalid_argument("limit release position overflows int64");
    release_ = config.minimum ? config.trigger + config.hysteresis : config.trigger - config.hysteresis;
    status_ = {config, true, false};
}
void AxisSwitch::update(const StepPositions& position) noexcept {
    if (!status_.enabled) return;
    const auto p = position[status_.config.axis];
    // Trigger takes precedence at zero hysteresis: equality stays ON, never flaps.
    if (status_.config.minimum) {
        if (p <= status_.config.trigger) status_.active = true;
        else if (p >= release_) status_.active = false;
    } else {
        if (p >= status_.config.trigger) status_.active = true;
        else if (p <= release_) status_.active = false;
    }
}
PlaneGeometry::PlaneGeometry(std::size_t axis, std::int64_t surface) : axis_(axis), surface_(surface) {
    if (axis >= 3) throw std::invalid_argument("probe axis must be X/Y/Z");
}
Contact PlaneGeometry::evaluate(const StepPositions& previous, const StepPositions& current) const noexcept {
    Contact contact;
    contact.touching = current[axis_] <= surface_;
    contact.entered = previous[axis_] > surface_ && contact.touching;
    if (contact.entered) {
        // Cast before subtraction to avoid int64 overflow across the full range.
        contact.fraction = (static_cast<long double>(previous[axis_]) - surface_) /
            (static_cast<long double>(previous[axis_]) - current[axis_]);
        for (std::size_t i = 0; i < 3; ++i)
            contact.point[i] = static_cast<long double>(previous[i]) + contact.fraction *
                (static_cast<long double>(current[i]) - previous[i]);
        contact.point[axis_] = static_cast<long double>(surface_);
    }
    return contact;
}
void Probe::configure_plane(std::size_t axis, std::int64_t surface, std::size_t input) {
    if (input >= 128) throw std::invalid_argument("probe input must be 0..127");
    auto geometry = std::make_unique<PlaneGeometry>(axis, surface);
    geometry_ = std::move(geometry);
    status_ = {};
    status_.enabled = true;
    status_.axis = axis;
    status_.surface = surface;
    status_.input = input;
}
void Probe::update(const StepPositions& previous, const StepPositions& current) noexcept {
    if (!geometry_) return;
    const auto contact = geometry_->evaluate(previous, current);
    status_.active = contact.touching;
    if (contact.entered) { status_.has_contact = true; status_.last_contact = contact; }
}
} // namespace cnc
