# linuxcnc-sim v0.1.0 — release-notes draft

linuxcnc-sim is an early experimental standalone CNC machine simulator for
LinuxCNC. It emulates motion hardware behind LinuxCNC instead of independently
interpreting G-code. LinuxCNC remains responsible for interpretation, trajectory
planning, coordinate systems, offsets and compensation.

This first public version includes:

- Four simulated step axes and configurable scales through the original
  Stepper-Ninja HAL/UDP interface.
- Virtual digital inputs, limit switches and LinuxCNC homing interaction,
  plus a configurable point/plane probe input.
- Interactive console, motion recording and CSV export.
- OpenGL visualization, machine-space workpiece placement and sparse voxel stock.
- Persistent flat-end material removal with continuous sweeps and exact batching.
- Simulator INI configuration, CLI overrides and startup configuration inspection.

**Experimental development software; not a machine safety system.** Geometry is
millimetre-based with a flat-end cutter only. There is no geometric stock probing,
toolsetter, spindle/holder simulation or collision model, general collision
detection, or rotary material transform. Performance and voxel resolution are
workload-dependent; this is not a hard-real-time or CAD-exact machining guarantee.

Stepper-Ninja, by Zsolt Viola, provides the existing HAL driver and protocol used
as the current interface. linuxcnc-sim does not claim authorship of that code.
This choice does not require a later physical CNC machine to use Stepper-Ninja
hardware. Other adapters are possible future work.

linuxcnc-sim is MIT-licensed; the bundled Stepper-Ninja files preserve their
separate original MIT license and attribution. See the [Quick Start](../README.md#quick-start),
[configuration guide](simulator-configuration.md) and
[release validation](release-validation-v0.1.0.md).
