# Phase 5 validation and LinuxCNC acceptance

Real LinuxCNC Phase-5 acceptance: **NOT YET PASSED**.
The first real run proved geometric correctness and lossless eventual processing,
but failed the desired progressive performance. Automated optimization checks
below do not grant real acceptance. The same test must be repeated manually.

## Batch Sweep Union validation (2026-09-28/29, parent 9e56ffe)

Work began with a clean `git status --short` and verified HEAD
`9e56ffeeb5725b4c517917ccd525b70a23942272` (`Add curved material removal baseline`).
The seven-entry log was inspected before changes. The frozen `benchmark paced 4`
was rerun **before implementation**, followed by all eight existing curved tests:
8/8 passed in 28.27 s. The measured pre-change worker/queue values were
3606.683 ms / 429; all six million cells matched the frozen references.
The old executable was retained in the ignored `build-batch/` directory.

The fixtures `arc.steps`, both RLE references, `serial.stats`, fixture README,
reference comparator and exact coalescing predicate have no diff against 9e56ffe.
No reference was regenerated. Existing geometry/work-count assertions are intact;
the curved test executable only gains additional diagnostics and an optional batch
interval argument. The original `benchmark paced 4` command still exercises the
same motion, fresh-stock runs, 1-ms delivery and four meshworkers. Optional fifth
argument `0` selects the old segmentwise worker for direct comparisons.

### Exact geometry and boundary coverage

Every window-size test compares **each of 6000000 cells**, separately for the
worker and for the union API with all original edges retained. Worker/canonical
batch results also have identical final global and per-chunk versions.
The source timestamps in these deterministic tests are explicit future-dated
millisecond ticks, followed by an immediate Capture, avoiding scheduling-dependent
idle splits. They test long motion and boundaries inside the curved path.

| Window ms | Batches | Coalesced segments including plunge | Max segments | Exact result |
|---:|---:|---:|---:|---|
| 1 | 3143 | 3143 | 1 | 166400 removed, all cells identical |
| 5 | 629 | 2683 | 5 | 166400 removed, all cells identical |
| 10 | 315 | 2629 | 10 | 166400 removed, all cells identical |
| 20 | 158 | 2600 | 20 | 166400 removed, all cells identical |
| 50 | 63 | 2587 | 50 | 166400 removed, all cells identical |

All also pass plunge-only: **56560 / 56.560 mm3**, FNV-1a-64
**95c98093e6e51d52**. Plunge+arc is **166400 / 166.400 mm3**, fingerprint
**585ccf7e9d0561a2**; arc-only delta **109840 / 109.840 mm3**. Hashes are
additional diagnostics, never a substitute for cell equality. The unchanged
frozen serial work counts pass as well.

Additional tests cover the exact **4534-edge** alternative subdivision twice,
idempotent repeated unions, the frozen **78560** differing cells between arc and
chord, and the original **296560** straight-groove result. L corners, out-and-back
motion and a tight curve match sequential subtraction; corner/curve also explicitly
differ from their endpoint chord. An exhaustive analytic centre oracle covers
rotated stock, randomized diagonal XYZ paths, large translations (1e11 mm), tangent
boundaries and partial chunks. X/Y/Z halo-only invalidation, bulk duplicate indices,
empty compaction, invalid-index rejection before mutation and metadata equality
are checked independently.

Worker tests exercise 0/19/20-ms endpoint timestamps (an exact half-open boundary),
backwards timestamps, a repeated timestamp with 600 non-collinear edges (256-edge
cap), Capture before deadline, OFF with pending geometry/disabled travel, re-enable,
tool replacement while a previously uncut edge is pending, Reset, workpiece and
generation replacement, and final mesh generation/material version. Shutdown
flushes even a future-dated open window without a Capture. Live short movement
flushes after its 20-ms deadline without another event. Already-expired source
timestamps for 1/20/1000 ms flush without waiting another worker-local interval.
The fatal-path regression keeps the producer's Capture promise alive while an
out-of-range pending batch fails: the consumer must fail that already-dequeued
Capture explicitly, not leave its future blocked.
CLI rejects negative, nonintegral, nonnumeric and >1000 values, accepts 0/1000,
and reports the default 20 and batch metrics in the real UDP integration test.

