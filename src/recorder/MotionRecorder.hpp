#pragma once

#include <memory>
#include <string>
#include <vector>
#include "machine/MachineState.hpp"

namespace cnc {
struct MotionSample {
    std::uint64_t sequence{}, time_us{};
    StepPositions steps{};
    std::uint8_t changed_axes{};
};

// Published blocks are immutable. A snapshot copies at most 1024 samples,
// independent of recording length; CSV traversal happens outside the UDP thread.
struct MotionBlock {
    std::shared_ptr<const MotionBlock> previous;
    std::vector<MotionSample> samples;
    ~MotionBlock();
};
struct MotionSnapshot {
    std::shared_ptr<const MotionBlock> history;
    std::vector<MotionSample> tail;
    std::uint64_t count{};
    bool incomplete{};
};

// Single owner (simulation thread). Snapshots can be read on other threads.
class MotionRecorder {
public:
    bool begin(const StepPositions& position, std::uint64_t now_us);
    void stop() noexcept { active_ = false; }
    void clear() noexcept;
    void observe(const StepPositions& position, std::uint64_t now_us) noexcept;
    MotionSnapshot snapshot() const;
    bool active() const { return active_; }
    bool failed() const { return failed_; }
    std::uint64_t count() const { return count_; }
private:
    void append(MotionSample sample);
    static constexpr std::size_t block_size = 1024;
    std::shared_ptr<const MotionBlock> history_;
    std::vector<MotionSample> tail_;
    StepPositions last_{};
    std::uint64_t count_{}, begin_us_{}, last_us_{};
    bool active_{}, failed_{};
};

void save_csv(const MotionSnapshot& snapshot, const Scales& scales,
              const std::array<std::string, axis_count>& units, const std::string& filename);
} // namespace cnc
