# LinuxCNC Simulator – Phase 4A.1
## Configurable Workpiece Geometry and Placement

**Project:** `linuxcnc-sim`  
**Phase:** 4A.1  
**Prerequisite:** Phase 4A  
**Scope:** Configurable workpiece geometry/placement; no material removal.

## 1. Goal

Make the virtual raw workpiece configurable at runtime through the existing simulator console. Phase 4A.1 establishes the coordinate model required before Phase 4B material removal.

The independent parameters are:

```text
workpiece size     X Y Z
workpiece position X Y Z
workpiece origin   X Y Z
workpiece voxel    SIZE
```

Semantics:

- **size** = physical dimensions of the raw stock in mm.
- **position** = G53 machine-space position of the selected workpiece reference point.
- **origin** = location of that reference point inside the workpiece, expressed as offsets from the workpiece minimum corner.
- **voxel** = voxel resolution in mm.

LinuxCNC remains authoritative for G54/G55/etc. The simulator must not apply work offsets a second time.

## 2. Coordinate model

For each axis:

```text
machine_min = position - origin_offset
machine_max = machine_min + size
```

Vector form:

```text
machine_min = position_machine_mm - origin_offset_mm
machine_max = machine_min + size_mm
```

### Example: common CNC setup

```text
workpiece size     100 60 20
workpiece position 0 0 0
workpiece origin   min min max
```

The origin resolves to `(0, 0, 20)` and the machine-space bounds are:

```text
X:   0 .. 100 mm
Y:   0 ..  60 mm
Z: -20 ..   0 mm
```

Thus Z=0 is the stock top surface.

### Example: centre of top face

```text
workpiece size     100 60 20
workpiece position 0 0 0
workpiece origin   center center max
```

Bounds:

```text
X: -50 .. +50 mm
Y: -30 .. +30 mm
Z: -20 ..   0 mm
```

### Example: arbitrary reference point

```text
workpiece size     100 60 20
workpiece position 50 30 0
workpiece origin   10 15 20
```

Bounds:

```text
X:  40 .. 140 mm
Y:  15 ..  75 mm
Z: -20 ..   0 mm
```

## 3. Origin syntax

`workpiece origin` must support continuous numerical offsets in millimetres.

Additionally, each axis independently accepts:

```text
min
center
max
```

Resolve against the current workpiece size:

```text
min     -> 0
center  -> size / 2
max     -> size
```

Mixed symbolic/numeric input is required:

```text
workpiece origin 25 center max
```

For size `100 60 20`, this resolves to `(25, 30, 20)`.

Internally, store numerical offsets in millimetres. Symbolic values are console conveniences, not a limitation of the data model.

Conceptually:

```cpp
struct WorkpieceConfig {
    glm::dvec3 size_mm;
    glm::dvec3 position_machine_mm;
    glm::dvec3 origin_offset_mm;
    double voxel_size_mm;
};
```

Equivalent project-native types are acceptable.

Normal numerical origin range:

```text
0 <= origin_axis <= size_axis
```

Phase 4A.1 shall enforce this range.

## 4. Console interface

Extend the existing console with:

```text
workpiece show
workpiece size X Y Z
workpiece position X Y Z
workpiece origin X Y Z
workpiece voxel SIZE
workpiece reset
```

Update console help accordingly.

Example:

```text
workpiece size 100 60 20
workpiece position 50 30 0
workpiece origin 10 center max
workpiece voxel 0.10
workpiece show
```

## 5. `workpiece show`

It must expose the complete transformation, for example:

```text
Workpiece
----------------------------------------
Size:
  X 100.000  Y 60.000  Z 20.000 mm

Position (G53):
  X 50.000   Y 30.000  Z 0.000 mm

Origin offset from workpiece min:
  X 10.000   Y 15.000  Z 20.000 mm

Machine bounds:
  X  40.000 .. 140.000 mm
  Y  15.000 ..  75.000 mm
  Z -20.000 ..   0.000 mm

Voxel size:
  0.100 mm

Chunk size:
  32 x 32 x 32
```

Numerical values are authoritative. The UI may additionally label exact `min`, `center`, or `max` values.

## 6. Runtime behaviour

A valid change to size, position, origin, voxel resolution, or reset must:

1. update the authoritative simulation configuration;
2. recalculate machine-space bounds;
3. rebuild/reconfigure the sparse voxel workpiece as necessary;
4. invalidate affected mesh state;
5. regenerate renderable geometry;
6. safely publish the new state to the renderer;
7. keep UDP/network processing independent of rendering/remeshing.

**UDP processing must never wait for OpenGL rendering.**

Workpiece configuration belongs to simulation state, not to the renderer.

Changing the workpiece must not move the tool. Tool position continues to come from actual integrated LinuxCNC/Stepper-Ninja machine motion.

If Home/fit currently relies on static stock bounds, update it to use the active workpiece bounds.

## 7. Rebuild semantics

Phase 4A.1 has no material removal, so rebuilding raw stock is acceptable.

Establish deterministic semantics suitable for Phase 4B: configuration changes define/reposition a fresh raw workpiece state unless the implementation can safely distinguish a pure transform from material-state changes.

