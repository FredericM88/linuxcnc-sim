#include "io/VirtualIO.hpp"
#include <stdexcept>

namespace cnc {
void VirtualIO::set_input(std::size_t index, bool state) {
    if (index >= input_count) throw std::out_of_range("input index must be 0..127");
    const auto bit = std::uint32_t{1} << (index % 32);
    if (state) manual_[index / 32] |= bit;
    else manual_[index / 32] &= ~bit;
}
bool VirtualIO::input(std::size_t index) const {
    if (index >= input_count) throw std::out_of_range("input index must be 0..127");
    return ((manual_[index / 32] | automatic_[index / 32]) & (std::uint32_t{1} << (index % 32))) != 0;
}
InputWords VirtualIO::packed_inputs() const noexcept {
    InputWords result;
    for (std::size_t i = 0; i < result.size(); ++i) result[i] = manual_[i] | automatic_[i];
    return result;
}
} // namespace cnc
