# Phase 5 validation and LinuxCNC acceptance

Real LinuxCNC Phase-5 acceptance: **NOT YET PASSED**.
The first real run proved geometric correctness and lossless eventual processing,
but failed the desired progressive performance. Automated optimization checks
below do not grant real acceptance. The same test must be repeated manually.

## First real run (operator-supplied baseline, before optimization)

Workpiece 30 x 20 x 10 mm, position (15,10,-10), origin center/center/max,
voxel 0.10 mm: machine bounds X 0..30, Y 0..20, Z -20..-10.
Tool flat-end 6 x 20 mm. Start (5,10,-5), plunge Z-12 F120, X25 F300.

| Counter | After plunge | After horizontal groove |
|---|---:|---:|
| queue max | 206 | 3235 |
| sweeps | 2773 | 6820 |
| events processed | not supplied | 40584 |
| chunks tested / changed | 12852 / 140 | 82884 / 14314 |
| voxels tested | 9653732 | 70125744 |
| voxels removed | 56560 | 296560 |
| removed mm3 | 56.560 | 296.560 |
| mesh rebuilds | not supplied | 6118 |
| material processing ms | 932.009 | 9337.319 |
| meshing ms | 2425.898 | 14954.038 |

After final drain: motion queue=0, dirty meshes=0, lag=0. During X motion the
visible groove trailed badly and caught up after the tool stopped. Nevertheless
RX=accepted=TX=1203003; invalid, length, checksum, timing, send errors, position
overflows and packet-ID gaps were all zero. Thus the issue was downstream work,
not missing motion or a reason to block UDP.

The discrete fingerprint is **296560 voxels / 296.560 mm3**, analytic comparison
**296.548668 mm3**. The automated quantitative test now explicitly asserts the
exact fingerprint in addition to its existing voxel-derived analytic bound.

## Automated validation (2026-09-28)

| Validation | Result |
|---|---|
| Graphics Release, complete suite | 41/41 passed, no skips |
| Headless Release, complete suite | 38/38 passed, no skips |
| ASan/UBSan Debug, complete headless suite, leak detection | 38/38 passed, no reports |
| ThreadSanitizer Debug, material/volume/workpiece selection | 21/21 passed, no reports |
| Vendor integrity | nine pinned SHA-256 files match originals |
| Protocol, original HAL, UDP, namespace tests | passed; namespace test not skipped |
| Original stepper-ninja checkout | clean; no protocol/vendor edits |
| Real LinuxCNC acceptance after optimization | **NOT YET PASSED** |

GCC 14.2.0 / C++20, Linux, 14 available logical CPUs; automatic mesh count 4.
Graphics tests used the available desktop (OpenGL 4.6 Core, Mesa 25.0.7/radeonsi,
AMD Radeon Graphics). Headless binary links no OpenGL/GLFW.
The graphics-suite UDP material runs reported 47 sweeps for 1000 motion events:
headless queue max 5, RTT median/p99/max 0.046/0.123/1.331 ms; graphics queue max 3,
RTT 0.062/0.158/2.924 ms. These are diagnostics, not real-time guarantees.
The original UDP/CLI material tests (with and without graphics) still require
1000 received/accepted/replied packets, 43160 removed voxels and zero invalid,
length/checksum/timing/send/overflow/gap errors. Their old **1001 sweeps** assertion
was intentionally replaced by exactly **1000 motion events**, fewer than 250
sweeps, and the unchanged exact occupancy assertion: fewer sweeps are the feature,
not a relaxed geometry or protocol tolerance. No test was disabled.

New coverage: all 1/20/200/4000 variants compare every final voxel with the coarse
reference; exact XYZ diagonals and negative direction; static events; reversals;
90-degree corners; arc-like polylines; chunk boundaries; one-ULP
near-collinearity and equal rounded products with different exact residuals.
Concurrency tests cover 1/2/4 mesh threads, repeated reset and workpiece generations,
Capture barriers, simultaneous status/mesh readers, snapshot independence while
material mutates, and exact final mesh indices/positions/normals versus serial
meshing of the final volume. Existing X/Y/Z halo-only invalidation and exhaustive
exposed-face-area checks remain active.

