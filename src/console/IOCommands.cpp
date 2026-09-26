#include "console/IOCommands.hpp"
#include <bit>
#include <charconv>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace cnc {
namespace {
constexpr char axes[] = "XYZ";
std::int64_t integer(const std::string& text) {
    std::int64_t value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size())
        throw std::invalid_argument("expected an int64 step value: " + text);
    return value;
}
std::size_t index(const std::string& text) {
    const auto value = integer(text);
    if (value < 0 || value >= 128) throw std::invalid_argument("input index must be 0..127");
    return static_cast<std::size_t>(value);
}
std::size_t axis(const std::string& text) {
    if (text == "X" || text == "x") return 0;
    if (text == "Y" || text == "y") return 1;
    if (text == "Z" || text == "z") return 2;
    throw std::invalid_argument("axis must be X/Y/Z");
}
bool minimum(const std::string& text) {
    if (text == "min") return true;
    if (text == "max") return false;
    throw std::invalid_argument("limit side must be min/max");
}
std::string words(InputWords values) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (auto value : values) out << " " << std::setw(8) << value;
    return out.str();
}
bool bit(InputWords words, std::size_t n) { return (words[n / 32] & (std::uint32_t{1} << (n % 32))) != 0; }
const char* on(bool active) { return active ? "ON" : "OFF"; }
} // namespace
IOCommand parse_io_command(const std::string& line) {
    std::istringstream stream(line);
    std::vector<std::string> args;
    for (std::string word; stream >> word;) args.push_back(std::move(word));
    IOCommand c;
    if (args.size() == 2 && args[1] == "show") {
        if (args[0] == "input") c.action = IOAction::ShowInputs;
        else if (args[0] == "limits") c.action = IOAction::ShowLimits;
        else if (args[0] == "probe") c.action = IOAction::ShowProbe;
        else throw std::invalid_argument("unknown I/O command");
    } else if (args.size() == 2 && args[0] == "input" && args[1] == "clear") c.action = IOAction::ClearManual;
    else if (args.size() == 3 && args[0] == "input") {
        c.action = IOAction::SetInput; c.input = index(args[1]);
        if (args[2] != "on" && args[2] != "off") throw std::invalid_argument("input state must be on/off");
        c.on = args[2] == "on";
    } else if (args.size() == 7 && args[0] == "limits" && args[1] == "set") {
        c.action = IOAction::SetLimit; c.axis = axis(args[2]); c.minimum = minimum(args[3]);
        c.position = integer(args[4]); c.hysteresis = integer(args[5]); c.input = index(args[6]);
        AxisSwitch check; check.configure({c.axis, c.input, c.minimum, c.position, c.hysteresis});
    } else if (args.size() == 4 && args[0] == "limits" && args[1] == "off") {
        c.action = IOAction::DisableLimit; c.axis = axis(args[2]); c.minimum = minimum(args[3]);
    } else if (args.size() == 5 && args[0] == "probe" && args[1] == "plane") {
        c.action = IOAction::SetPlane; c.axis = axis(args[2]); c.position = integer(args[3]); c.input = index(args[4]);
    } else if (args.size() == 2 && args[0] == "probe" && args[1] == "off") c.action = IOAction::DisableProbe;
    else throw std::invalid_argument("I/O syntax: input n on/off; limits set X min steps hyst input; probe plane Z steps input");
    return c;
}
std::vector<IOCommand> load_io_config(const std::string& filename) {
    std::ifstream file(filename);
    if (!file) throw std::runtime_error("cannot read I/O config: " + filename);
    std::vector<IOCommand> commands;
    unsigned number = 0;
    for (std::string line; std::getline(file, line);) {
        ++number;
        line = line.substr(0, line.find('#'));
        if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
        try { commands.push_back(parse_io_command(line)); }
        catch (const std::exception& e) { throw std::runtime_error(filename + ":" + std::to_string(number) + ": " + e.what()); }
    }
    if (file.bad()) throw std::runtime_error("I/O config read failed: " + filename);
    return commands;
}
std::string compact_io(const IOStatus& status) {
    std::ostringstream out;
    out << "Virtual inputs:";
    for (std::size_t i = 0; i < 6; ++i)
        out << ' ' << axes[i / 2] << (i % 2 ? '+' : '-') << ':'
            << (status.switches[i].enabled ? on(status.switches[i].active) : "--");
    out << " Probe:" << (status.probe.enabled ? on(status.probe.active) : "--") << '\n';
    unsigned manual = 0;
    for (auto word : status.manual) manual += static_cast<unsigned>(std::popcount(word));
    out << "Wire: 22=" << bit(status.inputs,22) << " 26=" << bit(status.inputs,26)
        << " 27=" << bit(status.inputs,27) << " 28=" << bit(status.inputs,28) << " Manual overrides: " << manual << '\n';
    return out.str();
}
std::string describe_io(const IOCommand& c, const IOStatus& status) {
    std::ostringstream out;
    switch (c.action) {
    case IOAction::ShowInputs:
        out << "Inputs [0..3] hex:" << words(status.inputs) << "\nManual [0..3] hex:" << words(status.manual)
            << "\nAuto   [0..3] hex:" << words(status.automatic)
            << "\nHAL exports only wire bits 22,26,27,28 (gp22,gp26,gp27,gp28).";
        break;
    case IOAction::SetInput:
        out << "Input " << c.input << " manual=" << on(c.on) << " effective=" << on(bit(status.inputs,c.input));
        if (c.input != 22 && c.input != 26 && c.input != 27 && c.input != 28) out << " (wire only; no HAL pin)";
        break;
    case IOAction::ShowLimits:
        for (std::size_t i = 0; i < 6; ++i) {
            const auto& s = status.switches[i];
            out << (i ? "\n" : "") << axes[i / 2] << (i % 2 ? "-Max: " : "-Min: ");
            if (!s.enabled) out << "disabled";
            else out << on(s.active) << " trigger=" << s.config.trigger << " hyst=" << s.config.hysteresis << " steps input=" << s.config.input;
        }
        break;
    case IOAction::ShowProbe:
        if (!status.probe.enabled) out << "Probe disabled";
        else {
            const auto& p = status.probe;
            out << "Probe " << on(p.active) << ": point in half-space " << axes[p.axis] << " <= " << p.surface << " steps; input=" << p.input;
            if (p.has_contact) out << "\nLast entry fraction=" << p.last_contact.fraction << " contact steps="
                << p.last_contact.point[0] << ',' << p.last_contact.point[1] << ',' << p.last_contact.point[2];
        }
        break;
    case IOAction::ClearManual: out << "All manual overrides cleared; automatic inputs remain."; break;
    default: out << "Sensor configuration updated (step coordinates; machine position unchanged)."; break;
    }
    return out.str();
}
} // namespace cnc
