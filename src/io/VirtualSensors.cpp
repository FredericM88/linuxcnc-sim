#include "io/VirtualSensors.hpp"
#include <stdexcept>

namespace cnc {
void VirtualSensors::execute(const IOCommand& c, const StepPositions& current) {
    if (c.axis >= 3 || c.input >= 128) throw std::invalid_argument("invalid I/O axis or input");
    switch (c.action) {
    case IOAction::SetInput: io_.set_input(c.input, c.on); break;
    case IOAction::ClearManual: io_.clear_manual(); break;
    case IOAction::SetLimit:
        switches_[2 * c.axis + (c.minimum ? 0 : 1)].configure({c.axis, c.input, c.minimum, c.position, c.hysteresis});
        break;
    case IOAction::DisableLimit: switches_[2 * c.axis + (c.minimum ? 0 : 1)].disable(); break;
    case IOAction::SetPlane: probe_.configure_plane(c.axis, c.position, c.input); break;
    case IOAction::DisableProbe: probe_.disable(); break;
    default: return; // Read-only status commands never modify sensor history.
    }
    update(current, current);
}
void VirtualSensors::update(const StepPositions& previous, const StepPositions& current) noexcept {
    InputWords automatic{};
    auto set = [&](std::size_t input) { automatic[input / 32] |= std::uint32_t{1} << (input % 32); };
    for (auto& sensor : switches_) {
        sensor.update(current);
        const auto state = sensor.status();
        if (state.enabled && state.active) set(state.config.input);
    }
    probe_.update(previous, current);
    const auto probe = probe_.status();
    if (probe.enabled && probe.active) set(probe.input);
    io_.set_automatic(automatic);
}
IOStatus VirtualSensors::status() const noexcept {
    IOStatus result{io_.packed_inputs(), io_.manual_inputs(), io_.automatic_inputs(), {}, probe_.status()};
    for (std::size_t i = 0; i < switches_.size(); ++i) result.switches[i] = switches_[i].status();
    return result;
}
} // namespace cnc
