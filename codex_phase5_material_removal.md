# LinuxCNC Simulator – Phase 5
## Continuous Voxel Material Removal

**Project:** `linuxcnc-sim`\
**Phase:** 5\
**Prerequisite:** Phase 4A + Phase 4A.1 (real LinuxCNC acceptance complete)\
**Primary milestone:** Turn the existing machine/workpiece visualizer into a persistent milling-material simulator.

## 1. Goal

Phase 5 introduces persistent material removal from the existing sparse voxel workpiece.

The simulator already has the original Stepper-Ninja HAL/wire path, integrated actual machine step positions, VirtualIO/homing/probe support, configurable workpiece geometry, 32³ sparse voxel chunks, CPU meshing and OpenGL visualization.

Phase 5 answers:

> Which workpiece voxels are swept by the cutting part of the tool during the actual machine motion produced by LinuxCNC?

Those voxels are permanently removed from the current material state.

The first visible milestone is a LinuxCNC toolpath producing a continuous groove or pocket in the virtual stock.

## 2. Core architecture

LinuxCNC remains authoritative for motion planning. Do not reinterpret G-code in the simulator.

```text
G-code
  ↓
LinuxCNC interpreter / offsets / compensation / trajectory planner
  ↓
original Stepper-Ninja HAL driver
  ↓
original Stepper-Ninja UDP protocol
  ↓
cnc-sim integrated step positions
  ↓
continuous tool motion
  ↓
material-removal engine
  ↓
sparse voxel state
  ↓
dirty-chunk remeshing
  ↓
OpenGL
```

This preserves LinuxCNC's handling of G0/G1/G2/G3, coordinate systems, compensation and trajectory planning.

## 3. Continuous sweep — not point stamping

Material must not be removed only at discrete sampled tool positions.

Incorrect:

```text
A                         B
O                         O
remove here               remove here
```

Correct:

```text
A=========================B
      swept tool volume
```

For each authoritative motion update:

```text
previous_tool_pose -> current_tool_pose
```

remove material intersected by the complete swept cutting geometry.

Correctness must not depend on the current ~1 ms sample rate.

## 4. Initial cutting tool

Implement one tool first:

```text
type: flat-end
diameter_mm
cutting_length_mm
```

The initial cutting geometry is an analytic cylinder aligned with tool/machine Z.

Define and document the tool reference point. Prefer bottom-centre/tool-tip for the first flat-end mill unless the existing abstraction already defines another convention.

Design the interface so later tools can be added without replacing the material engine:

- ball end mill
- bull-nose/corner-radius
- V-bit/engraving cutter
- chamfer mill

Do not implement all of them in Phase 5.

## 5. Workpiece coordinates

Use the Phase-4A.1 model:

```text
workpiece size
workpiece position
workpiece origin
workpiece voxel
```

Machine-space bounds:

```text
machine_min = position - origin
machine_max = machine_min + size
```

Tool motion arrives in G53 machine coordinates. Use one explicit transform path between machine space and workpiece-local/voxel space.

Do not duplicate LinuxCNC G54/G55 processing.

Keep the transform architecture compatible with later A-axis support without requiring rotated/resampled voxel storage. A-axis stock rotation itself is not required in Phase 5.

## 6. Sparse voxel material

Preserve 32×32×32 chunks.

Voxel semantics remain:

```text
0       empty
255     material
1..254  reserved for future use
```

Phase 5 uses binary removal:

```text
255 -> 0
```

Do not assign semantics to 1..254 yet.

Chunk states remain:

```text
Empty
Solid
Mixed
```

When a cut first reaches an implicit `Solid` chunk:

```text
Solid
 ↓
materialize 32³ voxel storage
 ↓
initialize valid stock voxels as material
 ↓
remove intersected voxels
 ↓
Mixed
```

If a Mixed chunk becomes fully empty, it should be collapsible to `Empty`.

## 7. Broad phase

Never scan the entire workpiece for every motion segment.

For each sweep, calculate a conservative AABB containing:

- previous tool pose
- current tool pose
- tool radius
- cutting length

Use it to determine potentially affected chunks.

Only those chunks enter narrow-phase voxel testing.

False-positive chunks are acceptable. False negatives are not.

## 8. Narrow phase

For candidate voxels, determine intersection with the continuous swept cutting geometry.

Prefer an analytic swept-cylinder/capsule/prism-style test rather than arbitrary path oversampling.

Required properties:

- continuous coverage between samples
- deterministic result
- no holes due to sample spacing
- horizontal, vertical and diagonal XYZ motion
- safe zero-length segments
- bounded numerical behaviour

A voxel-centre inclusion rule is acceptable for the first implementation if its accuracy is explicitly documented and tested. A conservative voxel-volume rule is also acceptable if documented.

