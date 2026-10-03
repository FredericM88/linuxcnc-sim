#include "config/SimulatorConfig.hpp"
#include "console/IOCommands.hpp"
#include "console/WorkpieceCommands.hpp"
#include <arpa/inet.h>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace cnc {
namespace {
unsigned number(std::string_view value) {
    unsigned result{};
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (error != std::errc{} || end != value.data() + value.size()) {
        throw std::invalid_argument("invalid nonnegative integer: " + std::string(value));
    }
    return result;
}

std::array<std::string, cnc::axis_count> split_four(std::string_view text) {
    std::array<std::string, cnc::axis_count> result;
    for (std::size_t i = 0; i < result.size(); ++i) {
        const auto end = text.find(',');
        if ((i < result.size() - 1 && end == std::string_view::npos) ||
            (i == result.size() - 1 && end != std::string_view::npos)) {
            throw std::invalid_argument("expected exactly four comma-separated values");
        }
        result[i] = text.substr(0, end);
        if (result[i].empty()) throw std::invalid_argument("empty value in list");
        if (end != std::string_view::npos) text.remove_prefix(end + 1);
    }
    return result;
}

double real_number(std::string_view text) {
    std::size_t consumed{};
    double value{};
    try { value = std::stod(std::string(text), &consumed); }
    catch (const std::exception&) { throw std::invalid_argument("expected a finite number: " + std::string(text)); }
    if (consumed != text.size() || !std::isfinite(value))
        throw std::invalid_argument("expected a finite number: " + std::string(text));
    return value;
}

std::string trim(std::string_view value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    return std::string(value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1));
}
glm::dvec3 triple(std::string_view text, bool ini_whitespace = false) {
    glm::dvec3 result;
    for (int i = 0; i < 3; ++i) {
        const auto end = text.find(',');
        if ((i < 2 && end == std::string_view::npos) || (i == 2 && end != std::string_view::npos))
            throw std::invalid_argument("expected three comma-separated values");
        const auto component = text.substr(0, end);
        result[i] = ini_whitespace ? real_number(trim(component)) : real_number(component);
        if (end != std::string_view::npos) text.remove_prefix(end + 1);
    }
    return result;
}

bool boolean(const std::string& value) {
    if (value == "true") return true;
    if (value == "false") return false;
    throw std::invalid_argument("expected true or false");
}
struct Entry { std::string value, location; };
using Entries = std::map<std::string, Entry>;
const std::map<std::string, std::set<std::string>> schema{
    {"SIMULATOR", {"UNITS", "STATS_INTERVAL_MS"}},
    {"NETWORK", {"BIND_ADDRESS", "PORT"}},
    {"AXIS_X", {"TYPE", "STEPS_PER_UNIT"}}, {"AXIS_Y", {"TYPE", "STEPS_PER_UNIT"}},
    {"AXIS_Z", {"TYPE", "STEPS_PER_UNIT"}}, {"AXIS_A", {"TYPE", "STEPS_PER_UNIT"}},
    {"MATERIAL", {"ENABLED", "BATCH_MS", "WORKERS"}}, {"MESH", {"WORKERS"}},
    {"RENDER", {"ENABLED"}}, {"WORKPIECE", {"SIZE", "POSITION", "ORIGIN", "VOXEL_SIZE"}},
    {"TOOL", {"TYPE", "DIAMETER", "CUTTING_LENGTH"}}, {"VIRTUAL_IO", {"CONFIG"}}
};
Entries read_ini(const std::string& filename) {
    std::ifstream file(filename);
    if (!file) throw std::invalid_argument(filename + ": cannot read simulator INI");
    Entries entries;
    std::set<std::string> sections;
    std::string section;
    unsigned line_number = 0;
    for (std::string raw; std::getline(file, raw);) {
        ++line_number;
        const auto line = trim(raw);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        const auto location = filename + ":" + std::to_string(line_number) + " [" + section + "]";
        if (line[0] == '[') {
            if (line.back() != ']') throw std::invalid_argument(location + ": malformed section");
            section = trim(std::string_view(line).substr(1, line.size() - 2));
            const auto where = filename + ":" + std::to_string(line_number) + " [" + section + "]";
            if (!schema.contains(section)) throw std::invalid_argument(where + ": unknown section");
            if (!sections.insert(section).second) throw std::invalid_argument(where + ": duplicate section");
            continue;
        }
        const auto equal = line.find('=');
        if (equal == std::string::npos) throw std::invalid_argument(location + ": expected KEY = VALUE");
        const auto key = trim(std::string_view(line).substr(0, equal));
        const auto where = location + " " + key;
        if (!schema.contains(section) || !schema.at(section).contains(key))
            throw std::invalid_argument(where + ": unknown key");
        if (!entries.emplace(section + "." + key, Entry{trim(std::string_view(line).substr(equal + 1)), where}).second)
            throw std::invalid_argument(where + ": duplicate key");
    }
    if (file.bad()) throw std::invalid_argument(filename + ": INI read failed");
    return entries;
}
} // namespace

