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

## Build against a LinuxCNC source tree

`LINUXCNC_SOURCE_DIR` selects the requested tree. A configured userspace RIP tree
builds a real module; an unconfigured tree retains the source-header compile
check. Use a separate, clean build directory for each environment:

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
Neither source-tree mode installs or loads a module.

### Configured RIP: real module

When `<source>/src/Makefile.modinc` and the generated `<source>/include/hal.h`
and `rtapi.h` exist, the normal CMake build produces:

```text
build-hal-2.10/stepgen-ninja.so
```

The driver is still built from the prepared, hash-verified, patched copy.
CMake invokes make with the requested RIP tree's own `Makefile.modinc`, which
supplies the compiler/linker flags, generated include directory and export rules.
The adapter retains `-Werror=implicit-function-declaration` and
`-Werror=incompatible-pointer-types`. No temporary hand-written Makefile is needed.

CMake evaluates `RUN_IN_PLACE`, `BUILDSYS`, `EMC2_HOME` and `RTLIBDIR` using make.
It requires `RUN_IN_PLACE=yes`, `BUILDSYS=uspace`, and canonical home/rtlib paths
matching the requested tree. A foreign or symlinked `Makefile.modinc`, mismatched
configuration, or generated HAL/RTAPI header pointing outside the tree is rejected.
Installed-mode `LINUXCNC_MODINC` and `LINUXCNC_INCLUDE_DIR` cache values are ignored
when selecting a source tree; there is no fallback to the system installation.

`RTLIBDIR` is validated and reported, but **never used as a copy/install destination**.
The module remains in the compatibility build directory. Inspection before any
separate manual runtime test can use:

```bash
file build-hal-2.10/stepgen-ninja.so
nm -D build-hal-2.10/stepgen-ninja.so
```

Build directories, upstream checkout paths and source-tree aliases containing
spaces are covered by regression tests, as are installed-mode modinc paths with
spaces. A LinuxCNC tree configured at an actual path containing spaces remains
subject to LinuxCNC's own generated flags, which this adapter does not rewrite.

### Unconfigured tree: compile check

If `src/Makefile.modinc` is absent, or it belongs to the requested userspace RIP
tree but the generated HAL/RTAPI headers are incomplete, CMake reports compile-only
mode. An existing invalid or foreign modinc is an error, not a silent fallback.
No configure step, generated headers or LinuxCNC libraries are required for the
compile check. It uses actual headers from `src/hal` and `src/rtapi` exclusively,
with the userspace realtime defines `USPACE`, `RTAPI`, `_GNU_SOURCE`, `realtime`,
`__MODULE__`, and `SIM`. Compiler errors are enabled for implicit declarations
and incompatible pointer types. No substitute HAL headers or 2.9 include paths
are used.

`stepgen-ninja` is an **object-library target** in this fallback mode. The complete
driver is compiled, including the protocol C implementation and Board-0 helper.
The contract test links a test-only HAL allocator/exporter and intercepts packet
send/receive; it uses the real API-1 inline getters/setters. It is not a LinuxCNC
runtime or real shared-memory integration test. This mode produces no `.so`.

## Configured RIP CMake integration validation

| Environment | Build and tests |
|---|---|
| Installed LinuxCNC 2.9.10, fresh build directory | Real `.so`; 11/11 compatibility tests passed |
| Configured LinuxCNC 2.10.0~pre2 RIP, fresh build directory | Real x86-64 ELF shared object; 10/10 compatibility tests passed |
| Isolated unconfigured copy of the real 2.10 source headers | Object compile check, no `.so`; 9/9 compatibility tests passed |

The RIP module references `hal_pin_new_bool`, `hal_pin_new_real`,
`hal_pin_new_si32` and `hal_pin_new_ui32`, with no old `hal_pin_*_newf` references.
Tests cover the existing contracts and cross-version parity, builds with spaces,
unconfigured/incomplete-tree fallback, and rejection of foreign modinc files,
RIP destinations and generated headers. All tests passed without skips.

The RIP configuration enables `-Wall -Wextra`, exposing six existing unused
parameter/function/variable warnings in upstream sources. No such source was
changed to suppress them. The compatibility header, patch, preparation logic,
vendor snapshot, upstream checkout, simulator sources and frozen references remain
unchanged. During this automated build/test validation, neither the installed
system module nor the RIP rtlib module was replaced, and the module was not loaded.
The subsequent manual smoke acceptance of this build path is recorded below.

### Manual smoke acceptance of the configured-RIP CMake build

The operator reported successful runtime smoke acceptance of the new CMake-built
module against LinuxCNC **2.10.0~pre2 RIP**, using HAL API 1. The tested artifact was:

```text
/home/frederic/dev/linuxcnc-sim/build-hal-rip-210/stepgen-ninja.so
SHA256: 06d03e0440990dedc64aa77f7295b2150a15432efb9137cee2fd47b72dd343aa
```

`file` identified an ELF 64-bit LSB shared object, x86-64, dynamically linked,
with debug_info, not stripped. `nm -D` showed:

```text
U hal_pin_new_bool
U hal_pin_new_real
U hal_pin_new_si32
U hal_pin_new_ui32
```

There were no old `hal_pin_*_newf` references. The generated module was manually
copied into the LinuxCNC 2.10 RIP rtlib; the source and copied module SHA256 hashes
matched exactly. This was a manual test step, not an automatic CMake installation.

Under the LinuxCNC 2.10 RIP environment, the following succeeded:

```hal
loadrt stepgen-ninja ip_address="192.168.50.2:8888"
```

The component reached ready state and exported the expected `bool`, `real`,
`sint` and `uint` pins. The end-to-end simulator smoke test reported
`Connection: CONNECTED 192.168.50.1:8888`. At the observed endpoint:

| Counter | Result |
|---|---:|
| RX | 155536 |
| Accepted | 155536 |
| TX | 155536 |

All observed communication error counters were zero: invalid packets, send
errors, length errors, checksum errors, timing errors, position overflows and
packet-ID gaps.

Homing completed successfully before the motion test, exercising virtual input
feedback as well as command transport. The simulator then showed:

| Axis | Steps | Position (mm) |
|---|---:|---:|
| X | 4000 | 10.0000 |
| Y | 4000 | 10.0000 |
| Z | -2000 | -5.0000 |

LinuxCNC AXIS showed the same X10 Y10 Z-5 position.

This is smoke acceptance specifically of the **new configured-RIP CMake module
build path**. Probe, material removal and the complete earlier runtime acceptance
suite were **not repeated** with this module. The separate
[full LinuxCNC 2.10 runtime acceptance record](linuxcnc-2.10-runtime-acceptance.md)
remains unchanged and describes the previous manually built module from commit
`9758646`. The known upstream checksum/connected-reference bug remains unfixed;
this error-free smoke run does not validate checksum-failure recovery.

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
