#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
#include "protocol/StepperNinjaProtocol.hpp"

#define CHECK(condition) do { if (!(condition)) throw std::runtime_error(\
    std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " #condition); } while (false)

namespace {
std::vector<std::uint8_t> bytes(cnc::Request request) {
    request.checksum = calculate_checksum(&request, sizeof(request) - 1);
    std::vector<std::uint8_t> result(sizeof(request));
    std::memcpy(result.data(), &request, result.size());
    return result;
}

std::uint32_t word(unsigned count, bool forward, std::uint32_t timing = 1234) {
    return (forward ? 0x80000000u : 0u) | (timing << 10) | (count - 1u);
}

void checksum_vectors() {
    CHECK(sizeof(cnc::Request) == 37);
    CHECK(sizeof(cnc::Response) == 61);
    cnc::Request request{};
    request.checksum = 0x90;
    CHECK(rx_checksum_ok(&request));
    cnc::Response response{};
    response.checksum = 0xf0;
    CHECK(tx_checksum_ok(&response));
    request.stepgen_command[0] = 0x81309809;
    request.pio_timing = 235;
    request.packet_id = 42;
    request.checksum = 0x99; // Independently recorded in the protocol analysis.
    CHECK(rx_checksum_ok(&request));
    response.jitter = 1000;
    response.packet_id = 42;
    response.checksum = 0x03;
    CHECK(tx_checksum_ok(&response));
    // Includes bytes >= 128 and exercises unsigned indexing and 8-bit overflow.
    std::array<std::uint8_t, 255> pattern{};
    for (unsigned i = 0; i < pattern.size(); ++i) pattern[i] = static_cast<std::uint8_t>(i);
    CHECK(calculate_checksum(pattern.data(), 255) == 0xe2); // Sum(0..255) - jump_table[255].
    request.stepgen_command[0] ^= 1u;
    CHECK(!rx_checksum_ok(&request));
    response.checksum ^= 1u;
    CHECK(!tx_checksum_ok(&response));
}

void decoder() {
    const auto idle = cnc::decode_step_burst(0);
    CHECK(!idle.active && idle.steps == 0 && idle.timing == 0 && idle.delta() == 0);
    for (const bool forward : {false, true}) {
        for (unsigned n = 1; n <= 1024; ++n) {
            for (const auto timing : {1u, 19494u, 0x1fffffu}) {
                const auto burst = cnc::decode_step_burst(word(n, forward, timing));
                CHECK(burst.active && burst.direction == forward);
                CHECK(burst.steps == n && burst.timing == timing);
                CHECK(burst.delta() == (forward ? std::int64_t(n) : -std::int64_t(n)));
            }
        }
    }
    CHECK(cnc::decode_step_burst(0x80000000u).delta() == 1);
    CHECK(cnc::decode_step_burst(0x3ffu).delta() == -1024);
    CHECK(cnc::decode_step_burst(0x81309809u).delta() == 10);
}

void integration() {
    cnc::MachineState machine({400, 100, -200, 10});
    CHECK(machine.positions() == cnc::StepPositions{});
    CHECK(machine.integrate({10, 1, -2, 3}));
    CHECK(machine.integrate({20, 0, 0, 0}));
    CHECK(machine.integrate({-5, 0, 0, 0}));
    CHECK(machine.positions() == (cnc::StepPositions{25, 1, -2, 3}));
    CHECK(machine.position_units(0) == 0.0625);
    CHECK(machine.position_units(2) == 0.01);
    cnc::MachineState limit;
    CHECK(limit.integrate({0, std::numeric_limits<std::int64_t>::max(), 0, 0}));
    CHECK(!limit.integrate({10, 1, 0, 0}));
    CHECK(limit.positions()[0] == 0); // No partial commit.
    cnc::MachineState negative_limit;
    CHECK(negative_limit.integrate({std::numeric_limits<std::int64_t>::min(), 0, 0, 0}));
    CHECK(!negative_limit.integrate({-1, 0, 0, 0}));
    for (const double invalid : {0.0, std::numeric_limits<double>::infinity(), std::nan("")}) {
        bool rejected = false;
        try { cnc::MachineState bad({invalid, 1, 1, 1}); }
        catch (const std::invalid_argument&) { rejected = true; }
        CHECK(rejected);
    }
}

void sequence() {
    cnc::PacketSequence sequence;
    CHECK(sequence.observe(254)); // Expected initial ID 0, synchronize once.
    CHECK(!sequence.observe(255));
    CHECK(!sequence.observe(0));
    CHECK(!sequence.observe(1));
    CHECK(sequence.observe(3));
    CHECK(!sequence.observe(4));
    CHECK(sequence.observe(4)); // Duplicate is a discontinuity too.
    CHECK(sequence.expected() == 5);
}

void receive_pipeline() {
    cnc::StepperNinjaProtocol device;
    cnc::Request request{};
    request.pio_timing = 235;
    request.stepgen_command[0] = word(10, true);
    auto packet = bytes(request);
    auto response = device.process(packet, 1000);
    CHECK(response && response->packet_id == 0 && tx_checksum_ok(&*response));
    CHECK(device.machine().positions()[0] == 10);
    CHECK(response->jitter == 1000 && response->encoder_timestamp[2] == 1000);
    CHECK(response->encoder_counter[0] == 0 && response->encoder_velocity[1] == 0);
    CHECK(response->interrupt_data == 0 && response->inputs[3] == 0);
    CHECK(response->step_ring_fill == 0 && response->step_ring_status == 0);

    packet[0] ^= 1;
    CHECK(!device.process(packet, 1100));
    packet = bytes(request);
    packet.push_back(0);
    CHECK(!device.process(packet, 1200));
    packet.resize(36);
    CHECK(!device.process(packet, 1300));
    CHECK(!device.process({}, 1400));
    request.pio_timing = 299;
    CHECK(!device.process(bytes(request), 1500));
    CHECK(device.machine().positions()[0] == 10);
    CHECK(device.stats().invalid_packets == 5);
    CHECK(device.stats().checksum_errors == 1 && device.stats().length_errors == 3);
    CHECK(device.stats().timing_errors == 1);

    request.pio_timing = 298;
    request.packet_id = 1;
    request.stepgen_command[0] = word(5, false);
    request.stepgen_command[3] = word(1024, true);
    response = device.process(bytes(request), 2000);
    CHECK(response && response->packet_id == 1 && response->jitter == 1000);
    CHECK(device.stats().packet_id_gaps == 0); // Invalid packets did not advance state.
    CHECK(device.machine().positions() == (cnc::StepPositions{5, 0, 0, 1024}));
    // Match firmware normal sequence semantics: duplicates are applied again.
    CHECK(device.process(bytes(request), 3000));
    CHECK(device.machine().positions()[0] == 0 && device.stats().packet_id_gaps == 1);
    response = device.process(bytes(request), (std::uint64_t{1} << 32) + 123);
    CHECK(response && response->encoder_timestamp[0] == 123 && tx_checksum_ok(&*response));
}
} // namespace

int main() {
    try {
        checksum_vectors(); decoder(); integration(); sequence(); receive_pipeline();
        std::cout << "PASS: layouts, original checksums, all burst counts/directions, positions, IDs, invalid packets\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
