#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace cnc {
using InputWords = std::array<std::uint32_t, 4>;
// Owned by the simulation thread; readers receive value snapshots.
class VirtualIO {
public:
    static constexpr std::size_t input_count = 128;
    void set_input(std::size_t index, bool state);
    bool input(std::size_t index) const;
    void clear_manual() noexcept { manual_ = {}; }
    void set_automatic(InputWords words) noexcept { automatic_ = words; }
    InputWords packed_inputs() const noexcept;
    InputWords manual_inputs() const noexcept { return manual_; }
    InputWords automatic_inputs() const noexcept { return automatic_; }
private:
    InputWords manual_{}, automatic_{};
};
} // namespace cnc
