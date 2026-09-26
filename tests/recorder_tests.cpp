#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <sstream>
#include <stdexcept>
#include <unistd.h>
#include "protocol/StepperNinjaProtocol.hpp"
#include "recorder/MotionRecorder.hpp"

// Deterministic allocation failure, confined to this single-threaded executable.
static bool fail_allocation = false;
void* operator new(std::size_t size) {
    if (fail_allocation) throw std::bad_alloc();
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " #x); } while (false)

int main() {
    try {
        cnc::StepperNinjaProtocol device;
        cnc::MotionRecorder recorder;
        unsigned id = 0;
        std::uint64_t now = 100000;
        auto packet = [&](cnc::StepPositions delta, bool valid = true) {
            cnc::Request request{};
            request.pio_timing = 235;
            request.packet_id = static_cast<std::uint8_t>(id++);
            for (std::size_t i = 0; i < 4; ++i)
                if (delta[i]) request.stepgen_command[i] = (delta[i] > 0 ? 0x80000000u : 0u) |
                    (19494u << 10) | static_cast<unsigned>(std::abs(delta[i]) - 1);
            request.checksum = calculate_checksum(&request, sizeof(request) - 1);
            if (!valid) request.checksum ^= 1;
            now += 1000;
            auto reply = device.process({reinterpret_cast<const std::uint8_t*>(&request), sizeof(request)}, now);
            if (reply) recorder.observe(device.machine().positions(), now);
            return reply.has_value();
        };
        CHECK(packet({100, 200, 300, 400}));
        CHECK(recorder.begin(device.machine().positions(), now));
        CHECK(!recorder.begin({}, now));
        auto start = recorder.snapshot();
        CHECK(start.count == 1 && start.tail.size() == 1);
        CHECK(start.tail[0].steps == (cnc::StepPositions{100, 200, 300, 400}));
        CHECK(start.tail[0].sequence == 0 && start.tail[0].changed_axes == 0 && start.tail[0].time_us == 0);
        for (int i = 0; i < 100; ++i) CHECK(packet({}));
        CHECK(recorder.count() == 1);
        CHECK(!packet({40, 40, 0, 0}, false));
        CHECK(recorder.count() == 1);
        CHECK(packet({3, 0, 0, 0}));
        CHECK(packet({-2, 5, 0, 0}));
        auto snapshot = recorder.snapshot();
        CHECK(snapshot.count == 3 && snapshot.tail[1].changed_axes == 1 && snapshot.tail[2].changed_axes == 3);
        CHECK(snapshot.tail[2].steps == (cnc::StepPositions{101, 205, 300, 400}));
        CHECK(snapshot.tail[1].time_us >= 102000 && snapshot.tail[2].time_us >= snapshot.tail[1].time_us);
        recorder.stop();
        CHECK(packet({10, 10, 0, 0}));
        CHECK(recorder.count() == 3);
        CHECK(recorder.begin(device.machine().positions(), now));
        CHECK(recorder.count() == 1 && recorder.snapshot().tail[0].sequence == 0);
        recorder.clear();
        CHECK(recorder.count() == 0 && !recorder.active());

        const auto max = std::numeric_limits<std::int64_t>::max();
        const auto min = std::numeric_limits<std::int64_t>::min();
        cnc::StepPositions position{max, min, 9007199254740993LL, -9007199254740993LL};
        CHECK(recorder.begin(position, 100));
        --position[0]; ++position[1]; ++position[2]; --position[3];
        recorder.observe(position, 150);
        auto exact = recorder.snapshot();
        CHECK(exact.tail[1].changed_axes == 15);
        for (unsigned i = 0; i < 4096; ++i) { --position[0]; recorder.observe(position, 200 + i); }
        auto large = recorder.snapshot();
        CHECK(large.count == 4098 && large.history && large.tail.size() <= 1024);
        CHECK(exact.count == 2 && exact.tail.size() == 2); // Snapshot unaffected by later writes.
        recorder.clear();
        CHECK(large.history->samples.size() == 1024 && large.tail.back().sequence == 4097);

        char pattern[] = "/tmp/cnc-sim-recorder-XXXXXX";
        const char* directory = mkdtemp(pattern);
        CHECK(directory);
        const std::filesystem::path dir(directory);
        const auto file = dir / "exact.csv";
        const cnc::Scales scales{400, 400, 400, 400};
        const std::array<std::string, 4> units{"mm", "mm", "mm", "unit"};
        cnc::save_csv(exact, scales, units, file.string());
        std::ifstream input(file);
        const std::string csv{std::istreambuf_iterator<char>(input), {}};
        CHECK(csv.find("# steps_per_unit=400,400,400,400\n# axis_units=mm,mm,mm,unit\n") != std::string::npos);
        CHECK(csv.find("sequence,time_us,x_steps,y_steps,z_steps,a_steps,changed_axes\n") != std::string::npos);
        CHECK(csv.find("0,0,9223372036854775807,-9223372036854775808,9007199254740993,-9007199254740993,0\n") != std::string::npos);
        CHECK(csv.find("1,50,9223372036854775806,-9223372036854775807,9007199254740994,-9007199254740994,15\n") != std::string::npos);
        bool rejected = false;
        try { cnc::save_csv(exact, scales, units, file.string()); } catch (const std::exception&) { rejected = true; }
        CHECK(rejected && std::filesystem::file_size(file) == csv.size());
        cnc::save_csv(large, scales, units, (dir / "large.csv").string());
        std::ifstream large_file(dir / "large.csv");
        std::uint64_t sequence = 0, last_time = 0;
        for (std::string line; std::getline(large_file, line);) {
            if (line.empty() || line[0] == '#' || line[0] == 's') continue;
            std::istringstream fields(line);
            std::string field;
            std::getline(fields, field, ','); CHECK(std::stoull(field) == sequence++);
            std::getline(fields, field, ','); CHECK(std::stoull(field) >= last_time); last_time = std::stoull(field);
        }
        CHECK(sequence == large.count);
        rejected = false;
        try { cnc::save_csv({}, scales, units, (dir / "empty.csv").string()); } catch (...) { rejected = true; }
        CHECK(rejected && !std::filesystem::exists(dir / "empty.csv"));
        std::filesystem::remove_all(dir);

        CHECK(recorder.begin({}, 100));
        for (int i = 1; i < 1024; ++i) recorder.observe({i, 0, 0, 0}, 100 + i);
        fail_allocation = true;
        recorder.observe({1024, 0, 0, 0}, 1200); // Next block allocation fails.
        fail_allocation = false;
        CHECK(recorder.failed() && !recorder.active() && recorder.count() == 1024);
        CHECK(recorder.snapshot().incomplete);
        fail_allocation = true;
        try { recorder.begin({}, 1300); } catch (const std::bad_alloc&) {}
        fail_allocation = false;
        CHECK(recorder.count() == 1024 && recorder.failed());
        CHECK(recorder.begin({}, 2000));
        recorder.observe({1, 0, 0, 0}, 2100);
        recorder.observe({2, 0, 0, 0}, 2050);
        CHECK(recorder.snapshot().tail.back().time_us == 100);
        std::cout << "PASS: start, idle, invalid, X/XY/XYZA, stop/clear/restart, time, exact CSV, immutable blocks, allocation failure\n";
    } catch (const std::exception& error) {
        fail_allocation = false;
        std::cerr << error.what() << '\n';
        return 1;
    }
}