## Reproduce builds, tests and sanitizers

```bash
cd ~/dev/linuxcnc-sim
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCNC_SIM_RENDER=ON -DCNC_SIM_RENDER_TESTS=ON
cmake --build build -j4
ctest --test-dir build --output-on-failure

cmake -S . -B build-headless -DCMAKE_BUILD_TYPE=Release \
  -DCNC_SIM_RENDER=OFF -DCNC_SIM_RENDER_TESTS=OFF
cmake --build build-headless -j4
env -u DISPLAY -u WAYLAND_DISPLAY ctest --test-dir build-headless --output-on-failure
python3 tests/vendor_integrity.py
ldd build-headless/cnc-sim

cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug \
  -DCNC_SIM_RENDER=OFF -DCNC_SIM_RENDER_TESTS=OFF \
  -DCMAKE_CXX_FLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie' \
  -DCMAKE_C_FLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined -no-pie'
cmake --build build-sanitize -j4
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build-sanitize --output-on-failure

cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DCNC_SIM_RENDER=OFF \
  -DCMAKE_CXX_FLAGS='-O1 -g -fsanitize=thread -fno-omit-frame-pointer -fno-pie' \
  -DCMAKE_C_FLAGS='-O1 -g -fsanitize=thread -fno-omit-frame-pointer -fno-pie' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=thread -no-pie'
cmake --build build-tsan -j4
TSAN_OPTIONS=halt_on_error=1 ctest --test-dir build-tsan \
  -R 'material-|phase4a-|workpiece-(geometry|transactions|runtime)' --output-on-failure
```

## Measurements and benchmark interpretation

Benchmark A uses the same stock, tool, plunge and horizontal path as above.
It sends one plunge segment, N horizontal segments, Enable and a final Capture.
Thus N+1 motion events and N+3 total events are processed. Capture copies the
final volume within wall time; exhaustive reference comparison runs outside timing.
All measured variants remove **296560 voxels** and compare equal voxel-for-voxel.
One segment includes both endpoint cylinders; no step/sample dropping is involved.
The synthetic benchmark is not a LinuxCNC trajectory replay: its plunge is one
segment and its optional horizontal pacing is a nominal 1 kHz stream.

Baseline executable was rebuilt from unchanged 34fd135 in Release using the same
compiler. The benchmark can also be built against its original headers/library
with `CNC_BENCH_BASELINE`. Final comparisons ran sequentially, after builds/tests
finished. Times are single-run diagnostics, not statistical latency guarantees.
Workload/geometry are deterministic; concurrent queue maxima, collection-window
boundaries, stale job counts and timings depend on scheduling. Offline coalescer
tests additionally assert exact sweep counts independent of scheduling.

### Staged checkpoints

| Stage | 4000-segment sweeps | Voxel tests | Material ms | Mesh ms | Wall ms |
|---|---:|---:|---:|---:|---:|
| Unchanged initial baseline | 4002 | 59715244 | 7861.937 | 9342.840 | 17245.750 |
| Instrumentation only | 4002 | 59715244 | 8782.922 | 10141.325 | 18968.760 |
| A: exact coalescing | 4 | 380230 | 42.507 | 400.634 | 457.451 |
| B: chunk invalidation | 3 | 365288 | 32.587 | 374.316 | 420.056 |
| C: detached meshing, one mesh thread | 3 | 365288 | 32.191 | 385.686 | 432.048 |
| D: four mesh threads | 3 | 365288 | 32.124 | 379.876 | 142.224 |

Instrumentation-only and A-D targeted suites all passed after their respective
stages (12 tests initially, 13 after coalescing). Instrumentation collects chunk
hits before mutation to separate timings, adding temporary storage/work; measured
instrumentation-only cost was visible. There are no per-voxel clock calls.
B removes repeated dirty set work. C does not inherently reduce isolated burst
wall time with one mesh thread; its purpose is ongoing material progress while
meshing runs. D reduces mesh wall time through actual parallel jobs. Mesh ms after
C is the sum of job elapsed times, not a single critical-path or CPU measurement.