## 9. Accuracy

Default voxel size is 0.10 mm.

Binary voxel geometry is quantized and must not be described as CAD-exact.

Tests for groove width, depth and volume must use tolerances derived from voxel size and the chosen inclusion rule.

The reserved values 1..254 may later support sub-voxel surfaces, but Phase 5 first establishes a correct binary engine.

## 10. Persistent material state

Removed material stays removed.

Subsequent sweeps operate on the already modified volume.

Camera movement, renderer updates or unrelated machine motion must never regenerate removed material.

`material reset` restores fresh raw stock using the current Phase-4A.1 workpiece configuration.

Changing workpiece configuration retains the established rebuild/reset semantics.

## 11. Dirty chunks and boundary invalidation

When material changes, update the existing dirty/version mechanism.

Only changed mesh chunks should be regenerated.

Meshing depends on neighbouring occupancy. A cut crossing a chunk boundary may require remeshing adjacent chunks even if their own voxel array did not change.

Use the existing halo/neighbour sampling design.

At minimum invalidate necessary face neighbours; include edge/corner neighbours if required by the mesher.

No stale internal faces, missing surfaces or cracks at chunk boundaries.

## 12. Hard real-time architecture rule

Never do this in the UDP-critical path:

```text
UDP
 -> integrate position
 -> remove voxels
 -> remesh
 -> OpenGL upload
 -> next UDP cycle
```

Preferred architecture:

```text
LinuxCNC / UDP (~1 ms)
        ↓
integrate authoritative machine position
        ↓
publish/enqueue ordered motion segment
        │
        └───────────────► material worker
                              ↓
                        swept removal
                              ↓
                         dirty chunks
                              ↓
                          CPU meshing
                              ↓
                    immutable mesh update
                              ↓
                       OpenGL main thread
```

Expensive material removal, meshing and OpenGL work must never block the Stepper-Ninja UDP cycle.

## 13. Ordering and backlog

Cutting segments are ordered and stateful.

Process:

```text
N -> N+1 -> N+2 ...
```

Never silently drop a motion segment needed for material removal.

If material processing falls behind, preserve sweep coverage using an explicit strategy such as a lossless queue, safe batching or conservative merging.

Expose queue/backlog diagnostics so overload is visible.

Rendering may temporarily lag material state. Lost cutting motion is not acceptable.

## 14. Console commands

Integrate with the existing console.

Suggested minimum:

```text
tool show
tool flat-end DIAMETER CUTTING_LENGTH

material show
material on
material off
material reset
```

Example:

```text
tool flat-end 6 20
material on
```

Material removal should have an explicit enable state. This allows homing and positioning without unintentionally cutting the stock.

Do not infer G0/G1 cutting semantics unless an authoritative motion-mode signal actually exists in the low-level interface.

## 15. Diagnostics

Expose useful statistics without burdening UDP:

```text
Material removal: ON/OFF
Tool: flat-end Ø6.000 L20.000
Sweeps processed
Current/max motion queue depth
Chunks tested
Chunks changed
Voxels tested
Voxels removed
Dirty mesh chunks
Mesh rebuild count
Material worker lag
```

Exact presentation may follow existing project style.

## 16. Initial acceptance setup

Recommended workpiece:

```text
workpiece size     30 20 10
workpiece position 15 10 -10
workpiece origin   center center max
workpiece voxel    0.10
```

Bounds:

```text
X   0 .. 30
Y   0 .. 20
Z -20 .. -10
```

Tool:

```text
tool flat-end 6 20
```

First cutting test: safely position above the stock, deliberately enable material simulation, plunge to a known depth and make a straight X/Y cut within the current Phase-3 LinuxCNC limits.

Expected top view:

```text
┌──────────────────────────────┐
│                              │
│     ==================>      │
│       ~6 mm groove           │
│                              │
└──────────────────────────────┘
```

Expected flat-end cross-section within voxel tolerance:

```text
████████      ████████
████████      ████████
████████______████████
██████████████████████
```

## 17. Automated tests

Add deterministic headless tests wherever possible.

### Static/vertical plunge
Verify correct voxel removal, Solid→Mixed materialization, untouched material and dirty/version updates.

### Horizontal groove
Verify continuous removal, expected width/depth within voxel tolerance and no gaps.

### Sample-spacing independence
Execute the same geometric sweep as many short segments and as a few long segments.

Resulting material must be identical or equivalent according to documented voxel semantics.

This is a critical Phase-5 test.

### Diagonal XYZ sweep
Verify continuous removal under simultaneous XYZ motion.

### Chunk-boundary cut
Cut across chunk boundaries and verify material continuity, neighbour invalidation and correct meshes.