### Final complete test runs (2026-09-29)

| Suite | Result | Elapsed s |
|---|---|---:|
| Graphics Release | 61/61, no skips | 76.78 |
| Headless Release, DISPLAY/WAYLAND_DISPLAY unset | 58/58, no skips | 66.96 |
| ASan/UBSan + leak detection, complete headless suite | 58/58, no skips | 323.17 |
| TSan, complete headless suite | 58/58, no skips | 268.38 |

No ASan/UBSan/TSan report in these final full runs. All builds are warning-free.
The complete graphics suite includes all three renderer tests. All four suites
include protocol-unit, original-hal-inputs, UDP, private network namespace and
vendor integrity tests. The explicit SHA-256 manifest check passes **9/9**;
vendored files equal the original clean stepper-ninja checkout. Frozen fixtures,
VoxelReference, coalescer and analytic predicate are byte-identical to 9e56ffe.

### Final frozen curved benchmark: segmentwise versus 20 ms

GCC 14.2 Release, same headless executable, one material owner and four meshworkers.
Three consecutive pairs were run after all build/test/sanitizer workloads finished:
`benchmark paced 4 0` then the unchanged `benchmark paced 4` command. Each command
starts fresh raw stock separately for plunge-only and plunge+arc; it checks the
unchanged full-cell reference outside the timed region. Wall time includes
1-ms-paced input delivery, Capture and final material/mesh drain. Source timestamps
are actual existing event creation times, so OS pacing changes window/coalescing
counts slightly. No hard performance threshold is asserted.

The following table reports the **first complete pair**, without selecting a best
timing. The pre-implementation 9e56ffe rerun is retained as a third comparison.

| Plunge + arc metric | 9e56ffe before editing | Current 0 ms reference | Current 20 ms union |
|---|---:|---:|---:|
| Motion events | 3143 | 3143 | 3143 |
| After exact coalescing | 2579 | 2577 | 2599 |
| Logical sweeps including Enable | 2580 | 2578 | 2600 |
| All events including Enable/Capture | 3145 | 3145 | 3145 |
| Removed voxels | 166400 | 166400 | 166400 |
| Removed mm3 | 166.400 | 166.400 | 166.400 |
| FNV-1a-64 | 585ccf7e9d0561a2 | 585ccf7e9d0561a2 | 585ccf7e9d0561a2 |
| Batch calls | — | 0 | 154 |
| Segments in batches | — | 0 | 2599 |
| Average segments/batch | — | 0.000 | 16.877 |
| Maximum segments/batch | — | 0 | 21 |
| Batch candidate chunks | — | 0 | 1488 |
| Batch candidate voxel visits, includes empty | — | 0 | 7414562 |
| Occupied voxel visits / voxels tested | 23003452 | 22973331 | 1625966 |
| Chunk-segment references | — | 0 | 24180 |
| Analytic containment calls | 23003452 | 22973331 | 179600 |
| Batch envelope-rejected occupied centres | — | 0 | 1440266 |
| Prefilter-skipped segment references | — | 0 | 24397284 |
| First-hit exits | — | 0 | 166400 |
| Remaining references skipped after first hit | — | 0 | 908280 |
| Chunks tested | 23978 | 23966 | 1488 |
| Chunks changed | 5834 | 5824 | 752 |
| Broad phase ms | 4.495 | 4.512 | 3.041 |
| Narrow phase ms | 3414.927 | 3189.694 | 245.313 |
| Mutation ms | 39.655 | 40.003 | 2.953 |
| Invalidation ms | 34.884 | 35.324 | 2.270 |
| Batch processing ms | — | 0.000 | 260.563 |
| Material worker ms | 3606.683 | 3382.818 | 265.489 |
| Coalescing/dequeue ms | 24.493 | 24.745 | 58.183 |
| Snapshot ms | 19.017 | 8.040 | 6.448 |
| Publication ms | 0.294 | 0.290 | 2.466 |
| Motion queue maximum | 429 | 370 | 4 |
| Mesh queue maximum | 280 | 280 | 280 |
| Mesh jobs/rebuilds | 2245 | 2225 | 1134 |
| Stale mesh jobs | 1945 | 1925 | 112 |
| Accepted mesh jobs | 300 | 300 | 1022 |
| Meshing ms, summed elapsed jobs | 5310.312 | 5115.528 | 2661.828 |
| Final worker lag ms | 0.000 | 0.000 | 0.000 |
| Wall ms | 3716.696 | 3528.128 | 3153.811 |