void simulator_usage() {
    std::cout << "Usage: cnc-sim [options]\n"
        "  --config FILE               Load simulator INI before CLI overrides\n"
        "  --print-config              Validate, print effective startup configuration, exit\n"
        "  --bind IPv4                 Default: 192.168.50.2\n"
        "  --port N                    Default: 8888; 0 selects a port for tests\n"
        "  --steps-per-unit X,Y,Z,A     Default configuration: 1000,1000,1000,1000\n"
        "  --units X,Y,Z,A              Display labels; default: mm,mm,mm,unit\n"
        "  --io-config FILE            Sensor commands loaded before UDP starts (step coordinates)\n"
        "  --stats                     Legacy scrolling statistics every second\n"
        "  --stats-ms N                Statistics interval >= 100 ms\n"
        "  --no-stats                  Headless; commands and final statistics\n"
        "  --verbose                   Show latest packet counters every refresh\n"
        "  --material-batch-ms N       Event-time window: default 20, 1..1000; 0=segmentwise\n"
        "  --material-workers N        Material owner count: must be 1\n"
        "  --mesh-workers N            0=auto (up to 4, reserve 2 CPUs); explicit 1..32\n"
        "  --render                    Open optional OpenGL 3.3 window\n"
        "  --voxel-size MM             Positive voxel edge; default 0.1\n"
        "  --stock-size X,Y,Z          Stock dimensions in mm; default 50,50,10\n"
        "  --stock-origin X,Y,Z        Legacy minimum-corner translation; default 0,0,-10\n"
        "  --stock-rotation X,Y,Z      Euler degrees; Rz * Ry * Rx; default 0,0,0\n"
        "  --help                      Show this help\n"
        "Default: fixed interactive terminal (5 Hz); type help for commands.\nPositions start at zero. Scales and units are local configuration, not wire data.\n";
}

Scales SimulatorConfig::scales() const {
    Scales result;
    for (std::size_t i = 0; i < axis_count; ++i) result[i] = axes[i].steps_per_unit;
    return result;
}

