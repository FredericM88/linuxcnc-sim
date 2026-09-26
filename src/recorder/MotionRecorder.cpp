#include "recorder/MotionRecorder.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <unistd.h>

namespace cnc {
MotionBlock::~MotionBlock() {
    // Avoid recursive destruction of a long, uniquely owned block chain.
    while (previous && previous.use_count() == 1) {
        auto parent = std::move(previous);
        // Blocks are allocated non-const; only the last owner can reach here.
        previous = std::move(const_cast<MotionBlock*>(parent.get())->previous);
    }
}

bool MotionRecorder::begin(const StepPositions& position, std::uint64_t now_us) {
    if (active_) return false;
    std::vector<MotionSample> fresh;
    fresh.reserve(block_size); // Failure leaves the previous recording intact.
    fresh.push_back({0, 0, position, 0});
    history_.reset();
    tail_ = std::move(fresh);
    last_ = position;
    count_ = 1;
    begin_us_ = now_us;
    last_us_ = 0;
    failed_ = false;
    active_ = true;
    return true;
}

void MotionRecorder::clear() noexcept {
    stop();
    history_.reset();
    std::vector<MotionSample>().swap(tail_);
    count_ = 0;
    failed_ = false;
}

void MotionRecorder::append(MotionSample sample) {
    if (tail_.size() == block_size) {
        auto block = std::make_shared<MotionBlock>();
        std::vector<MotionSample> fresh;
        fresh.reserve(block_size);
        block->previous = history_;
        block->samples = std::move(tail_);
        history_ = std::move(block);
        tail_ = std::move(fresh);
    }
    tail_.push_back(sample);
    ++count_;
}

void MotionRecorder::observe(const StepPositions& position, std::uint64_t now_us) noexcept {
    if (!active_) return;
    std::uint8_t changed = 0;
    for (std::size_t i = 0; i < axis_count; ++i) {
        if (position[i] != last_[i]) changed |= static_cast<std::uint8_t>(1u << i);
    }
    if (!changed) return;
    const auto time = std::max(last_us_, now_us >= begin_us_ ? now_us - begin_us_ : 0);
    try {
        append({count_, time, position, changed});
        last_ = position;
        last_us_ = time;
    } catch (...) {
        // Keep the valid prefix and continue answering UDP. Never claim completeness.
        active_ = false;
        failed_ = true;
    }
}

MotionSnapshot MotionRecorder::snapshot() const { return {history_, tail_, count_, failed_}; }

void save_csv(const MotionSnapshot& snapshot, const Scales& scales,
              const std::array<std::string, axis_count>& units, const std::string& filename) {
    if (!snapshot.count) throw std::runtime_error("no samples to save");
    if (filename.empty() || filename.find('\0') != std::string::npos)
        throw std::runtime_error("invalid filename");
    // O_EXCL also rejects existing symlinks, atomically; never truncate an old file.
    const int fd = open(filename.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if (fd < 0) throw std::system_error(errno, std::generic_category(), "save " + filename);
    FILE* file = fdopen(fd, "w");
    if (!file) {
        const int error = errno;
        close(fd);
        unlink(filename.c_str());
        throw std::system_error(error, std::generic_category(), "fdopen");
    }
    try {
        std::ostringstream metadata;
        metadata.imbue(std::locale::classic());
        metadata << "# cnc-sim motion record\n# steps_per_unit=" << std::setprecision(17);
        for (std::size_t i = 0; i < axis_count; ++i) metadata << (i ? "," : "") << scales[i];
        metadata << "\n# axis_units=";
        for (std::size_t i = 0; i < axis_count; ++i) metadata << (i ? "," : "") << units[i];
        if (snapshot.incomplete) metadata << "\n# incomplete=allocation failure; valid prefix only";
        metadata << "\nsequence,time_us,x_steps,y_steps,z_steps,a_steps,changed_axes\n";
        const auto header = metadata.str();
        if (fwrite(header.data(), 1, header.size(), file) != header.size())
            throw std::runtime_error("CSV header write failed");
        std::vector<const MotionBlock*> blocks;
        for (auto block = snapshot.history.get(); block; block = block->previous.get()) blocks.push_back(block);
        auto write_samples = [&](const auto& samples) {
            for (const auto& s : samples) {
                if (fprintf(file, "%llu,%llu,%lld,%lld,%lld,%lld,%u\n",
                            static_cast<unsigned long long>(s.sequence), static_cast<unsigned long long>(s.time_us),
                            static_cast<long long>(s.steps[0]), static_cast<long long>(s.steps[1]),
                            static_cast<long long>(s.steps[2]), static_cast<long long>(s.steps[3]),
                            unsigned(s.changed_axes)) < 0) throw std::runtime_error("CSV data write failed");
            }
        };
        for (auto it = blocks.rbegin(); it != blocks.rend(); ++it) write_samples((*it)->samples);
        write_samples(snapshot.tail);
        const int result = fclose(file);
        file = nullptr;
        if (result != 0) throw std::runtime_error("CSV close/write failed");
    } catch (...) {
        if (file) fclose(file);
        // This file was exclusively created by this operation, never an old target.
        unlink(filename.c_str());
        throw;
    }
}
} // namespace cnc
