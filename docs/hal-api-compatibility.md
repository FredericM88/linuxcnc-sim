# Stepper-Ninja HAL API compatibility

The project-owned adapter in `compat/stepper-ninja/` builds the original
Stepper-Ninja `stepgen-ninja` driver for LinuxCNC 2.9 and HAL API 1 (2.10).
It supports this simulator's pinned Board-0 UDP profile. It does not change
the simulator, packet definitions, checksums, pin names, directions, defaults,
motion calculations or frozen material references.

Commit `9758646` subsequently passed [real runtime acceptance](linuxcnc-2.10-runtime-acceptance.md)
with LinuxCNC 2.10.0~pre2 RIP, HAL API 1 and POSIX/uspace realtime for the pinned
Board-0 UDP profile. The compile/test results below remain the earlier record.

## Source ownership and preparation

The complete original driver is still obtained separately from
[Stepper-Ninja](https://github.com/atrex66/stepper-ninja/tree/eb7e5dfa2e76477e606a47038b07cca5e8a4b424),
revision `eb7e5dfa2e76477e606a47038b07cca5e8a4b424`, by Zsolt Viola.
Its MIT license remains in `third_party/stepper-ninja/LICENSE.txt` and is copied
to the generated source directory. The patch is an adaptation of that source;
the original driver's attribution and module license declarations are retained.
Project-owned adapter/build/test code is covered by the root MIT license.

The standalone CMake build runs `prepare.py`, which verifies all ten upstream
files against the existing vendor SHA-256 manifest, plus pinned hashes for
`stepgen-ninja.c` and `hal_pin_macros.h`. It reads the separate checkout, copies
only the required inputs to `<build>/prepared/` as regular files, and applies
`hal-api.patch` there with zero fuzz. Upstream header symlinks are not carried
into the generated tree. Output paths overlapping either upstream or the vendor
snapshot are rejected. A changed driver or protocol configuration fails early.
Updating the upstream revision requires reviewing the patch and input hashes.

The patch changes just three generated files: `stepgen-ninja.c`,
`hal_pin_macros.h`, and `modules/breakoutboard_hal_0.c`. The only API-version
branch in production compatibility code lives in `hal_compat.h`:

```c
#if defined(HAL_API_VERSION) && HAL_API_VERSION >= 1
```

The root simulator CMake build remains independent of LinuxCNC headers and the
separate checkout. Other breakout-board profiles, SPI builds and other upstream
modules are outside this adapter's supported build profile.

## Exact API mapping

| Adapter type | LinuxCNC 2.9 | LinuxCNC 2.10/API 1 |
|---|---|---|
| `sn_bit_pin` | `hal_bit_t *` | `hal_bool_t` |
| `sn_s32_pin` | `hal_s32_t *` | `hal_sint_t` |
| `sn_u32_pin` | `hal_u32_t *` | `hal_uint_t` |
| `sn_float_pin` | `hal_float_t *` | `hal_real_t` |
| Pin direction | `hal_pin_dir_t` | `hal_pdir_t` |

| Adapter operation | LinuxCNC 2.9 | LinuxCNC 2.10/API 1 |
|---|---|---|
| `sn_new_bit` | `hal_pin_bit_newf` | `hal_pin_new_bool` |
| `sn_new_s32` | `hal_pin_s32_newf` | `hal_pin_new_si32` |
| `sn_new_u32` | `hal_pin_u32_newf` | `hal_pin_new_ui32` |
| `sn_new_float` | `hal_pin_float_newf` | `hal_pin_new_real` |
| `sn_get_bit` / `sn_set_bit` | Dereference / assignment | `hal_get_bool` / `hal_set_bool` |
| `sn_get_s32` / `sn_set_s32` | Dereference / assignment | `hal_get_si32` / `hal_set_si32` |
| `sn_get_u32` / `sn_set_u32` | Dereference / assignment | `hal_get_ui32` / `hal_set_ui32` |
| `sn_get_float` / `sn_set_float` | Dereference / assignment | `hal_get_real` / `hal_set_real` |
| `sn_export_funct` | `hal_export_funct(name, fn, arg, uses_fp, reentrant, id)` | `hal_export_funct(name, fn, arg, reentrant, id)` |

Creation adapts `(direction, reference_address, component_id, name)` to API 1's
`(component_id, direction, reference_address, 0, "%s", name)`. API 0 also uses
`"%s", name`, preserving upstream's already formatted names. Existing `PIN_*_INIT`
defaults are then set through the typed setter; errors propagate unchanged.
The three exported callbacks keep their names and `reentrant=1`; API 0 keeps
`uses_fp=1` as well.

API 1 physically stores integer pins in its native 64-bit HAL types. The
adapter deliberately uses the **si32/ui32** creation/access APIs so driver
reads truncate to 32 bits and writes sign/zero-extend 32-bit values. It does
not use the native-width `sint/uint` accessors or the clamped accessors.

## Direct-access audit

All 50 pin field declarations in the complete driver were converted, including
declarations in inactive conditional branches. All value dereferences in the
driver and Board-0 helper are replaced by typed get/set calls:

| Location | Reads | Writes | Covered pin values |
|---|---:|---:|---|
| `stepgen-ninja.c` | 47 | 41 | Commands, feedback, scales, mode/enable, pulse width, debug frequency/step counters/reset; encoder raw count/scale/position/velocity/RPM/index/reset; jitter, ring fill/status, connected, period, I/O readiness; conditional Raspberry Pi GPIO and PWM enable/frequency/duty/minimum/scale |
| `breakoutboard_hal_0.c` | 3 | 4 | GPIO output initialization/packing and input/inverted-input feedback |
| `hal_pin_macros.h` | 0 | 4 | BIT/S32/U32/FLOAT default initialization |

Counts are source call sites, including inactive branches, and include the
explicit read plus write for each of the two debug-counter compound assignments.
All eight creation macros and three Board-0 creation sites use the adapter;
all three callback exports use the adapter. There are no remaining `*d->...`
value dereferences in either generated C file. Analog/toolchanger declarations
are adapted, but other board helpers are not included in the supported build.

An existing upstream bug is intentionally preserved: on a checksum failure,
`d->connected = 0` clears the reference rather than the pin value. It is a
reference assignment, not a pin-value access. A subsequent valid packet can
dereference the null reference. Changing this would alter existing failure
behavior and is outside this API-only patch. The contract test records that
behavior without triggering a subsequent null access. Pin directions, including
the upstream `connected` declaration as `HAL_IN`, are also preserved.

## Build against installed LinuxCNC

Requires CMake 3.20+, a C compiler, make, patch, Python 3.9+, and the installed
LinuxCNC userspace development package. From the simulator checkout root:

```bash
git clone https://github.com/atrex66/stepper-ninja.git ../stepper-ninja-hal
git -C ../stepper-ninja-hal checkout --detach eb7e5dfa2e76477e606a47038b07cca5e8a4b424
cmake -S compat/stepper-ninja -B build-hal \
  -DSTEPPER_NINJA_SOURCE="$PWD/../stepper-ninja-hal"
cmake --build build-hal -j4
ctest --test-dir build-hal --output-on-failure
```

This uses the installation's `Makefile.modinc`, including its realtime flags,
module export processing and shared-module link rules. The result is
`build-hal/stepgen-ninja.so`. For a nonstandard installation, set both
`LINUXCNC_INCLUDE_DIR` and `LINUXCNC_MODINC` to that same installation.
The build has no installation target and never loads or replaces a HAL module.
On 2.9, the tests additionally compile the untouched upstream driver as a
behavioral baseline. `-DBUILD_TESTING=OFF` builds only the driver.

## Compile against uninstalled LinuxCNC 2.10 sources

For the local 2.10-pre2 source tree, no configure step, generated headers,
LinuxCNC libraries or host installation changes are needed for this component:

```bash
cmake -S compat/stepper-ninja -B build-hal-2.10 \
  -DSTEPPER_NINJA_SOURCE="$PWD/../stepper-ninja-hal" \
  -DLINUXCNC_SOURCE_DIR="$HOME/dev/linuxcnc-2.10-source" \
  -DHAL_BASELINE_EXECUTABLE="$PWD/build-hal/tests/hal-driver-baseline"
cmake --build build-hal-2.10 -j4
ctest --test-dir build-hal-2.10 --output-on-failure
```

The baseline option is optional and refers to a prior **2.9** test build. With
it, the API-1 executable's transcript is compared against unchanged upstream.
The compile uses actual headers from `src/hal` and `src/rtapi` exclusively,
with the userspace realtime defines `USPACE`, `RTAPI`, `_GNU_SOURCE`, `realtime`,
`__MODULE__`, and `SIM`. Compiler errors are enabled for implicit declarations
and incompatible pointer types. No substitute HAL headers or 2.9 include paths
are used.

`stepgen-ninja` is an **object-library target** in source-tree mode. The complete
driver is compiled, including the protocol C implementation and Board-0 helper.
The contract test links a test-only HAL allocator/exporter and intercepts packet
send/receive; it uses the real API-1 inline getters/setters. It is not a LinuxCNC
runtime or real shared-memory integration test. Producing/loading a 2.10 module
with 2.10's own link/export rules would require a configured 2.10 development
environment (`Makefile.modinc`) and live acceptance; this was not attempted
during the initial compile validation. The subsequent RIP runtime acceptance
is documented above; the source-tree CMake mode remains a compile check only.

## Validation on 2026-10-03

Host: installed LinuxCNC 2.9.10, GCC 14.2.0. Source check: clean LinuxCNC
`v2.10.0-pre2` tree reporting version `2.10.0~pre2`.

| Check | Result |
|---|---|
| Normal graphics Release build and existing CTest suite | 94/94 passed, no skips; 70.59 s |
| Installed 2.9.10 module build | `build-hal-2.9/stepgen-ninja.so` built successfully |
| 2.9 compatibility tests | 10/10 passed |
| Real 2.10-pre2 headers, complete driver object compile | Passed, no installed 2.9 HAL includes |
| 2.10 compatibility tests, including cross-version comparison | 9/9 passed |
| Pin and protocol parity | All 66 names/directions/defaults and 3 callback exports match; tested outgoing UDP bytes and feedback transcripts identical |
| Vendor integrity | All 10 manifest hashes match; entire vendor directory unchanged |
| Separate Stepper-Ninja checkout | Clean; file contents and symlink targets unchanged |

Contract coverage includes position/velocity mode, positive/negative motion,
disabled axes, pulse-width changes, debug resets, encoder feedback/index/reset
and timeout, all 128 digital input bits and inverses, output packing, watchdog
status, checksum failure, integer boundaries, API-1 64-to-32-bit truncation,
and six pin-creation failure positions. Preparation tests cover repeatability,
rejection of modified driver/configuration inputs and protected output paths.

No compiler warnings occurred in these builds; upstream emits informational
`#pragma message` notes. During this initial validation there was no installation,
module loading, new live LinuxCNC acceptance run, commit or push. PWM/SPI and
other board profiles have no runtime-validation claim. The existing checksum-error
reference bug remains.