SimulatorConfig load_simulator_config(int argc, const char* const* argv, bool render_available) {
    SimulatorConfig c;
    // Tokenize first so --config has the same precedence anywhere on the command line.
    const std::set<std::string> switches{"--stats", "--no-stats", "--verbose", "--render", "--print-config"};
    const std::set<std::string> values{"--config", "--bind", "--port", "--steps-per-unit", "--units",
        "--io-config", "--stats-ms", "--material-workers", "--material-batch-ms", "--mesh-workers",
        "--voxel-size", "--stock-size", "--stock-origin", "--stock-rotation"};
    std::vector<std::pair<std::string, std::string>> cli;
    std::string filename;
    bool has_config = false;
    for (int i = 1; i < argc; ++i) {
        const std::string flag(argv[i]);
        if (switches.contains(flag)) cli.emplace_back(flag, "");
        else if (values.contains(flag)) {
            if (++i == argc) throw std::invalid_argument("missing value for " + flag);
            if (flag == "--config") {
                if (has_config) throw std::invalid_argument("duplicate --config; specify one simulator INI");
                filename = argv[i]; has_config = true;
            } else cli.emplace_back(flag, argv[i]);
        } else throw std::invalid_argument("unknown option: " + flag);
    }
    const auto ini = has_config ? read_ini(filename) : Entries{};
    std::map<std::string, std::string> locations;
    auto checked = [&](const std::string& keys, auto action) {
        try { action(); }
        catch (const std::exception& error) {
            std::string where;
            std::istringstream names(keys);
            for (std::string key; names >> key;)
                if (locations.contains(key)) where += (where.empty() ? "" : "; ") + locations.at(key);
            throw std::invalid_argument((where.empty() ? keys : where) + ": " + error.what());
        }
    };
    auto apply = [&](const std::string& key, auto action) {
        if (const auto it = ini.find(key); it != ini.end()) {
            locations[key] = it->second.location;
            checked(key, [&] { action(it->second.value); });
        }
    };
    auto port = [&](const std::string& text) {
        const auto n = number(text);
        if (n > 65535) throw std::invalid_argument("port must be in 0..65535");
        c.network.port = static_cast<std::uint16_t>(n);
    };
    apply("SIMULATOR.UNITS", [&](const auto& v) {
        if (v != "mm") throw std::invalid_argument("supported linear units: mm (no automatic conversion)");
        c.linear_units = v;
    });
    apply("SIMULATOR.STATS_INTERVAL_MS", [&](const auto& v) {
        c.stats_ms = number(v); c.interactive = c.stats_ms == 0;
    });
    apply("NETWORK.BIND_ADDRESS", [&](const auto& v) { c.network.bind_address = v; });
    apply("NETWORK.PORT", port);
    for (std::size_t i = 0; i < axis_count; ++i) {
        const auto section = std::string("AXIS_") + "XYZA"[i];
        apply(section + ".TYPE", [&](const auto& v) {
            if (v == "linear") c.axes[i].type = AxisType::Linear;
            else if (v == "rotary") c.axes[i].type = AxisType::Rotary;
            else throw std::invalid_argument("axis TYPE must be linear or rotary");
            c.units[i] = c.axes[i].type == AxisType::Linear ? c.linear_units : "deg";
        });
        apply(section + ".STEPS_PER_UNIT", [&](const auto& v) {
            c.axes[i].steps_per_unit = real_number(v);
            if (c.axes[i].steps_per_unit <= 0) throw std::invalid_argument("STEPS_PER_UNIT must be > 0");
        });
    }
    apply("MATERIAL.ENABLED", [&](const auto& v) { c.material_enabled = boolean(v); });
    apply("MATERIAL.BATCH_MS", [&](const auto& v) { c.workers.material_batch_ms = number(v); });
    apply("MATERIAL.WORKERS", [&](const auto& v) { c.workers.material_workers = number(v); });
    apply("MESH.WORKERS", [&](const auto& v) { c.workers.mesh_workers = number(v); });
    apply("RENDER.ENABLED", [&](const auto& v) { c.render = boolean(v); });
    bool legacy_placement = true;
    for (const auto& [key, entry] : ini) {
        (void)entry;
        if (key.starts_with("WORKPIECE.")) legacy_placement = false;
    }
    apply("WORKPIECE.SIZE", [&](const auto& v) { c.workpiece.size_mm = triple(v, true); });
    apply("WORKPIECE.POSITION", [&](const auto& v) { c.workpiece.position_machine_mm = triple(v, true); });
    apply("WORKPIECE.VOXEL_SIZE", [&](const auto& v) { c.workpiece.voxel_size_mm = real_number(v); });
    apply("WORKPIECE.ORIGIN", [&](const auto& v) {
        // Use the existing console's symbolic/numeric semantics, after size is known.
        std::string words;
        std::string_view rest(v);
        for (int i = 0; i < 3; ++i) {
            const auto end = rest.find(',');
            if ((i < 2 && end == std::string_view::npos) || (i == 2 && end != std::string_view::npos))
                throw std::invalid_argument("expected three comma-separated origin values");
            const auto token = trim(rest.substr(0, end));
            if (token.empty() || token.find_first_of(" \t\r\n") != std::string::npos)
                throw std::invalid_argument("expected min, center, max or a millimetre offset per axis");
            words += " " + token;
            if (end != std::string_view::npos) rest.remove_prefix(end + 1);
        }
        c.workpiece = parse_workpiece_command("workpiece origin" + words, c.workpiece).config;
    });
    apply("TOOL.TYPE", [&](const auto& v) {
        if (v != "flat-end") throw std::invalid_argument("supported tool TYPE: flat-end");
    });
    apply("TOOL.DIAMETER", [&](const auto& v) { c.tool.diameter_mm = real_number(v); });
    apply("TOOL.CUTTING_LENGTH", [&](const auto& v) { c.tool.length_mm = real_number(v); });
    apply("VIRTUAL_IO.CONFIG", [&](const auto& v) {
        if (v.empty()) throw std::invalid_argument("expected an I/O config filename");
        c.io.files.push_back((std::filesystem::absolute(filename).parent_path() / v).lexically_normal().string());
    });
    glm::dvec3 legacy_min = SceneConfig{}.workpiece.translation;
    bool cli_io = false;
    for (const auto& [flag, v] : cli) {
        auto set = [&](const std::string& key, auto action) {
            locations[key] = "CLI " + flag;
            checked(key, action);
        };
        if (flag == "--bind") set("NETWORK.BIND_ADDRESS", [&] { c.network.bind_address = v; });
        else if (flag == "--port") set("NETWORK.PORT", [&] { port(v); });
        else if (flag == "--steps-per-unit") set("AXES", [&] {
            const auto parts = split_four(v);
            for (std::size_t i = 0; i < axis_count; ++i) {
                c.axes[i].steps_per_unit = real_number(parts[i]);
                if (c.axes[i].steps_per_unit == 0) throw std::invalid_argument("steps-per-unit must be finite and nonzero");
            }
        });
        else if (flag == "--units") set("LABELS", [&] { c.units = split_four(v); });
        else if (flag == "--io-config") set("VIRTUAL_IO.CONFIG", [&] {
            if (!cli_io) { c.io.files.clear(); cli_io = true; }
            c.io.files.push_back(v);
        });
        else if (flag == "--stats-ms") set("SIMULATOR.STATS_INTERVAL_MS", [&] {
            c.stats_ms = number(v); c.interactive = false;
            if (c.stats_ms < 100 || c.stats_ms > 3600000) throw std::invalid_argument("stats-ms must be in 100..3600000");
        });
        else if (flag == "--stats" || flag == "--no-stats") set("SIMULATOR.STATS_INTERVAL_MS", [&] {
            c.stats_ms = flag == "--stats" ? 1000 : 0; c.interactive = false;
        });
        else if (flag == "--verbose") c.verbose = true;
        else if (flag == "--render") set("RENDER.ENABLED", [&] { c.render = true; });
        else if (flag == "--print-config") c.print_config = true;
        else if (flag == "--material-workers") set("MATERIAL.WORKERS", [&] { c.workers.material_workers = number(v); });
        else if (flag == "--material-batch-ms") set("MATERIAL.BATCH_MS", [&] { c.workers.material_batch_ms = number(v); });
        else if (flag == "--mesh-workers") set("MESH.WORKERS", [&] { c.workers.mesh_workers = number(v); });
        else if (flag == "--voxel-size") set("WORKPIECE.VOXEL_SIZE", [&] { c.workpiece.voxel_size_mm = real_number(v); });
        else if (flag == "--stock-size") set("WORKPIECE.SIZE", [&] { c.workpiece.size_mm = triple(v); });
        else if (flag == "--stock-origin") set("WORKPIECE.POSITION", [&] {
            legacy_min = triple(v); legacy_placement = true;
        });
        else if (flag == "--stock-rotation") set("WORKPIECE.ROTATION", [&] { c.stock_rotation_degrees = triple(v); });
    }
    checked("NETWORK.BIND_ADDRESS", [&] {
        in_addr address{};
        if (inet_pton(AF_INET, c.network.bind_address.c_str(), &address) != 1)
            throw std::invalid_argument("bind requires a numeric IPv4 address");
    });
    checked("SIMULATOR.STATS_INTERVAL_MS", [&] {
        if (c.stats_ms && (c.stats_ms < 100 || c.stats_ms > 3600000))
            throw std::invalid_argument("statistics interval must be 0 or 100..3600000 ms");
    });
    checked("LABELS", [&] {
        for (const auto& unit : c.units) for (unsigned char ch : unit)
            if (ch < 32 || ch == 127) throw std::invalid_argument("units must not contain control characters");
    });
    checked("MATERIAL.WORKERS MATERIAL.BATCH_MS MESH.WORKERS", [&] { c.workers = resolve_material_workers(c.workers); });
    checked("TOOL.DIAMETER TOOL.CUTTING_LENGTH", [&] { validate_tool(c.tool); });
    checked("WORKPIECE.SIZE WORKPIECE.POSITION WORKPIECE.ORIGIN WORKPIECE.VOXEL_SIZE WORKPIECE.ROTATION", [&] {
        SceneConfig scene;
        scene.stock_size_mm = c.workpiece.size_mm;
        scene.volume.voxel_size_mm = c.workpiece.voxel_size_mm;
        scene.workpiece.translation = legacy_placement ? legacy_min : c.workpiece.machine_min();
        const auto angles = glm::radians(c.stock_rotation_degrees);
        scene.workpiece.orientation = glm::angleAxis(angles.z, glm::dvec3(0,0,1)) *
            glm::angleAxis(angles.y, glm::dvec3(0,1,0)) * glm::angleAxis(angles.x, glm::dvec3(1,0,0));
        if (legacy_placement) c.initial_workpiece = workpiece_from_scene(scene);
        else {
            auto volume = build_workpiece(c.workpiece);
            if (scene.workpiece.orientation != glm::dquat(1,0,0,0))
                c.initial_workpiece = workpiece_from_scene(scene);
            else c.initial_workpiece = std::make_shared<const WorkpieceSnapshot>(WorkpieceSnapshot{c.workpiece, volume, 1, false});
        }
        c.workpiece = c.initial_workpiece->config;
    });
    checked("VIRTUAL_IO.CONFIG", [&] {
        for (const auto& file : c.io.files) {
            if (!std::filesystem::is_regular_file(file)) throw std::invalid_argument("I/O config is not a readable file: " + file);
            const auto commands = load_io_config(file);
            c.io.commands.insert(c.io.commands.end(), commands.begin(), commands.end());
        }
    });
    checked("RENDER.ENABLED LABELS", [&] {
        if (c.render) {
            if (!render_available) throw std::invalid_argument("--render unavailable: rebuild with -DCNC_SIM_RENDER=ON");
            for (std::size_t i = 0; i < 3; ++i)
                if (c.units[i] != "mm") throw std::invalid_argument("--render requires XYZ --units mm,mm,mm (scales in steps/mm)");
        }
    });
    return c;
}

