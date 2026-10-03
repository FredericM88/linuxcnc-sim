# Stepper-Ninja vendored files

- Upstream: [Stepper-Ninja](https://github.com/atrex66/stepper-ninja)
- Revision: `eb7e5dfa2e76477e606a47038b07cca5e8a4b424`
- Upstream author: Zsolt Viola (also credited as Viola Zsolt in source)
- License: MIT, **Copyright (c) 2025 Zsolt Viola**; the complete original text
  is preserved in [LICENSE.txt](LICENSE.txt).

These are third-party files, not code authored by linuxcnc-sim. All ten files
listed in [SHA256SUMS](SHA256SUMS) are byte-for-byte copies from the pinned
upstream revision, including whitespace and line endings. The original nine
manifest entries are unchanged. The tenth is the Board-0 HAL fragment relocated
from the former tracked reference checkout for the `original-hal-inputs` test.
`UPSTREAM.md` and `SHA256SUMS` are linuxcnc-sim provenance metadata.

## Included scope

The simulator compiles `firmware/modules/transmission.c` as C11. Its original
headers, configuration include chain, checksum table and PIO timing table are
preserved. `src/protocol/OriginalProtocol.hpp` supplies C linkage to C++ without
modifying upstream files. SYSTEM include directories isolate upstream warnings.

Profile: Board 0, four step generators, three encoders, UDP, PWM disabled with
`pwm_count=1`, no analog block and no software step ring. Compile-time assertions
check the existing packet sizes, offsets and profile.

`hal-driver/modules/breakoutboard_hal_0.c` is compiled directly by the original
HAL input-mapping test using small HAL stubs. This test dependency is not a full
LinuxCNC HAL driver distribution. The separately installed original HAL driver
is built from a pinned external checkout as described in the
[Quick Start](../../README.md#quick-start).

No Pico SDK, WIZnet, BCM2835, LinuxCNC source or unrelated upstream firmware,
utilities, binaries or documentation are bundled. The complete reference checkout
is not part of this public tree; `/stepper-ninja/` is ignored for optional local
reference use. Historical analysis links to omitted files use upstream permalinks
at the pinned revision.

Verify without any external checkout:

```bash
python3 tests/vendor_integrity.py
# or: ctest --test-dir build -R vendor-integrity --output-on-failure
```

The project's own MIT license is [separate](../../LICENSE); it does not replace
this directory's original upstream copyright and license.
