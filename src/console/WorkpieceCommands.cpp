#include "console/WorkpieceCommands.hpp"
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace cnc {
namespace {
double number(const std::string& token) {
    std::size_t used{};
    double value{};
    try { value = std::stod(token, &used); }
    catch (const std::exception&) { throw std::invalid_argument("expected a finite millimetre value: " + token); }
    if (used != token.size() || !std::isfinite(value))
        throw std::invalid_argument("expected a finite millimetre value: " + token);
    return value;
}
}
WorkpieceCommand parse_workpiece_command(const std::string& line, const WorkpieceConfig& current) {
    std::istringstream in(line);
    std::string first, action, token;
    in >> first >> action;
    if (first != "workpiece") throw std::invalid_argument(workpiece_help);
    WorkpieceCommand result{false, current};
    if (action == "show") result.show = true;
    else if (action == "reset") result.config = WorkpieceConfig{};
    else if (action == "size" || action == "position" || action == "origin" || action == "voxel") {
        const int count = action == "voxel" ? 1 : 3;
        for (int i = 0; i < count; ++i) {
            if (!(in >> token)) throw std::invalid_argument(workpiece_help);
            double value;
            if (action == "origin" && token == "min") value = 0;
            else if (action == "origin" && token == "center") value = current.size_mm[i] / 2;
            else if (action == "origin" && token == "max") value = current.size_mm[i];
            else value = number(token);
            if (action == "size") result.config.size_mm[i] = value;
            else if (action == "position") result.config.position_machine_mm[i] = value;
            else if (action == "origin") result.config.origin_offset_mm[i] = value;
            else result.config.voxel_size_mm = value;
        }
    } else throw std::invalid_argument(workpiece_help);
    if (in >> token) throw std::invalid_argument("unexpected workpiece argument: " + token);
    return result;
}
std::string describe_workpiece(const WorkpieceSnapshot& snapshot) {
    const auto& c = snapshot.config;
    std::ostringstream out;
    out << std::fixed << std::setprecision(3) << "Workpiece (revision " << snapshot.revision << ")\n";
    auto vector = [&](const char* label, glm::dvec3 v) {
        out << label << ": X " << v.x << " Y " << v.y << " Z " << v.z << " mm\n";
    };
    vector("Size", c.size_mm);
    vector("Position (G53)", c.position_machine_mm);
    vector("Origin offset from workpiece min", c.origin_offset_mm);
    out << "Machine bounds" << (snapshot.legacy_rotation ? " (before legacy rotation)" : "") << ":\n";
    for (int i = 0; i < 3; ++i)
        out << "  " << "XYZ"[i] << ' ' << c.machine_min()[i] << " .. " << c.machine_max()[i] << " mm\n";
    out << std::defaultfloat << std::setprecision(15) << "Voxel size: " << c.voxel_size_mm << " mm\n"
        << "Chunk size: 32 x 32 x 32";
    if (snapshot.legacy_rotation)
        out << "\nLegacy --stock-rotation active; next workpiece edit selects axis-aligned stock.";
    return out.str();
}
} // namespace cnc
