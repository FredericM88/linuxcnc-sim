# v0.1.0 release preparation and validation

Baseline: `b61b8c5ea6578f1b7b91feea9f675cc1d74fc296`
(`Add simulator INI configuration`). The working tree was clean before the
read-only audit. The operator confirmed Phase 5.5 manual acceptance against real
LinuxCNC before this cleanup. No new manual LinuxCNC run is claimed here.
Validation date: 2026-10-03.

## Repository audit and cleanup

The audit distinguished the 336 tracked files from local/ignored files. It
covered the root, complete tracked inventory, ignore/attribute rules, documentation,
examples, scripts, CMake, tests, vendor licenses and include dependencies, symlinks,
recorder outputs, generated files, local paths, network addresses and Git metadata.

- Git author history consistently identifies Frederic Müller. The new root
  LICENSE uses the standard MIT text and Copyright (c) 2026 Frederic Müller.
- The original upstream MIT license remains byte-identical, with Copyright (c)
  2025 Zsolt Viola. No project ownership claim is made over the HAL or protocol.
- The full `stepper-ninja/` reference tree had 208 tracked entries. The unchanged
  Board-0 HAL fragment was relocated under `third_party/stepper-ninja/`; the other
  207 entries were removed from the public tree. The local upstream checkout and
  its local Git metadata were left intact and are now ignored in their entirety.
- The original nine vendor manifest entries remain unchanged; the relocated HAL
  fragment adds a tenth pinned hash. The test no longer depends on a second
  checkout, and CMake always includes the original HAL mapping test.
- Six development prompt files were removed (five at root and the Phase 1 prompt
  under docs). Design, protocol, benchmark and acceptance evidence was retained.
- Recorder CSVs were already untracked and ignored. No recorder files, builds,
  editor state or nested Git metadata are included in the public source tree.
- The README is organized around architecture, interface rationale, features,
  requirements, build, Quick Start, configuration, controls, limitations, docs,
  roadmap, attribution and license. Release notes are a separate public draft.
- Intentional example subnet and upstream device addresses remain unchanged.
  The private configured Git remote is local metadata and is not copied into docs.
  Clone instructions request the published repository URL without inventing one.

## Feature freeze

All files under `src/`, `examples/` and `scripts/`, along with frozen material
fixtures and material/protocol test expectations, are byte-identical to the
baseline. No CLI, output formatting, runtime behavior, configuration semantics,
geometry, packet processing, transport, worker algorithm or rendering changes
were made. Build/test edits only relocate the HAL test dependency and extend
vendor integrity coverage. Vendored upstream file contents are unchanged.

## Validation matrix

| Suite | Result | Elapsed seconds |
|---|---|---:|
| Graphics Release | 94/94, no skips | 83.60 |
| Fresh public-tree headless Release | 91/91, no skips | 71.02 |
| ASan/UBSan + leak detection | 91/91, no skips | 335.11 |
| TSan | 91/91, no skips | 281.58 |

No compiler warnings, skips, ASan/UBSan/leak reports or TSan reports occurred.

A new source export containing only staged public files was built in a new
headless build directory and ran the complete headless suite. This export contains
neither the ignored reference checkout nor any pre-existing build input. Graphics,
ASan/UBSan with leak detection, and TSan use the full existing suites. Display
variables are unset for headless and sanitizer runs. No frozen references are
regenerated. Elapsed times are validation durations, not performance benchmarks.

## Frozen material and vendor checks

All four complete suites preserve:

| Reference | Fingerprint |
|---|---|
| Plunge | `95c98093e6e51d52` |
| Plunge + arc | `585ccf7e9d0561a2` |

Batch tests retain exact equality over all **6,000,000 voxels**, with the original
frozen references unchanged. Vendor integrity passes **10/10**: the original nine
hashes plus the relocated HAL fragment. All ten files also compare byte-for-byte
against a fresh public upstream clone checked out at the pinned revision.

## Documentation and reproducibility

- All **111 repository-local Markdown links**, including heading anchors, resolve
  within the public tree. Omitted upstream analysis sources have commit-pinned
  links verified against the original Git objects.
- All **11 README Bash blocks** pass shell syntax checks. Referenced startup
  scripts and supplied LinuxCNC/simulator configuration inputs exist. The optional
  headless INI is explicitly a user-created copy, not a bundled file.
- The README's pinned Stepper-Ninja revision was fetched from its public upstream
  repository into a new checkout. Its original `stepgen-ninja` HAL module builds
  successfully against the installed LinuxCNC 2.9.10 development files, without
  source edits, compiler warnings or installation. Upstream pragma messages are
  compiler notes. This checks the documented external dependency independently of
  the retained local reference checkout.
- The 126-file public inventory contains no full reference checkout, nested Git
  metadata, recorder CSV, generated build/editor artifact, developer home path,
  private development hostname, private origin address or development prompt.
  Intentional simulation/vendor network configuration remains unchanged.

The existing network namespace test validates setup, UDP communication and teardown
inside private unprivileged namespaces. Release preparation does not change host
networking, install a HAL module or start a new manual LinuxCNC session.

The full validation configurations and commands are documented in
[Phase 5.5 validation](phase55-test.md#reproduction). The fresh source export is
created with `git checkout-index` after staging the public tree, configured with
`-DCMAKE_BUILD_TYPE=Release -DCNC_SIM_RENDER=OFF -DCNC_SIM_RENDER_TESTS=OFF`,
and tested with both DISPLAY and WAYLAND_DISPLAY unset. Local logs remain in
ignored build directories as `release-build.log`, `release-tests.log` and CTest's
`Testing/Temporary/LastTest.log`.

## Publication and deferred work

This preparation creates one local commit only. It does not push, tag v0.1.0 or
create a GitHub release. Publication location and release publication remain
separate maintainer actions. Geometric stock probing, toolsetter, spindle/holder
model, new cutters, collision detection, rotary material transforms and additional
transports remain outside v0.1.0. See the [release-notes draft](release-notes-v0.1.0.md).
