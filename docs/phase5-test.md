# Phase 5 validation and LinuxCNC acceptance

Real LinuxCNC Phase-5 acceptance: **NOT YET PERFORMED**.
The automated tests below were run locally on 2026-09-28. They use original
Stepper-Ninja datagrams and OpenGL but do not replace an interactive LinuxCNC run.

## Reproduce automated validation

```bash
cd ~/dev/linuxcnc-sim
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCNC_SIM_RENDER=ON -DCNC_SIM_RENDER_TESTS=ON
cmake --build build -j4
ctest --test-dir build --output-on-failure

cmake -S . -B build-headless -DCMAKE_BUILD_TYPE=Release \
  -DCNC_SIM_RENDER=OFF -DCNC_SIM_RENDER_TESTS=OFF
cmake --build build-headless -j4
env -u DISPLAY -u WAYLAND_DISPLAY \
  ctest --test-dir build-headless --output-on-failure
python3 tests/vendor_integrity.py
ldd build-headless/cnc-sim
```

Graphics tests use the available desktop, OpenGL 4.6 Core, Mesa 25.0.7/radeonsi
on AMD Radeon Graphics. GCC 14.2.0, C++20. The headless binary links neither
OpenGL nor GLFW. Both suites include the existing unprivileged network-namespace
test; it ran successfully, without skipping or changing the host network.

Additional ASan/UBSan Debug validation (material and related volume/snapshot tests):

```bash
cmake -S . -B build-sanitize -DCNC_SIM_RENDER=OFF -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie' \
  -DCMAKE_C_FLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined -no-pie'
cmake --build build-sanitize -j4
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build-sanitize \
  -R 'material-|phase4a-|workpiece-(geometry|transactions|runtime)' --output-on-failure
```

## Results

| Validation | Result |
|---|---|
| Graphics Release, full CTest suite | 35/35 passed, no skips |
| Headless Release, full CTest suite | 32/32 passed, no skips |
| ASan + UBSan + leak detection, selected Debug suite | 15/15 passed, no sanitizer reports |
| Original protocol/HAL tests | passed in both full suites |
| Vendor integrity | all nine pinned SHA-256 files unchanged and equal to originals |
| Original `stepper-ninja` checkout | clean; no vendor or protocol source edits |
| Real LinuxCNC Phase-5 acceptance | **NOT YET PERFORMED** |

New deterministic coverage:

- Static stamp, vertical plunge, Solid -> Mixed, Empty compaction, untouched cells,
  partial boundary initialization, per-chunk/global versions and idempotent recuts.
- Horizontal, vertical, simultaneous XYZ, reversed, tiny and zero-length motion.
  One long sweep equals 127 shorter collinear sweeps exactly. Seeded random
  diagonals also match 19 subdivisions. Analytic XYZ tests check the coupling of
  Z-time clipping and XY projection, not merely a 2D capsule.
- A legacy rotated stock broad phase agrees with exhaustive voxel-centre inclusion.
  Finite long-segment inclusion and out-of-stock non-removal are checked.
- X/Y/Z chunk-boundary dirty propagation; a cut changes the visible face of an
  otherwise unchanged Solid neighbour. Meshed area matches exhaustive exposed
  voxel-face area; continuous grooves cross the 32-cell boundary without gaps.
- 200000 numbered FIFO events are consumed in order. A deliberately delayed
  consumer begins with 10000 queued events; no events are overwritten or lost.
  The worker test processes a bent path with 2000 segments, then queued capture,
  reset, tool change and OFF movement, comparing exact final occupancy.
- Runtime test sends original UDP XYZ packets and compares the resulting material
  with the expected path. ON stamps only the current pose after disabled travel.
  Reset preserves configuration/machine pose, immutable snapshots remain unchanged,
  and invalid commands cannot change tool/material configuration.
- Failure test injects an unsupported material pose, verifies explicit incomplete
  state and failed pending diagnostic capture, then joins cleanly.
- Both headless and graphics black-box tests send 1000 original packets at nominal
  1 kHz while cutting, check 1001 sweeps including the enable stamp, exactly
  43160 removed voxels, and zero invalid/send/gap errors. Shutdown drains the
  queue and dirty meshes. This is an automated UDP test, not LinuxCNC acceptance.
- Framebuffer test detects a visible cut, verifies repeated rendering preserves
  it and checks reset restores the original pixels. Existing camera, tool movement,
  workpiece reconfiguration and concurrent UDP/recorder graphics tests remain.

Quantitative 0.10-mm test: stock 30 x 20 x 10 mm, tool diameter 6 mm, X centre
travel 20 mm, depth 2 mm. Counts were **6000000 -> 5703440** material voxels,
**296560 removed**, equivalent to **296.560 mm3**. The analytic capsule-prism
volume `(20*6 + pi*3^2)*2` is **296.548668 mm3**. The accepted bound is
**8.354 mm3**, derived from a 2D half-cell diagonal boundary strip and exact
grid-aligned depth. Measured agreement does not imply CAD accuracy.

