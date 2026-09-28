#pragma once
#include "volume/SparseVoxelVolume.hpp"
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

namespace cnc::test {
// Test-only canonical dense order: z outermost, then y, x fastest. Implicit
// chunks and unordered_map insertion/iteration order do not affect the result.
struct VoxelReference {
    VoxelCoord dimensions;
    std::vector<VoxelValue> values;

    static VoxelReference capture(const SparseVoxelVolume& volume) {
        VoxelReference result{volume.dimensions(), {}};
        const auto d=result.dimensions;
        result.values.reserve(static_cast<std::size_t>(d.x*d.y*d.z));
        for (std::int64_t z=0; z<d.z; ++z) for (std::int64_t y=0; y<d.y; ++y) for (std::int64_t x=0; x<d.x; ++x)
            result.values.push_back(volume.sample({x,y,z}));
        return result;
    }
    std::uint64_t differences(const SparseVoxelVolume& volume) const {
        if (volume.dimensions()!=dimensions) throw std::runtime_error("voxel reference dimensions differ");
        std::size_t i=0;
        std::uint64_t different=0;
        const auto d=dimensions;
        for (std::int64_t z=0; z<d.z; ++z) for (std::int64_t y=0; y<d.y; ++y) for (std::int64_t x=0; x<d.x; ++x)
            different += values.at(i++)!=volume.sample({x,y,z});
        return different;
    }
    void require_equal(const SparseVoxelVolume& volume) const {
        const auto different=differences(volume);
        if (different) throw std::runtime_error("exact voxel comparison: " + std::to_string(different) + " cells differ");
    }
    std::string fingerprint() const {
        // FNV-1a-64 over dimensions as uint64 little-endian, then one byte/cell.
        // Diagnostic only: correctness comparisons above inspect every cell.
        std::uint64_t hash=14695981039346656037ull;
        auto byte=[&](std::uint8_t value) { hash^=value; hash*=1099511628211ull; };
        for (int axis=0; axis<3; ++axis) {
            const auto d=static_cast<std::uint64_t>(dimensions[axis]);
            for (unsigned shift=0; shift<64; shift+=8) byte(static_cast<std::uint8_t>(d>>shift));
        }
        for (auto value : values) byte(value);
        std::ostringstream out; out<<std::hex<<std::setfill('0')<<std::setw(16)<<hash;
        return out.str();
    }
    void write(const std::string& path) const {
        std::ofstream out(path);
        if (!out) throw std::runtime_error("cannot write voxel reference");
        out<<"CNC_VOXEL_RLE_V1\n"<<dimensions.x<<' '<<dimensions.y<<' '<<dimensions.z<<'\n';
        for (std::size_t begin=0; begin<values.size();) {
            auto end=begin+1;
            while (end<values.size() && values[end]==values[begin]) ++end;
            out<<static_cast<unsigned>(values[begin])<<' '<<end-begin<<'\n';
            begin=end;
        }
        if (!out) throw std::runtime_error("voxel reference write failed");
    }
    static VoxelReference read(const std::string& path) {
        std::ifstream in(path); std::string magic;
        VoxelReference result{};
        if (!(in>>magic>>result.dimensions.x>>result.dimensions.y>>result.dimensions.z) || magic!="CNC_VOXEL_RLE_V1")
            throw std::runtime_error("invalid voxel reference: " + path);
        const auto d=result.dimensions;
        // Bounded test data; reject malformed headers before allocation/arithmetic.
        for (int axis=0; axis<3; ++axis) if (d[axis]<=0 || d[axis]>1000) throw std::runtime_error("invalid reference dimension");
        const auto total=static_cast<std::size_t>(d.x*d.y*d.z);
        result.values.reserve(total);
        unsigned value; std::size_t length;
        while (result.values.size()<total) {
            if (!(in>>value>>length) || (value!=0 && value!=255) || !length || length>total-result.values.size())
                throw std::runtime_error("invalid/truncated voxel runs");
            result.values.insert(result.values.end(),length,static_cast<VoxelValue>(value));
        }
        std::string extra;
        if (in>>extra) throw std::runtime_error("trailing voxel reference data");
        return result;
    }
};
} // namespace cnc::test