### Final same-machine burst comparisons

| N | Mode | Motion / coalesced | Sweeps | Voxels tested | Chunks tested / changed | Queue max | Rebuilds | Material ms | Mesh ms | Wall ms |
|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 34fd135 | 2 / 2 | 3 | 365288 | 72 / 62 | 4 | 280 | 35.242 | 383.809 | 419.579 |
| 1 | A-D / 4 mesh | 2 / 2 | 3 | 365288 | 72 / 62 | 4 | 280 | 33.924 | 381.413 | 132.704 |
| 20 | 34fd135 | 21 / 21 | 22 | 657484 | 408 / 228 | 23 | 336 | 72.695 | 464.803 | 538.368 |
| 20 | A-D / 4 mesh | 21 / 2 | 3 | 365288 | 72 / 62 | 23 | 280 | 33.780 | 395.481 | 136.465 |
| 200 | 34fd135 | 201 / 201 | 202 | 3454484 | 3546 / 1804 | 201 | 502 | 431.354 | 871.652 | 1305.564 |
| 200 | A-D / 4 mesh | 201 / 2 | 3 | 365288 | 72 / 62 | 200 | 280 | 33.962 | 394.228 | 137.182 |
| 4000 | 34fd135 | 4001 / 4001 | 4002 | 59715244 | 69210 / 13968 | 3998 | 3608 | 7785.639 | 9120.385 | 16940.503 |
| 4000 | A-D / 4 mesh | 4001 / 2 | 3 | 365288 | 72 / 62 | 2124 | 280 | 33.834 | 381.052 | 143.974 |

Final removed voxels=296560 in every row. Motion and mesh queues drain to zero.
Optimized mesh queue maximum=280 in all these runs. The old implementation did
not expose mesh queue max; its initial deduplicated dirty set contains 280 chunks
(the entire stock). The original single material thread also performed meshing;
optimized runs use one material owner plus four mesh threads (observed peak 4).
A burst necessarily creates queue backlog even with a fast consumer; the paced
comparison below is the useful queue/lag comparison for the live use case.

### Nominal 1 kHz, four-second horizontal move

| Counter | 34fd135 | Optimized (1 material + 4 mesh) |
|---|---:|---:|
| Motion events | 4001 | 4001 |
| After coalescing | 4001 | 173 |
| Total events | 4003 | 4003 |
| Sweeps | 4002 | 174 |
| Voxels tested | 59715244 | 2897328 |
| Voxels removed | 296560 | 296560 |
| Chunks tested | 69210 | 3030 |
| Chunks changed | 13968 | 1540 |
| Motion queue max | 3130 | 7 |
| Mesh queue max | not measured | 280 |
| Mesh rebuilds | 3594 | 1933 |
| Material processing ms | 7811.763 | 484.980 |
| Mesh build sum ms | 9097.553 | 5017.047 |
| Wall ms, including 4 s motion | 16944.626 | 4017.055 |

Optimized detailed times: broad 0.437 ms, narrow 449.924 ms,
mutation 14.669 ms, dirty invalidation 5.471 ms,
coalescing/dequeue 59.744 ms, snapshot 22.266 ms,
publication 4.666 ms; 71 stale jobs discarded,
parallel build peak 4. Completion about 17.055 ms after
nominal motion end, versus about 12944.626 ms in the baseline.
This measures worker completion, not screen refresh or LinuxCNC performance.

Material work is now roughly 12.1% of the four-second movement duration;
snapshot copying is a small part of it. Stage E is therefore deliberately omitted.
Chunk-parallel mutation would need safe map partitioning/preallocation and ordered
reduction of counts, versions and dirty results. Large diagonal bounding boxes,
high-curvature polylines or much larger cuts may justify that work later.

### Mesh worker comparison (4000-event burst)