`workpiece reset` restores centralized documented defaults and rebuilds the workpiece.

Do not duplicate default values across console, simulation, renderer, or tests.

## 8. Voxel configuration

Default voxel size remains `0.10 mm` unless existing Phase 4A code/documentation has another authoritative default.

Chunk size remains fixed:

```text
32 x 32 x 32
```

Voxel size must be finite and > 0. Reject values that would cause indexing overflow or impractical allocation. Do not silently clamp invalid values.

## 9. Validation

Commands must be transactional: invalid input must leave the previous valid workpiece unchanged.

Reject examples such as:

```text
workpiece size 0 60 20
workpiece size -100 60 20
workpiece position nan 0 0
workpiece origin foo center max
workpiece origin -1 center max
workpiece voxel 0
workpiece voxel -0.1
```

All sizes must be finite and > 0. Positions must be finite. Numerical origin offsets must be finite and within `[0,size]`.

If a size change would make the current numerical origin invalid, do not leave an invalid state. Prefer rejecting the size change with a clear error unless a different explicit, documented, deterministic policy is implemented.

## 10. Architecture constraints

Preserve Phase 4A separation:

```text
LinuxCNC / UDP
      |
      v
MachineState / actual step positions
      |
      v
Simulation state
      |
      +---- WorkpieceConfig
      +---- SparseVoxelVolume
      +---- CPU meshing
      |
      v
render snapshot
      |
      v
OpenGL renderer
```

No OpenGL ownership of workpiece state. No OpenGL calls from the UDP/network thread.

Do not change:

- original Stepper-Ninja wire protocol;
- copied vendor/third-party protocol sources;
- packet sizes/checksum semantics;
- HAL protocol behaviour;
- MachineState step integration;
- VirtualIO wire mapping;
- Phase 3 limit/probe behaviour.

## 11. Explicit non-goals

Do not implement in 4A.1:

- material removal;
- swept-tool cutting;
- collision detection;
- contact physics;
- new probing semantics;
- A-axis stock rotation;
- G54/G55 management;
- work-offset synchronization.

## 12. Tests

Add automated tests for geometry/bounds, parser behaviour, runtime rebuild/state publication, invalid transactional updates, and regressions.

Required geometry cases:

```text
size 100 60 20
position 0 0 0
origin min min max
=> min (0,0,-20), max (100,60,0)
```

```text
size 100 60 20
position 0 0 0
origin center center max
=> min (-50,-30,-20), max (50,30,0)
```

```text
size 100 60 20
position 50 30 0
origin 10 15 20
=> min (40,15,-20), max (140,75,0)
```

Parser coverage must include `min`, `center`, `max`, numeric, mixed symbolic/numeric, invalid tokens, NaN/Inf, and out-of-range origin.

Verify rendering-disabled/headless builds still work and all previous Phase 1–4A tests continue to pass.

## 13. Documentation

Update relevant files such as:

```text
README.md
docs/repository.md
docs/phase4a-design.md
docs/phase4a-test.md
```

or add dedicated Phase 4A.1 docs if cleaner.

Terminology must remain consistent:

```text
position = G53 position of the selected workpiece reference point
origin   = offset of that reference point from the workpiece minimum corner
size     = physical raw-stock dimensions
```

## 14. Compatibility

The existing real-test launch path must remain functional, including:

```bash
sudo ip netns exec cnc-sim-ns   runuser -u "$USER" -- env   DISPLAY="$DISPLAY"   XAUTHORITY="${XAUTHORITY:-$HOME/.Xauthority}"   XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"   ./build/cnc-sim     --render     --steps-per-unit 400,400,400,400     --io-config examples/phase3/virtual-io.conf
```

Phase 3 VirtualIO, homing, probe, recorder, terminal dashboard, and Phase 4A renderer must continue to work.

## 15. Recommended default

Use a conventional default equivalent to:

```text
workpiece size     50 50 10
workpiece position 0 0 0
workpiece origin   min min max
workpiece voxel    0.10
```

giving:

```text
X:   0 .. 50 mm
Y:   0 .. 50 mm
Z: -10 ..  0 mm
```

If current Phase 4A dimensions differ, preserve them where practical while adopting the new position/origin semantics consistently.

## 16. Acceptance criteria

Phase 4A.1 is complete when all of the following are true:

- runtime console can change workpiece size;
- runtime console can change G53 workpiece position;
- origin accepts continuous mm offsets;
- origin also accepts per-axis `min|center|max`;
- numeric and symbolic origin components can be mixed;
- voxel size is configurable;
- calculated machine-space bounds are correct;
- OpenGL reflects valid changes without simulator restart;
- tool position remains based on actual LinuxCNC machine motion;
- invalid configurations are rejected without partial state changes;
- prior tests still pass;
- new transformation/parser/runtime tests pass;
- headless/render-disabled builds still work;
- Stepper-Ninja protocol/vendor code remains unchanged;
- documentation is updated;
- implementation is committed locally and repository is clean.

# End of Phase 4A.1 specification
