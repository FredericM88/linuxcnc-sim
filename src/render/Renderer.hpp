#pragma once
#include <memory>
#include "tool/Tool.hpp"
#include "volume/SparseVoxelVolume.hpp"

namespace cnc {
// Construct, draw, present and destroy on the main/context thread only.
// Receives immutable simulation data and copied machine snapshots.
class Renderer {
public:
    explicit Renderer(const SparseVoxelVolume& volume, ToolDefinition tool = {});
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    void set_workpiece(const SparseVoxelVolume& volume);
    bool draw(const MachineSnapshot& snapshot);
    void present();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace cnc
