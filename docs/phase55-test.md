# Phase 5.5 configuration validation

Implementation started from clean HEAD
`c042166f27c25c46beaebc8605f1a991ded20c48` (Phase 5 accepted).
Validation date: 2026-10-03. No references were regenerated.

The [configuration guide](simulator-configuration.md) records the complete schema,
precedence, CLI compatibility, units, coordinate responsibilities, relative paths,
validation and Phase 6 limits. The example is `examples/mill/simulator.ini`.

## Scope and configuration coverage

33 dedicated CTests were added: 31 executable-level Python scenarios, one C++
model/transform test and one render-capability/example test. They cover:

- Defaults without INI, full and partial INI, explicit defaults/INI/CLI precedence,
  including `--config` after overrides; legacy CLI flags without INI.
- X/Y/Z/A scale mapping, positive INI scales versus signed legacy CLI scales,
  linear steps/mm versus rotary steps/degree, independent legacy display labels.
- Symbolic, numeric and mixed origins; key-order independence; one-time symbolic
  resolution; CLI minimum-corner/size overrides; exact decimal legacy transforms
  with and without rotation.
- Relative I/O paths from another working directory; replacement by CLI paths;
  repeated CLI files; missing/directory/bad-content I/O references.
- Unknown and reserved sections, unknown keys, duplicate keys/sections, malformed
  integers/reals, NaN/infinity/overflow, invalid enums/booleans, vectors and origins,
  port and IPv4 validation, voxel/stock/chunk limits, batch intervals, material and
  mesh worker limits, tool dimensions and statistics bounds.
- Filename/line/section/key diagnostics and errors before simulation startup.
- Default no-config runtime behavior; actual configured workpiece origin and
  virtual input; custom initial tool plus material enable with an independent
  analytic voxel-centre count; interactive overrides without rewriting the INI.
- Actual example validation from an unrelated working directory, rendering build
  capability checks, and `--print-config` without UDP/worker/window startup.

A first run exposed a wrong argument count in the newly added C++ test harness;
it was corrected to use the actual array size. The final runs below use the
corrected test. No production algorithm or frozen assertion was changed for it.

## Final complete runs

| Suite | Result | Elapsed seconds |
|---|---|---:|
| Graphics Release | 94/94, no skips | 78.58 |
| Headless Release | 91/91, no skips | 73.69 |
| ASan/UBSan + leak detection | 91/91, no skips | 333.11 |
| TSan | 91/91, no skips | 278.07 |

All builds are warning-free. No ASan, UBSan, leak or TSan report occurred.

All four builds use the existing project configurations; graphics Release includes
all three desktop renderer tests. Headless and sanitizer suites run with DISPLAY
and WAYLAND_DISPLAY unset. ASan/UBSan includes leak detection. Each suite includes
all existing material, batching, curved, protocol, original HAL, UDP, private
network namespace, recorder, virtual I/O and vendor-integrity tests.

## Frozen Phase 5 results and source integrity

All four final suite logs report:

| Case | Removed voxels | Volume mm³ | FNV-1a-64 |
|---|---:|---:|---|
| Plunge | 56560 | 56.560 | `95c98093e6e51d52` |
| Plunge + arc | 166400 | 166.400 | `585ccf7e9d0561a2` |

Batch windows 1/5/10/20/50 ms each report **exact_cells=6000000**, with the same
frozen arc fingerprint. Existing segmentwise, subdivision, boundary, failure,
concurrency and full-cell curved-reference checks pass. Fingerprints are additional
diagnostics, not substitutes for exact occupancy comparison.

The explicit vendor check passes **9/9 pinned SHA-256 files**. A diff against
`c042166` is empty for `src/material`, `src/tool`, `src/protocol`, `src/network`,
`src/machine`, `src/meshing`, `src/volume`, `third_party`, `stepper-ninja`,
`tests/fixtures`, `tests/VoxelReference.hpp`, `tests/curved_material_tests.cpp` and
`tests/material_batch_tests.cpp`. There is no new material-worker concurrency,
protocol change, material algorithm change or altered frozen reference.

Startup integration accepts an already-validated immutable workpiece snapshot.
The old scene-to-workpiece adapter was moved verbatim into Workpiece (apart from
parameter/access syntax), preserving legacy transforms. Initial tool/enable events
are queued before starting the existing UDP producer. Default startup queues no
additional events. The accepted Phase 5 LinuxCNC run remains historical evidence;
this phase's validation does not claim a new manual LinuxCNC acceptance run.

## Reproduction

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCNC_SIM_RENDER=ON -DCNC_SIM_RENDER_TESTS=ON
cmake --build build -j4
ctest --test-dir build --output-on-failure

cmake -S . -B build-headless -DCMAKE_BUILD_TYPE=Release -DCNC_SIM_RENDER=OFF -DCNC_SIM_RENDER_TESTS=OFF
cmake --build build-headless -j4
env -u DISPLAY -u WAYLAND_DISPLAY ctest --test-dir build-headless --output-on-failure

cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug -DCNC_SIM_RENDER=OFF -DCNC_SIM_RENDER_TESTS=OFF \
  -DCMAKE_C_FLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie' \
  -DCMAKE_CXX_FLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined -no-pie'
cmake --build build-sanitize -j4
env -u DISPLAY -u WAYLAND_DISPLAY ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
  UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  ctest --test-dir build-sanitize --output-on-failure

cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DCNC_SIM_RENDER=OFF -DCNC_SIM_RENDER_TESTS=OFF \
  -DCMAKE_C_FLAGS='-O1 -g -fsanitize=thread -fno-omit-frame-pointer -fno-pie' \
  -DCMAKE_CXX_FLAGS='-O1 -g -fsanitize=thread -fno-omit-frame-pointer -fno-pie' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=thread -no-pie'
cmake --build build-tsan -j4
env -u DISPLAY -u WAYLAND_DISPLAY TSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build-tsan --output-on-failure

python3 tests/vendor_integrity.py
```

Local build/test logs are retained in each ignored build directory as
`phase55-build.log`, `phase55-tests.log` and CTest's `Testing/Temporary/LastTest.log`.
The suites were run concurrently across configurations, so elapsed times are
validation durations, not isolated performance measurements.