Zero batch-specific counters in reference mode mean that no union path ran;
they do not mean the segmentwise engine had no candidates. Batch candidate voxel
visits include already empty cells; `voxels_tested` counts occupied centres.
One containment call means one invocation of the unchanged analytic predicate.

- Occupied voxel visits: 92.92% lower versus the current segmentwise reference.
- Analytic containment calls: 99.22% lower versus the current segmentwise reference.
- Narrow-phase elapsed time: 92.31% lower versus the current segmentwise reference.
- Material-worker elapsed time: 92.15% lower versus the current segmentwise reference.
- Motion queue maximum: 98.92% lower versus the current segmentwise reference.
- Stale mesh jobs: 94.18% lower versus the current segmentwise reference.
- Wall elapsed time: 10.61% lower versus the current segmentwise reference.

The measured analytic-call reduction is **22793731** calls in this pair.
The union separately skips **24397284** references through
prefilters and **908280** remaining references after the
first hit. Those counters describe a hypothetical blind union loop, **not**
additional savings that can be added to the measured old/new call difference.

| Repeat | Interval ms | Worker ms | Queue max | Stale jobs | Wall ms |
|---:|---:|---:|---:|---:|---:|
| 1 | 0 | 3382.818 | 370 | 1925 | 3528.128 |
| 1 | 20 | 265.489 | 4 | 112 | 3153.811 |
| 2 | 0 | 3324.647 | 355 | 1866 | 3476.782 |
| 2 | 20 | 271.849 | 4 | 103 | 3160.437 |
| 3 | 0 | 3300.442 | 317 | 1941 | 3479.516 |
| 3 | 20 | 262.701 | 5 | 93 | 3154.297 |

Every repeat preserves **166400 / 166.400 mm3**, fingerprint
**585ccf7e9d0561a2**, and zero differing cells. Every separate plunge preserves
**56560 / 56.560 mm3**, fingerprint **95c98093e6e51d52**. All end with motion
and mesh queues, dirty chunks and lag zero; four meshworkers are observed.
The first-pair plunge details are:

| Plunge metric | 0 ms reference | 20 ms union |
|---|---:|---:|
| Worker ms | 6.134 | 4.743 |
| Narrow ms | 3.704 | 3.829 |
| Containment calls | 80724 | 56560 |
| Wall ms | 90.513 | 86.830 |

Compared with the originally supplied 9e56ffe paced report (worker 3656.478 ms,
23020413 voxel/containment tests, queue maximum 490, stale jobs 1910 and wall
3758.683 ms), the first 20-ms run uses 265.489 ms worker time,
1625966 occupied visits / 179600 analytic calls,
queue maximum 4, 112 stale jobs and 3153.811 ms wall.
Wall time approaches the fixed 3142-ms delivery duration, so it cannot fall
in proportion to compute time. Publication time can rise as coherent intermediate
mesh directories become available more often. Mesh scheduling itself is unchanged.

Review found no additional writer of material/versions/dirty state and no new
UDP wait, lock or graphics call. Segment-index references remain within each
union call; deep immutable mesh snapshots retain their lifetime independently.
Versions increase by the actual removed count once per changed chunk, and
six-face halo invalidation uses only actual hits. Generation validation remains
unchanged. The discovered already-dequeued Capture failure path is fixed and
covered by a producer-retained-promise regression.

Remaining limits: loose candidate boxes for long/discontinuous paths, worst-case
candidate-times-segment work, unbounded lossless input backlog, full sparse-volume
snapshot costs and no hard realtime latency guarantee. The 256-segment cap bounds
retained geometry per union, not total volume work or memory. Automated success
does not grant real LinuxCNC acceptance: **NOT YET PASSED**.

