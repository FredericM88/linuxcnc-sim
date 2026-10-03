# Repository layout

```text
LICENSE                         linuxcnc-sim MIT license
README.md                       Public entry point, build and Quick Start
CMakeLists.txt                  Simulator and automated tests
src/                            C++20 simulator and configuration system
examples/mill/                  Simulator INI milling profile
examples/phase3/                LinuxCNC XYZ machine, homing and virtual I/O
examples/phase1/                Earlier motion-only LinuxCNC example
scripts/                        veth/namespace setup, launch and teardown
tests/                          C/C++, Python and shell regression tests
third_party/stepper-ninja/       Minimal unchanged protocol and HAL-test inputs
docs/                           Configuration, designs, validation and release notes
```

The public project is **linuxcnc-sim**; its executable remains **cnc-sim**.
Source names, CLI behavior, configuration semantics and example LinuxCNC machine
names retain the accepted implementation. This release cleanup does not rename
runtime interfaces.

## Bundled dependencies

Only `third_party/stepper-ninja/` supplies bundled Stepper-Ninja code. The existing
nine protocol/license files and the Board-0 HAL fragment are pinned to upstream
commit `eb7e5dfa2e76477e606a47038b07cca5e8a4b424`. The fragment is used directly by
`original-hal-inputs`; it has not been reimplemented or patched. All ten files are
covered by the SHA-256 integrity test. No upstream checkout or submodule download
is needed to build the simulator or run its complete automated tests.

The [upstream provenance](../third_party/stepper-ninja/UPSTREAM.md) and original
[MIT license](../third_party/stepper-ninja/LICENSE.txt) preserve Zsolt Viola's
attribution separately from the [linuxcnc-sim MIT license](../LICENSE).
The historical [file-selection analysis](stepper-ninja-files.md) also records
licenses of upstream components that are deliberately **not bundled**.

The separately installed LinuxCNC HAL module uses a pinned external checkout;
see the [Quick Start](../README.md#quick-start). The full development reference
checkout formerly tracked at `stepper-ninja/` is excluded from the public tree.
That path is ignored so an existing local reference checkout can remain intact.
It is not a build/test dependency. References to omitted upstream sources in
historical documents use commit-pinned upstream links; bundled sources use
repository-relative links.

`.gitattributes` preserves upstream bytes and existing line endings only under
the vendor directory. Project sources retain normal whitespace checks.

## Local and generated files

Build directories, compiler outputs, recorder CSVs, Python caches, editor files,
temporary files and LinuxCNC runtime state are ignored. The local recorder outputs
`phase2-line.csv` and `phase2-circle.csv` were already untracked before release
cleanup and are not distributed. LinuxCNC `.ini`, `.hal`, `.tbl` and virtual-I/O
example files remain tracked. Runtime parameter and position files are generated
locally and should not be committed.

Development prompt documents were removed from the public tree. Useful design,
protocol, benchmark and acceptance documents remain under `docs/`; their phase
names identify historical revisions rather than separate public products.

## Architecture and evidence

- [Configuration model, precedence and complete schema](simulator-configuration.md)
- [Workpiece geometry and runtime snapshots](phase4a1-design.md)
- [Material ownership, continuous sweeps, batching and mesh publication](phase5-design.md)
- [Frozen material comparisons and LinuxCNC acceptance](phase5-test.md)
- [Configuration validation and subsequent manual acceptance](phase55-test.md)
- [Public release validation](release-validation-v0.1.0.md)

The example subnet `192.168.50.0/24` and the original vendor configuration's
addresses are intentional functional configuration, not private developer
infrastructure. No developer-specific home directory or private Git origin is
part of the public instructions.
