#pragma once
#include <memory>
#include "machine/MachineState.hpp"
#include "io/VirtualIO.hpp"

namespace cnc {
struct SwitchConfig {
    std::size_t axis{}, input{};
    bool minimum = true;
    std::int64_t trigger{}, hysteresis{};
};
struct SwitchStatus {
    SwitchConfig config;
    bool enabled{}, active{};
};
class AxisSwitch {
public:
    void configure(SwitchConfig config);
    void disable() noexcept { status_.enabled = status_.active = false; }
    void update(const StepPositions& position) noexcept;
    SwitchStatus status() const noexcept { return status_; }
private:
    SwitchStatus status_;
    std::int64_t release_{};
};

struct Contact {
    bool touching{}, entered{};
    long double fraction{}; // Segment parameter, not an electrical timestamp.
    std::array<long double, 3> point{};
};
class ContactGeometry {
public:
    virtual ~ContactGeometry() = default;
    virtual Contact evaluate(const StepPositions& previous, const StepPositions& current) const noexcept = 0;
};
// Point probe against the closed half-space coordinate[axis] <= surface.
// Geometry coordinates are simulator steps, independent of LinuxCNC homing offsets.
class PlaneGeometry final : public ContactGeometry {
public:
    PlaneGeometry(std::size_t axis, std::int64_t surface);
    Contact evaluate(const StepPositions& previous, const StepPositions& current) const noexcept override;
private:
    std::size_t axis_;
    std::int64_t surface_;
};
struct ProbeStatus {
    bool enabled{}, active{}, has_contact{};
    std::size_t axis{}, input{};
    std::int64_t surface{};
    Contact last_contact;
};
class Probe {
public:
    void configure_plane(std::size_t axis, std::int64_t surface, std::size_t input);
    void disable() noexcept { geometry_.reset(); status_ = {}; }
    void update(const StepPositions& previous, const StepPositions& current) noexcept;
    ProbeStatus status() const noexcept { return status_; }
private:
    std::unique_ptr<const ContactGeometry> geometry_;
    ProbeStatus status_;
};
} // namespace cnc