### Reproduction

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCNC_SIM_RENDER=ON -DCNC_SIM_RENDER_TESTS=ON
cmake --build build -j 4
ctest --test-dir build --output-on-failure
cmake -S . -B build-headless -DCMAKE_BUILD_TYPE=Release -DCNC_SIM_RENDER=OFF -DCNC_SIM_RENDER_TESTS=OFF
cmake --build build-headless -j 4
ctest --test-dir build-headless --output-on-failure
./build-headless/curved-material-tests benchmark paced 4
./build-headless/curved-material-tests benchmark paced 4 0
ctest --test-dir build-headless -R 'material-batch-|curved-' --output-on-failure
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  ctest --test-dir build-sanitize --output-on-failure
TSAN_OPTIONS=halt_on_error=1 ctest --test-dir build-tsan --output-on-failure
```

Use the sanitizer configurations below (the same non-PIE GCC setup as the frozen
baseline). Full ASan/UBSan and **full TSan headless suites** include every new batch
test and the zero-interval reference test. The new 1-ms batch test originally hit
its own 30-second future wait under instrumentation, with no sanitizer finding.
That completion guard is now 120 seconds; the independent CTest timeout remains
180 seconds. No voxel, fingerprint or frozen work-count assertion was changed.
Benchmarks are measured separately from sanitizer/build workloads; their timings are diagnostics, not pass thresholds.
The real LinuxCNC performance acceptance remains **NOT YET PASSED**.

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

## Curved-motion baseline of ae13f21 — no production optimization

This baseline was captured from
`ae13f21c281d05722fd117ad399afc36db67a03e`, **before the next curved-motion
optimization**. Production MaterialRemoval, MaterialWorker, MotionCoalescer,
SparseVoxelVolume, mesh scheduling, UDP and protocol code are unchanged.
Only tests, frozen reference data, CMake test registration and documentation were
added. There is no sweep union/batching, approximate arc coalescing, new material
parallelism or analytical circular cut. No future optimization is claimed correct
or faster. Real Phase-5 acceptance remains **NOT YET PASSED**.

### Fresh-stock geometry and clockwise direction

Each test/benchmark run independently constructs WorkpieceConfig:

| Setting | Value |
|---|---|
| Size | 30 x 20 x 10 mm |
| G53 position | (15, 10, -10) mm |
| Origin | center / center / max, numerical offset (15, 10, 10) mm |
| Resulting bounds | X 0..30, Y 0..20, Z -20..-10 mm |
| Voxel/chunk size | 0.10 mm / 32 cubed |
| Grid | 300 x 200 x 100 = 6000000 voxels; 280 chunks |
| Tool | flat-end, diameter 6 mm, cutting length 20 mm, +machine Z |
| Enable position | (15, 4, -5) mm, entirely above stock |
| Plunge | (15, 4, -5) -> (15, 4, -12) mm |
| Arc | (15, 4, -12) -> (5, 4, -12), centre (10, 4), R5, XY/G17, clockwise/G2 |

There is no preceding groove or other cut. Enable contributes a no-removal static
sweep. The plunge is deliberately **one segment**, with no specified plunge-feed
or acceleration model. Case A stops after it. Case B applies that same plunge
and then only the arc. No retract, reset or tool-change sweep is added.

`G2 X5 Y4 I-5 J0` from the rightmost point describes the **lower** semicircle.
The tool centre reaches Y=-1 mm, outside the stock's Y>=0 region. The current
volume clips removal naturally; the path is not reflected to the upper half,
clamped to stock or replaced by its straight chord. Both endpoints lie within
the configured stock bounds. These facts, the stock transform, exact endpoints,
unchanged raw stock and tool geometry are explicitly checked.

### Exact sampling and event generation

`tests/generate_curved_motion.py` defines:

```text
R = 5 mm
v = 300/60 = 5 mm/s
dt = 0.001 s
length = pi*R = 15.707963267949 mm
duration = pi*R/v = 3.141592653590 s
N = ceil(duration/dt) = 3142 intervals
for i = 0..N:
    theta_i = -min(v*i*dt/R, pi)
    x_i = 10 + R*cos(theta_i)
    y_i = 4  + R*sin(theta_i)
    z_i = -12
    steps_i = round_to_nearest_away_from_zero_at_halves(400 * position_i)
