# Frozen curved-material baseline

Captured from the unchanged production algorithm at
`ae13f21c281d05722fd117ad399afc36db67a03e` (before any curved-motion optimization).
Normal tests only read these files. They must not regenerate the expected result
from a potentially optimized implementation and then compare it with itself.

- `arc.steps`: canonical integer endpoint sequence, 400 steps/mm, clockwise lower
  XY semicircle. The first line identifies the format; the second names columns;
  subsequent rows contain `tick X_steps Y_steps Z_steps`. Tick zero is the arc
  start/plunge endpoint; there are 3143 positions and 3142 changed intervals.
- `plunge.rle`, `plunge-arc.rle`: **complete** binary occupancy, not just removed
  counts or hashes. Format `CNC_VOXEL_RLE_V1`, then `nx ny nz`, then decimal
  `value run_length` pairs covering all 6000000 voxels. Order is z outermost,
  y next, x fastest; values are 0 or 255. No unordered container order is used.
- `serial.stats`: deterministic counters for sequential segment replay and an
  offline application of the existing exact coalescer, without wall-clock flush
  boundaries. Format: name, sweeps, chunks tested, chunks changed, voxels tested,
  voxels removed, FNV-1a-64 fingerprint. Enable's above-stock stamp counts as a
  sweep. No meshing or worker scheduling contributes to these fixed counters.

Fingerprint: FNV-1a-64, offset 14695981039346656037, multiplier 1099511628211,
modulo 2^64. Input is nx, ny, nz each encoded as unsigned 64-bit little-endian,
followed by one byte per voxel in canonical order. Geometry configuration is
checked separately. A hash match is never a substitute for the exact cell check.

| Material state | Removed | FNV-1a-64 |
|---|---:|---|
| Plunge only | 56560 | `95c98093e6e51d52` |
| Plunge + arc | 166400 | `585ccf7e9d0561a2` |

SHA-256 of the reference artifacts:

```text
8b1378bf37cbdd260888e89acd72d2cb6d528c06d09aa96acc442364112e77a6  arc.steps
a2c8443de4b3daedde6b74906a894d9d0d9ae13ba47cb0afc723e0d92e247697  plunge.rle
77b669c959513b521688dbc58580f53ead40aba212ed055f7b7bee48cd3bc724  plunge-arc.rle
f0cc40cfa63a0b759c33a5e4ccedad6f1613204a005165e44fb4a3eba0a92be0  serial.stats
```

To check the integer sequence against the documented generator:

```bash
python3 tests/generate_curved_motion.py
```

The script's explicit `--write NEW_FILE` mode refuses to overwrite existing data.
The C++ `record NEW_DIRECTORY` mode is for intentional reference investigation
only; it refuses existing directories and is never called by CTest. When
investigating a future implementation, retain these original fixtures and write
any candidate output elsewhere:

```bash
./build-headless/curved-material-tests record build-curved/candidate-output
```

The fixture was initially captured with GCC 14.2.0, Release. It is also checked
under ASan/UBSan and ThreadSanitizer. See [the test protocol](../../../docs/phase5-test.md)
for configuration, exact sampling, timing semantics and measured results.
