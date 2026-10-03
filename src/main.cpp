#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <future>
#include <sstream>
#include "config/SimulatorConfig.hpp"
#include "console/MaterialCommands.hpp"
#include "console/TerminalUI.hpp"
#include "console/IOCommands.hpp"
#include "console/WorkpieceCommands.hpp"
#include "simulation/Simulation.hpp"
#ifdef CNC_SIM_RENDER
#include "render/Renderer.hpp"
#endif

namespace {
static_assert(std::atomic<bool>::is_always_lock_free);
std::atomic<bool> stopping{false};
extern "C" void stop_handler(int) { stopping = 1; }

std::string statistics(const cnc::SimulationStatus& state, const cnc::SimulatorConfig& options, bool compact) {
    const auto& s = state.packets;
    std::ostringstream out;
    if (compact) {
        out << "CNC Simulator - Virtual Stepper-Ninja\n"
            << "Connection: " << (state.peer.empty() ? "WAITING" :
                state.now_us - state.last_valid_us < 1000000 ? "CONNECTED " : "STALE ") << state.peer << '\n'
            << "RX: " << s.received_packets << "  accepted: " << s.accepted_packets << "  TX: " << state.sent << '\n'
            << "Invalid: " << s.invalid_packets << "  send errors: " << state.send_errors << '\n'
            << "Length errors: " << s.length_errors << "  checksum errors: " << s.checksum_errors << '\n'
            << "Timing errors: " << s.timing_errors << "  position overflows: " << s.position_overflows << '\n'
            << "Packet-ID gaps: " << s.packet_id_gaps << "\nMachine position\n";
    } else {
        out << "Packets RX: " << s.received_packets << "  accepted: " << s.accepted_packets
            << "  invalid: " << s.invalid_packets << "  TX: " << state.sent
            << "  send errors: " << state.send_errors << '\n'
            << "Length errors: " << s.length_errors << "  checksum errors: " << s.checksum_errors
            << "  timing errors: " << s.timing_errors << "  position overflows: " << s.position_overflows
            << "  Packet-ID gaps: " << s.packet_id_gaps << '\n';
    }
    constexpr std::array labels{"X", "Y", "Z", "A"};
    for (std::size_t i = 0; i < cnc::axis_count; ++i)
        out << labels[i] << "  " << state.positions[i] << " steps  "
            << std::fixed << std::setprecision(4) << static_cast<double>(state.positions[i]) / options.scales()[i]
            << ' ' << options.units[i] << '\n';
    out << "Recorder: " << (state.recorder_failed ? "ERROR: allocation failed, incomplete" :
                               state.recording ? "RECORDING" : "STOPPED")
        << "\nSamples: " << state.samples << "  Changed samples: " << (state.samples ? state.samples - 1 : 0) << '\n';
    out << cnc::compact_io(state.io);
    return out.str();
}

std::string trim(std::string text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}
void run_console(cnc::Simulation& simulation, const cnc::SimulatorConfig& options) {
    std::future<std::string> saving;
    {
        cnc::TerminalUI terminal(options.interactive);
        if (!terminal.fixed()) {
            std::cout << "Stepper-Ninja virtual device (Phase 5, 4 stepgens / 3 stationary encoders)\n"
                      << "Wire sizes: " << cnc::request_size << " RX / " << cnc::response_size << " TX\n"
                      << "Reference: zero steps on startup; scales are local configuration\n"
                      << "Listening: " << options.network.bind_address << ':' << simulation.port() << std::endl;
        }
        std::string message = "Ready. help: commands; record begin: new recording; quit: exit";
        std::string peer;
        auto last_draw = std::chrono::steady_clock::now() - std::chrono::seconds(1);
        auto last_stats = last_draw;
        while (!stopping) {
            for (const auto& raw : terminal.read_commands(20)) {
                const auto line = trim(raw);
                if (line.empty()) continue;
                try {
                    std::istringstream parser(line);
                    std::string first, second, extra;
                    parser >> first;
                    if (first == "quit") {
                        if (parser >> extra) throw std::runtime_error("usage: quit");
                        stopping = 1;
                        break;
                    } else if (first == "help") {
                        parser >> extra;
                        if (!extra.empty() && extra != "io") throw std::runtime_error("usage: help [io]");
                        std::string trailing;
                        if (parser >> trailing) throw std::runtime_error("usage: help [io]");
                        message = "record begin: new; record stop: retain; record clear: stop and erase\n"
                            "record save <file>: CSV snapshot; status: refresh; help; quit: exit\n"
                            "Aliases: begin record, stop record, clear record, save record <file>\n"
                            "input show | n on/off | clear; limits show; probe show\n"
                            "help io: sensor configuration in steps\n" + std::string(cnc::workpiece_help) + "\n" + cnc::material_help;
                        if (extra == "io") message = "input show | input n on/off | input clear (manual OR automatic)\n"
                            "limits set X min <trigger_steps> <hysteresis_steps> <input>\n"
                            "limits off X min; limits show (X/Y/Z; min/max)\n"
                            "probe plane Z <surface_steps> <input>; probe off; probe show\n"
                            "Plane: coordinate <= surface. Default sensors disabled.";
                    } else if (first == "input" || first == "limits" || first == "probe") {
                        const auto command = cnc::parse_io_command(line);
                        message = cnc::describe_io(command, simulation.command(cnc::Action::IO, command).io);
                    } else if (first == "tool" || first == "material") {
                        message = cnc::execute_material_command(simulation, line);
                    } else if (first == "workpiece") {
                        const auto before = simulation.workpiece_snapshot();
                        const auto command = cnc::parse_workpiece_command(line, before->config);
                        if (!command.show) simulation.configure_workpiece(command.config);
                        message = cnc::describe_workpiece(*simulation.workpiece_snapshot());
                        if (!command.show && before->legacy_rotation)
                            message += "\nLegacy rotation cleared: axis-aligned fresh raw stock.";
                    } else if (first == "status") {
                        if (parser >> extra) throw std::runtime_error("usage: status");
                        message = simulation.command(cnc::Action::Status).message;
                        if (!terminal.fixed()) std::cout << statistics(simulation.status(), options, false)
                            << cnc::describe_material(simulation.material_status()) << std::endl;
                    } else {
                        parser >> second;
                        if (second == "record") std::swap(first, second);
                        if (first != "record") throw std::runtime_error("unknown command; type help");
                        if (second == "save") {
                            std::string filename;
                            std::getline(parser, filename);
                            filename = trim(filename);
                            if (filename.empty()) throw std::runtime_error("usage: record save <file>");
                            if (saving.valid()) throw std::runtime_error("save already in progress");
                            auto snapshot = simulation.command(cnc::Action::Save).recording;
                            if (!snapshot.count) throw std::runtime_error("no samples to save");
                            saving = std::async(std::launch::async,
                                [snapshot = std::move(snapshot), scales = options.scales(), units = options.units, filename] {
                                    try {
                                        cnc::save_csv(snapshot, scales, units, filename);
                                        return "Saved " + std::to_string(snapshot.count) + " samples: " + filename;
                                    } catch (const std::system_error& error) {
                                        return std::string("Error saving CSV: ") + error.code().message() + " (" + filename + ")";
                                    } catch (const std::exception& error) { return std::string("Error: ") + error.what(); }
                                });
                            message = "Saving snapshot; UDP and recording continue.";
                        } else {
                            if (parser >> extra) throw std::runtime_error("unexpected command argument");
                            cnc::Action command;
                            if (second == "begin") command = cnc::Action::Begin;
                            else if (second == "stop") command = cnc::Action::Stop;
                            else if (second == "clear") command = cnc::Action::Clear;
                            else throw std::runtime_error("unknown command; type help");
                            message = simulation.command(command).message;
                        }
                    }
                } catch (const std::exception& error) { message = std::string("Error: ") + error.what(); }
                if (!terminal.fixed()) std::cout << message << std::endl;
            }
            if (saving.valid() && saving.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                message = saving.get();
                if (!terminal.fixed()) std::cout << message << std::endl;
            }
            const auto state = simulation.status();
            const auto material = simulation.material_status();
            if (!material.error.empty()) throw std::runtime_error(material.error);
            if (!state.fatal_error.empty()) throw std::runtime_error(state.fatal_error);
            const auto now = std::chrono::steady_clock::now();
            if (!terminal.fixed() && state.peer != peer) {
                peer = state.peer;
                std::cout << "Connected peer: " << peer << std::endl;
            }
            if (now - last_draw >= std::chrono::milliseconds(200)) {
                terminal.draw(statistics(state, options, true) + cnc::describe_material(material, true), message);
                if (options.verbose && !terminal.fixed()) std::cout << "Latest RX: " << state.packets.received_packets
                    << " accepted: " << state.packets.accepted_packets << " invalid: " << state.packets.invalid_packets << std::endl;
                last_draw = now;
            }
            if (!terminal.fixed() && options.stats_ms && now - last_stats >= std::chrono::milliseconds(options.stats_ms)) {
                std::cout << statistics(state, options, false) << cnc::describe_material(material) << std::endl;
                last_stats = now;
            }
        }
    } // Restore terminal before joining a pending disk write or reporting errors.
    if (saving.valid()) std::cout << saving.get() << '\n';
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--help") { cnc::simulator_usage(); return 0; }
        const auto options = cnc::load_simulator_config(argc, argv,
#ifdef CNC_SIM_RENDER
            true
#else
            false
#endif
        );
        if (options.print_config) { std::cout << cnc::describe_config(options); return 0; }
        struct sigaction action{};
        action.sa_handler = stop_handler;
        sigemptyset(&action.sa_mask);
        if (sigaction(SIGINT, &action, nullptr) < 0 || sigaction(SIGTERM, &action, nullptr) < 0)
            throw std::runtime_error("cannot install signal handlers");
        // A disconnected output pipe must not kill the network worker with SIGPIPE.
        struct sigaction ignore{};
        ignore.sa_handler = SIG_IGN;
        sigaction(SIGPIPE, &ignore, nullptr);
        cnc::Simulation simulation(options.network.bind_address, options.network.port, options.scales(), options.io.commands,
                                   std::nullopt, options.workers, options.initial_workpiece,
                                   options.tool, options.material_enabled);
#ifdef CNC_SIM_RENDER
        if (options.render) {
            auto rendered = simulation.workpiece_snapshot();
            cnc::Renderer renderer(*rendered->volume, {}, false);
            std::exception_ptr console_error;
            std::jthread console([&] {
                try { run_console(simulation, options); }
                catch (...) { console_error = std::current_exception(); stopping = true; }
            });
            try {
                while (!stopping) {
                    renderer.apply_material(simulation.material_meshes());
                    if (!renderer.draw(simulation.machine_snapshot())) break;
                    renderer.present();
                }
            } catch (...) {
                stopping = true;
                console.join();
                throw;
            }
            stopping = true;
            console.join();
            if (console_error) std::rethrow_exception(console_error);
        } else
#endif
        { run_console(simulation, options); }
        simulation.stop();
        if (!simulation.material_status().error.empty()) throw std::runtime_error(simulation.material_status().error);
        if (!simulation.status().fatal_error.empty()) throw std::runtime_error(simulation.status().fatal_error);
        std::cout << "Stopped. Final statistics:\n" << statistics(simulation.status(), options, false) << cnc::describe_material(simulation.material_status()) << std::endl;
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "cnc-sim: " << error.what() << '\n';
        return 1;
    }
}