| Mesh workers | Observed parallel peak | Sweeps | Material ms | Mesh sum ms | Wall ms |
|---:|---:|---:|---:|---:|---:|
| 1 | 1 | 3 | 32.389 | 378.367 | 426.359 |
| 2 | 2 | 4 | 35.500 | 440.872 | 271.366 |
| 4 | 4 | 3 | 33.556 | 384.767 | 146.356 |

The two-worker run split the collection window once, producing four sweeps and
35 stale jobs; material geometry was still identical. Thread count alone is not
a speedup promise, and the automatic cap intentionally avoids using every CPU.

### Deterministic path tests B-E

At h=0.5 mm with a 2 x 3 mm tool, 4000-segment paths compare every voxel before
and after offline exact coalescing:

| Path | Sweeps before / after | Voxel tests before / after | Removed |
|---|---:|---:|---:|
| Straight crossing a chunk boundary (E) | 4000 / 1 | 568956 / 1008 | 456 |
| Exact diagonal XYZ (B) | 4000 / 1 | 286850 / 5733 | 596 |
| 90-degree corner (C) | 4000 / 2 | 475990 / 1036 | 450 |
| Arc-like polyline (D) | 4000 / 4000 | 385766 / 385766 | 1164 |
| Direction reversal | 4000 / 2 | 532020 / 984 | 264 |

Arc-like motion intentionally receives no approximate speedup. Tests also retain
finer 0.25-mm X/Y/Z chunk-halo checks. Source and commands:

```bash
for n in 1 20 200 4000; do ./build-headless/material-benchmark "$n" 4; done
for w in 1 2 4; do ./build-headless/material-benchmark 4000 "$w"; done
./build-headless/material-benchmark 4000 4 1000
./build-headless/material-tests coalescing
./build-headless/material-concurrency
```

To reproduce the old executable without touching history or the working checkout:

```bash
mkdir -p build-perf/reference
git archive 34fd135 | tar -x -C build-perf/reference
cmake -S build-perf/reference -B build-perf/reference-build \
  -DCMAKE_BUILD_TYPE=Release -DCNC_SIM_RENDER=OFF -DBUILD_TESTING=OFF
cmake --build build-perf/reference-build -j4
c++ -std=c++20 -O3 -DCNC_BENCH_BASELINE \
  -Ibuild-perf/reference/src \
  -isystem build-perf/reference/third_party/stepper-ninja/firmware/inc \
  -isystem build-perf/reference/third_party/stepper-ninja/firmware/modules/inc \
  tests/material_benchmark.cpp build-perf/reference-build/libcnc_sim_core.a \
  build-perf/reference-build/libstepper_ninja_original.a -pthread \
  -o build-perf/reference-benchmark
./build-perf/reference-benchmark 4000
./build-perf/reference-benchmark 4000 1 1000
```

Known limits: deep snapshot copies scale with stored cut material; whole-directory
coherence may delay rendering when ongoing cuts outpace meshing; a large sweep
remains indivisible; exact conservative coalescing deliberately rejects uncertain
floating-point diagonals. Queue allocation remains unbounded/lossless and can
fail fatally under unlimited overload. No real-time promise or real acceptance
is inferred from these measurements. Logs remain ignored under `build-perf/` and
`build*/Testing/Temporary/LastTest.log`.

## Repeat real LinuxCNC acceptance — exact procedure

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
    --material-workers 1 --mesh-workers 4 \
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
For this exact path require **296560 removed voxels / 296.560 mm3** after drain.
After the plunge alone require **56560 / 56.560 mm3**. Record `material show` and
`status` after plunge and horizontal movement separately, before retracting.
Compare motion/coalesced/sweep counts, voxels tested, queue max, mesh queue max,
all timing categories and observed visual lag with the first real run below.
The groove should follow the moving tool much more closely. Material snapshots
can lag the actual tool; record any visible catch-up after X25 explicitly.

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
in a separate real acceptance entry; until the operator confirms a successful repeat retain **NOT YET PASSED**.

After closing LinuxCNC and quitting the simulator:

```bash
cd ~/dev/linuxcnc-sim
sudo ./scripts/teardown-veth.sh
```