std::string describe_config(const SimulatorConfig& c) {
    std::ostringstream out;
    out << std::boolalpha << std::setprecision(17);
    const auto vector = [&](const char* key, glm::dvec3 v) { out << key << " = " << v.x << ',' << v.y << ',' << v.z << '\n'; };
    out << "Effective startup configuration (defaults < INI < CLI; runtime commands may subsequently change state)\n"
        << "[SIMULATOR]\nUNITS = " << c.linear_units << "\nSTATS_INTERVAL_MS = " << c.stats_ms
        << "\nInteractive terminal = " << c.interactive << "\nVerbose = " << c.verbose
        << "\nDisplay labels = " << c.units[0] << ',' << c.units[1] << ',' << c.units[2] << ',' << c.units[3]
        << "\n[NETWORK]\nBIND_ADDRESS = " << c.network.bind_address << "\nPORT = " << c.network.port << '\n';
    for (std::size_t i = 0; i < axis_count; ++i)
        out << "[AXIS_" << "XYZA"[i] << "]\nTYPE = " << (c.axes[i].type == AxisType::Linear ? "linear" : "rotary")
            << "\nSTEPS_PER_UNIT = " << c.axes[i].steps_per_unit << " (steps/"
            << (c.axes[i].type == AxisType::Linear ? c.linear_units : "degree") << ")\n";
    out << "[MATERIAL]\nENABLED = " << c.material_enabled << "\nBATCH_MS = " << c.workers.material_batch_ms
        << "\nWORKERS = " << c.workers.material_workers << "\n[MESH]\nWORKERS = " << c.workers.mesh_workers
        << "\n[RENDER]\nENABLED = " << c.render << "\n[WORKPIECE]\n";
    vector("SIZE", c.workpiece.size_mm); vector("POSITION", c.workpiece.position_machine_mm);
    vector("ORIGIN", c.workpiece.origin_offset_mm);
    out << "VOXEL_SIZE = " << c.workpiece.voxel_size_mm << '\n';
    vector("Machine min (before legacy rotation)", c.workpiece.machine_min());
    vector("Machine max (before legacy rotation)", c.workpiece.machine_max());
    vector("Legacy rotation degrees", c.stock_rotation_degrees);
    out << "[TOOL]\nTYPE = flat-end\nDIAMETER = " << c.tool.diameter_mm << "\nCUTTING_LENGTH = " << c.tool.length_mm
        << "\n[VIRTUAL_IO]\n";
    if (c.io.files.empty()) out << "CONFIG = (none)\n";
    for (const auto& file : c.io.files) out << "CONFIG = " << file << '\n';
    return out.str();
}
} // namespace cnc
