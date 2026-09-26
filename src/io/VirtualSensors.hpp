#pragma once
#include "io/Sensors.hpp"

namespace cnc {
enum class IOAction { ShowInputs, SetInput, ClearManual, ShowLimits, SetLimit, DisableLimit, ShowProbe, SetPlane, DisableProbe };
struct IOCommand {
    IOAction action = IOAction::ShowInputs;
    std::size_t axis{}, input{};
    bool minimum = true, on = false;
    std::int64_t position{}, hysteresis{};
};
struct IOStatus {
    InputWords inputs{}, manual{}, automatic{};
    std::array<SwitchStatus, 6> switches{};
    ProbeStatus probe;
};
// Synchronous sensor coordinator, owned only by Simulation's UDP thread.
class VirtualSensors {
public:
    void execute(const IOCommand& command, const StepPositions& current);
    void update(const StepPositions& previous, const StepPositions& current) noexcept;
    IOStatus status() const noexcept;
    InputWords inputs() const noexcept { return io_.packed_inputs(); }
private:
    VirtualIO io_;
    std::array<AxisSwitch, 6> switches_;
    Probe probe_;
};
} // namespace cnc
