#include "config/SimulatorConfig.hpp"
#include <iostream>
#include <stdexcept>

int main() {
    try {
        // Decimal minima must not drift through position + origin - origin.
        // This compares the actual runtime transform, not rounded console output.
        for (const char* rotation : {"0,0,0", "15,25,35"}) {
            const char* args[]{"cnc-sim", "--stock-size", "10,20,30", "--stock-origin", "0.1,0.2,0.3",
                               "--stock-rotation", rotation};
            const auto c = cnc::load_simulator_config(static_cast<int>(std::size(args)), args, false);
            const auto& transform = c.initial_workpiece->volume->config().workpiece;
            if (transform.translation != glm::dvec3(.1,.2,.3))
                throw std::runtime_error("legacy minimum-corner translation changed");
            const auto old_scene = cnc::workpiece_from_scene(c.initial_workpiece->volume->config());
            if (old_scene->config.position_machine_mm != c.workpiece.position_machine_mm ||
                old_scene->config.origin_offset_mm != c.workpiece.origin_offset_mm)
                throw std::runtime_error("legacy reference-point semantics changed");
        }
        std::cout << "PASS: exact legacy runtime transforms\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n'; return 1;
    }
}