### Repeat cut
Cut the same path twice. The second pass must not corrupt state.

### Outside-stock motion
Tool motion completely outside stock must not change material.

### Reset
After cutting, `material reset` restores fresh stock with the current workpiece configuration.

### Headless
Material simulation/tests work without OpenGL/GLFW.

### Regression
All previous Phase 1–4A.1 tests remain green.

## 18. Quantitative validation

Where practical track/test:

```text
material voxels before
material voxels after
removed voxel count
removed volume
```

Approximate removed volume:

```text
removed_volume_mm3 =
    removed_voxels * voxel_size_mm^3
```

Compare simple cuts against analytic expected geometry with voxel-appropriate tolerance.

Do not overstate accuracy.

## 19. OpenGL behaviour

Material removal should appear progressively.

Requirements:

- unchanged chunks keep mesh/GPU state where practical
- changed chunks receive updated meshes
- tool continues following actual machine position
- workpiece transform remains authoritative
- renderer remains interactive
- orbit/pan/zoom/Home remain functional
- removed material never reappears

Temporary render lag is acceptable. Lost material motion is not.

## 20. Performance principles

Correctness first, but avoid unbounded work.

Use:

- sparse chunks
- conservative sweep AABB
- chunk culling
- local voxel tests
- dirty-only remeshing
- versioned/immutable publication
- worker processing

Add lightweight timing metrics for material processing and meshing so later optimization is evidence-based.

## 21. Future collision architecture

Phase 5 should not yet implement full collision simulation, but its tool abstraction must allow later separation of:

```text
cutting geometry
non-cutting shaft
holder
spindle
```

Future semantics can then become:

```text
cutting geometry + workpiece -> remove material
shaft/holder + solid         -> collision
holder + fixture             -> collision
machine + machine            -> collision
```

Do not entangle Phase-5 cutting with future collision logic.

## 22. Explicit non-goals

Not required in Phase 5:

- cutting forces
- spindle load/torque
- chip physics
- heat
- tool deflection
- wear
- chatter
- realistic surface roughness
- fixture modelling
- holder/spindle/machine collision
- stock deformation
- coolant
- automatic G54/G55 synchronization
- full A-axis material rotation
- sub-voxel scalar surfaces

## 23. Protocol integrity

Do not change:

- original Stepper-Ninja packet format
- packet sizes
- checksum algorithm
- copied vendor/third-party protocol code
- HAL protocol semantics

Existing integrity/hash checks must continue to pass.

## 24. Documentation

Update/add as appropriate:

```text
README.md
docs/repository.md
docs/phase5-design.md
docs/phase5-test.md
```

Document:

- tool reference convention
- sweep mathematics
- voxel inclusion rule
- threading/queue design
- chunk invalidation
- reset semantics
- accuracy limitations
- diagnostics
- real LinuxCNC acceptance procedure

## 25. Real LinuxCNC acceptance

After automated tests, perform interactive acceptance through the proven path:

```text
LinuxCNC
 -> original HAL
 -> UDP
 -> cnc-sim
 -> material worker
 -> sparse voxel state
 -> remeshing
 -> OpenGL
```

Acceptance must demonstrate:

1. homing with material removal disabled;
2. safe positioning above stock;
3. deliberate material enable;
4. plunge/cut into stock;
5. continuous visible groove;
6. tool/groove agreement with G53;
7. persistent removed material;
8. no protocol/checksum/gap errors caused by material simulation;
9. usable renderer;
10. no lost material-motion segments/backlog failure.

Document the real acceptance separately after implementation.

## 26. Completion criteria

Phase 5 is complete when:

- flat-end tool configuration works;
- continuous swept-volume removal works;
- actual integrated LinuxCNC motion drives removal;
- sample-spacing gaps do not occur;
- Solid chunks materialize only when needed;
- removed voxels persist;
- changed chunks/neighbours remesh correctly;
- OpenGL progressively displays the cut;
- UDP remains decoupled from expensive work;
- required motion segments are never silently dropped;
- headless material tests pass;
- graphical tests pass;
- previous tests remain green;
- protocol/vendor integrity remains intact;
- quantitative tests pass within documented voxel tolerance;
- real LinuxCNC acceptance produces a continuous cut;
- design, mathematics and limitations are documented.

## 27. Phase 5 milestone

```text
Phase 4A / 4A.1
Machine + stock visualization
          ↓
Phase 5
Persistent milling-material simulation
```

At the end of Phase 5, LinuxCNC moves the virtual tool and `linuxcnc-sim` continuously and permanently removes the material swept by its cutting geometry.

This is the first phase in which the project behaves as an actual milling-material simulator rather than only a machine-motion visualizer.