One Graphics Release suite run measured:

| 1000-exchange case | Median RTT | p99 RTT | Max RTT |
|---|---:|---:|---:|
| Material UDP/CLI without rendering | 0.104 ms | 0.223 ms | 0.315 ms |
| Material UDP/CLI with rendering | 0.097 ms | 0.213 ms | 1.135 ms |
| Existing renderer reconfiguration + recorder | 0.083 ms | 0.166 ms | 0.341 ms |

Material queue maxima in those first two tests were 24 and 14 events; the worker
backlog test observed 1990. These are run-dependent diagnostics, not asserted
latency limits or real-time guarantees. No wire-motion recovery is claimed for
packets that never reach the simulator. Detailed logs are local ignored artifacts
under `build*/Testing/Temporary/LastTest.log` and `build*/phase5-ctest.log`.

## Manual LinuxCNC acceptance — exact procedure, not yet executed

Use the existing Phase-3 configuration and original installed HAL driver.
Begin with a newly started simulator so the existing zero-step reference and
LinuxCNC homing sequence agree. The known Phase-4A.1 start path is retained.

Terminal 1, project directory:

```bash
cd ~/dev/linuxcnc-sim
sudo ./scripts/setup-veth.sh
sudo ip netns exec cnc-sim-ns \
  runuser -u "$USER" -- env \
  DISPLAY="$DISPLAY" \
  XAUTHORITY="${XAUTHORITY:-$HOME/.Xauthority}" \
  XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}" \
  ./build/cnc-sim \
    --render \
    --steps-per-unit 400,400,400,400 \
    --io-config examples/phase3/virtual-io.conf
```

In the simulator console, before homing:

```text
material off
probe off
input clear
workpiece origin min min min
workpiece size 30 20 10
workpiece position 15 10 -10
workpiece origin center center max
workpiece voxel 0.10
tool flat-end 6 20
workpiece show
tool show
material show
```

The preparatory origin command makes this valid even after a previous larger
origin configuration. Expected stock bounds: X 0..30, Y 0..20, Z -20..-10 mm.
Check Material OFF and zero queue/dirty chunks. Probe stays disabled for this test.

Terminal 2:

```bash
cd ~/dev/linuxcnc-sim
linuxcnc examples/phase3/phase3.ini
```

In LinuxCNC AXIS: release E-stop, machine ON, **Home All**. Wait for all axes to
finish; verify X/Y/Z=0 in LinuxCNC and in simulator step/mm displays. Material
must remain OFF throughout homing. With MDI, enter separately and await completion:

```gcode
G21 G90 G40 G49 G80
G53 G1 Z-5 F300
G53 G1 X5 Y10 F300
```

Expected simulator pose: X=2000, Y=4000, Z=-2000 steps, i.e. G53 (5,10,-5) mm.
The tip is 5 mm above the stock top. In simulator:

```text
material reset
material on
material show
```

Wait until `material show` reports ON, queue 0 and dirty chunks 0. Then in LinuxCNC
MDI, one line at a time, wait for each move:

```gcode
G53 G1 Z-12 F120
G53 G1 X25 F300
G53 G1 Z-5 F300
```

Observe the plunge and progressive ~6-mm-wide, ~2-mm-deep groove from X5 to X25,
Y10. Expected pose after the X cut: (10000,4000,-4800) steps. After retract:
(10000,4000,-2000). All moves lie within the example's existing soft limits.
No spindle command is needed for this phase: cutting is explicitly gated by
`material on`, not by inferred G-code/spindle state.

Simulator:

```text
material off
material show
status
```

Wait for stable removal counters, queue 0 and dirty chunks 0. Record all packet
and material counters (including maximum queue depth). Expected: persistent groove,
no gap islands, RX=accepted=TX, all protocol/send/gap error counters zero. Orbit,
pan, zoom and Home/Fit must remain usable while cutting and preserve the material.
The final counts should agree with the quantitative test within the documented
voxel semantics when the actual path matches these moves. Actual path/sample
quantization is authoritative.

Optional repeat-cut check: while OFF, return above X5, then enable and repeat the
three cutting moves; after drain, removed count should not increase for the same
path. To demonstrate disabled positioning without material changes, keep OFF and
move elsewhere. Finally, with OFF and the tool above stock:

```text
material reset
material show
```

The original configured stock must reappear, removed voxels return to zero, and
machine position/tool definition remain unchanged. Capture the observed results
in a separate real acceptance entry; until then retain **NOT YET PERFORMED**.

After closing LinuxCNC and quitting the simulator:

```bash
cd ~/dev/linuxcnc-sim
sudo ./scripts/teardown-veth.sh
```