```

Endpoints are set explicitly to (6000,1600,-4800) and (2000,1600,-4800) steps.
Nominal travel per full interval is 0.005 mm; step size is 0.0025 mm. The final
sample clamps the ideal angle to pi and is delivered at tick 3142, i.e. 3142 ms.
The ideal remaining final travel takes 0.592653590 ms. There is no synthetic
acceleration/deceleration, interpolation of real HAL timing, following error or
claim of an exact LinuxCNC planner replay.

The committed `arc.steps` is the authoritative integer sequence, so C++ tests do
not recalculate trigonometry. The Python fixture test independently checks every
byte against regeneration; the smallest distance to a half-step rounding boundary
is 0.0000732803964638 steps. Each integer endpoint is converted through the
existing `tool_pose()` at 400 steps/mm, exactly like production machine snapshots.
Consecutive unchanged XYZ samples would not generate Motion events. For this
fixture **all 3142 intervals change XYZ**.

| Count | Plunge only | Plunge + arc |
|---|---:|---:|
| Generated positions, including initial air pose | 2 | 3144 |
| Arc positions alone, including shared plunge endpoint | 0 | 3143 |
| Generated/received Motion events | 1 | 3143 |
| Other worker events | Enable + Capture | Enable + Capture |
| Total worker events | 3 | 3145 |

Both burst and paced modes replay exactly these endpoints. Paced mode sleeps to
absolute monotonic deadlines `arc_start + tick*1ms`; event timestamps reflect the
actual enqueue time. Late OS wakeups can lead to catch-up bursts, but never skipped
samples or changed coordinates. Only the arc is paced; the plunge remains one
event. The benchmark exercises the existing MaterialWorker/MotionQueue pipeline,
not a new UDP generator or a G-code parser. Existing UDP integration tests remain
in the full suites.

### Frozen full-volume reference and fixed counters

[Reference artifacts](../tests/fixtures/curved-material/README.md) retain the
**entire final occupancy**, not just a removed count. `plunge.rle` and
`plunge-arc.rle` encode all six million 0/255 values in canonical z/y/x order,
x fastest, as lossless text run-lengths. The expected state is read from disk;
normal tests never generate it from the implementation being tested.

`tests/VoxelReference.hpp` compares every cell, and also reports FNV-1a-64 over
three uint64 little-endian grid dimensions followed by the canonical voxel bytes.
It does not iterate sparse maps. A test explicitly varies sparse insertion and
materialization order. Hash equality is supplementary; the exact comparisons do
not depend on collision resistance. Transform/tool configuration is checked
separately, and versions/timings are not serialized as geometry.

| State | Removed voxels | Removed mm3 | FNV-1a-64 |
|---|---:|---:|---|
| Fresh stock + plunge | 56560 | 56.560 | `95c98093e6e51d52` |
| Fresh stock + plunge + arc | 166400 | 166.400 | `585ccf7e9d0561a2` |
| Additional removal caused by arc | 109840 | 109.840 | difference, not a separate stock state |
| Fresh stock + plunge + straight chord | 176560 | 176.560 | `d147bb5429b4d692` |

Arc and chord differ at **78560 individual voxel locations**. Explicit witnesses
include local voxel (100,40,80), preserved by the arc and removed by the chord,
and (100,0,80), removed by the arc and preserved by the chord. This prevents a
future endpoint-only/chord shortcut even if removed counts happen to agree.

`serial.stats` pins scheduling-independent structural counters. The raw reference
calls the unchanged MaterialRemoval for every individual segment. A separate
**test-only** replay applies the existing exact predicate greedily, with no
wall-clock flush boundaries; it also compares every voxel with the frozen arc.
Quantized curved motion does contain some exact straight runs, so 3142 arc events
become 2574 arc events in this offline replay (2575 with the plunge).

| Reference replay | Sweeps incl. Enable | Chunks tested | Chunks changed | Voxel tests | Removed |
|---|---:|---:|---:|---:|---:|
| Plunge only | 2 | 18 | 16 | 80724 | 56560 |
| Plunge + all 3142 arc segments | 3144 | 29894 | 6884 | 29115741 | 166400 |
| Arc-only increment in that serial replay | 3142 | 29876 | 6868 | 29035017 | 109840 |
| Plunge + offline exact-coalesced arc | 2576 | 23930 | 5820 | 22944469 | 166400 |

Alternative segmentation splits an original edge only when its represented
binary64 midpoint is proven exactly on that edge by the existing exact predicate;
otherwise the edge is retained. This gives **4534 arc edges**, 4536 sweeps including
plunge/Enable, 43752 tested chunks, 7526 changed chunks and 43146111 voxel tests.
All six million final cells match the frozen arc. This is a subdivision of the
same polyline, **not** another angular discretization of the ideal circle.
Two independent worker executions with the same path and 1 material/4 mesh
threads also compare exactly against the frozen reference and against each other.

Runtime coalescing boundaries depend on the existing 20-ms budget and scheduling;
therefore runtime sweeps/chunk tests/voxel tests, queue peaks and mesh job counts
can vary while the input sequence and final state remain identical. The tests pin
the serial/offline structural counts and the generated/received event counts.
They also assert correct accounting, drained queues, zero final lag and current
mesh generation/version. They do not assert a timing or queue-peak performance
threshold. No existing test or tolerance was relaxed.

### Curved benchmark measurements

Measured sequentially after all build/test processes finished, on GCC 14.2.0
Release with 14 available logical CPUs (2026-09-28). These are individual
baseline observations, not throughput guarantees or pass/fail time limits.

| Metric | Plunge, burst | Plunge + arc, burst | Plunge, paced case | Plunge + arc, 1 kHz |
|---|---:|---:|---:|---:|
| Generated positions including initial pose | 2 | 3144 | 2 | 3144 |
| Generated Motion events | 1 | 3143 | 1 | 3143 |
| Received Motion events | 1 | 3143 | 1 | 3143 |
| Motion events after coalescing | 1 | 2591 | 1 | 2580 |
| Total events (incl. Enable/Capture) | 3 | 3145 | 3 | 3145 |
| Sweeps (incl. Enable) | 2 | 2592 | 2 | 2581 |
| Chunks tested | 18 | 24104 | 18 | 23996 |
| Chunks changed | 16 | 5864 | 16 | 5834 |
| Voxels tested | 80724 | 23142589 | 80724 | 23020413 |
| Voxels removed | 56560 | 166400 | 56560 | 166400 |
| Removed mm3 | 56.560 | 166.400 | 56.560 | 166.400 |
| Broad phase ms | 0.006 | 4.502 | 0.005 | 4.530 |
| Narrow phase ms | 4.925 | 3420.238 | 4.635 | 3459.533 |
| Mutation ms | 2.013 | 40.122 | 2.091 | 40.685 |
| Dirty invalidation ms | 0.189 | 35.928 | 0.179 | 36.119 |
| Worker processing ms | 7.492 | 3615.740 | 7.284 | 3656.478 |
| Coalescing ms | 0.442 | 23.703 | 0.443 | 24.908 |
| Snapshot ms | 0.275 | 17.617 | 0.419 | 18.381 |
| Mesh publication ms | 0.089 | 0.308 | 0.076 | 0.315 |
| Mesh jobs / rebuilds | 280 | 1979 | 280 | 2214 |
| Stale mesh jobs | 0 | 1679 | 0 | 1910 |
| Accepted mesh jobs | 280 | 300 | 280 | 304 |
| Mesh processing sum ms | 339.733 | 4734.812 | 340.064 | 5206.111 |
| Motion queue maximum | 3 | 3120 | 3 | 490 |
| Mesh queue peak | 280 | 280 | 280 | 280 |
| Final worker lag ms | 0.000 | 0.000 | 0.000 | 0.000 |
| Wall time ms | 95.202 | 3677.310 | 95.573 | 3758.683 |
| Material workers | 1 | 1 | 1 | 1 |
| Mesh workers | 4 | 4 | 4 | 4 |
| Observed parallel mesh peak | 4 | 4 | 4 | 4 |

Plunge versus plunge+arc differs by exactly **109840 removed voxels / 109.840 mm3**.
The independently measured wall-time difference in the paced pair was 3663.111 ms;
completion was approximately 616.683 ms beyond the nominal arc delivery
interval, including startup/plunge/shutdown overhead.

A second standalone paced arc run measured 2581 coalesced motion events,
2582 sweeps, queue max 443, 2191 mesh jobs (1891 stale), and
3714.108 ms wall time. Both benchmark repetitions require **exact cell equality**
to the same frozen states; both reported `585ccf7e9d0561a2`. The difference in
runtime counters is expected from the unchanged deadline-driven consumer.

Separately measured direct serial replay, without worker scheduling or meshes:

| Timing | Plunge only | Plunge + all original arc segments |
|---|---:|---:|
| Broad phase ms | 0.005 | 5.528 |
| Narrow phase ms | 4.318 | 4106.653 |
| Mutation ms | 1.985 | 49.120 |
| Dirty invalidation ms | 0.178 | 44.336 |
| Wall ms | 6.614 | 4343.732 |


All benchmark cases use fresh stock, one material owner and four mesh threads.
Wall time includes worker construction, Enable, plunge, input pacing if selected,
final Capture copy and complete material/mesh shutdown. Raw stock construction,
reading/expanding fixtures, exact comparisons and fingerprint calculation are
outside wall time. The initial full-stock mesh work is included in **each** case;
subtracting independently timed runs is a diagnostic difference, not isolated CPU
arc cost. The serial table above supplies the exact additive geometric/work delta.

Mesh job count is derived from the existing rebuild counter after complete drain:
every submitted job completed then, including rejected stale jobs. Accepted jobs
are rebuilds minus stale jobs; they are not unique chunk counts. Mesh milliseconds
sum job elapsed durations across threads and can exceed benchmark wall time.
Queue peak is an observed high-water mark, **not capacity**. There is no fixed
configured mesh-queue capacity: this implementation has one batch (at most 280
chunk jobs for this stock) plus one deduplicated pending set (at most 280 entries).
Those sets can refer to the same chunk; 560 is a structural bound here, not a
preallocated queue. MotionQueue remains dynamically allocated and unbounded.
Final worker lag is zero after drain and is not an in-motion visual latency metric.

### Curved baseline validation and reproduction

| Validation after adding the curved baseline | Result |
|---|---|
| Graphics Release, full CTest suite | **49/49 passed**, no skips |
| Headless Release, full CTest suite | **46/46 passed**, no skips |
| ASan/UBSan + leak detection, full headless suite | **46/46 passed**, no reports |
| TSan, all curved tests plus existing concurrency/queue/runtime/failure tests | **12/12 passed**, no reports |
| Vendor integrity | nine unchanged original files match pinned SHA-256 |
| Protocol/HAL/UDP and namespace tests | passed in full suites, no namespace skip |
| Source integrity against ae13f21 | no changes under `src/`, `third_party/`, `stepper-ninja/` |
| Real LinuxCNC acceptance | **NOT YET PASSED** |


New CTests cover configuration/fingerprint ordering, plunge, arc plus fixed
coalescer replay, same-polyline subdivision, identical worker repetition, the
chord distinction, paced worker replay and integer fixture regeneration.
The optional brute-force union oracle was not added: the frozen complete occupancy
and independent replay/subdivision checks supply the baseline without a new
production or test union algorithm.

Use the complete Graphics/Headless/ASan build commands above. Relevant TSan run:

```bash
cmake --build build-tsan -j4
TSAN_OPTIONS=halt_on_error=1 ctest --test-dir build-tsan \
  -R 'curved-|material-(concurrency|queue|runtime|failure)' --output-on-failure
```

Individual checks and benchmark commands:

```bash
python3 tests/generate_curved_motion.py
ctest --test-dir build-headless -R '^curved-' --output-on-failure
./build-headless/curved-material-tests benchmark burst 4
./build-headless/curved-material-tests benchmark paced 4
./build-headless/curved-material-tests arc
./build-headless/curved-material-tests segmentation
./build-headless/curved-material-tests repeat
./build-headless/curved-material-tests chord
```

CTest never rewrites expected files. The explicit `record NEW_DIRECTORY` command
refuses an existing directory and is only for deliberate reference investigation;
do not replace these ae13f21 fixtures to make a future optimized implementation
pass. Record candidate output elsewhere and require exact equality with these
original states. Logs and temporary captures remain ignored under `build-curved/`.

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
