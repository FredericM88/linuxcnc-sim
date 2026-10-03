# LinuxCNC 2.10 runtime acceptance

This records the operator-reported real runtime acceptance of simulator branch
`linuxcnc-2.10-hal`, compatibility commit `9758646`
("Add LinuxCNC 2.10 HAL compatibility").

## Tested environment and module lifecycle

- LinuxCNC `2.10.0~pre2`, locally built Run-In-Place (RIP).
- POSIX/uspace realtime, `HAL_API_VERSION = 1`.
- LinuxCNC source tree: `~/dev/linuxcnc-2.10-source`.
- Pinned Stepper-Ninja Board-0 UDP profile, upstream revision
  `eb7e5dfa2e76477e606a47038b07cca5e8a4b424`.

The compatibility driver was built from the prepared, pinned source using the
LinuxCNC 2.10 RIP `Makefile.modinc` and copied only into the local RIP rtlib:
`~/dev/linuxcnc-2.10-source/rtlib/stepgen-ninja.so`. The installed system
LinuxCNC 2.9 module at `/usr/lib/linuxcnc/modules/stepgen-ninja.so` was not replaced.

Under LinuxCNC 2.10 HAL API 1, this module-load command succeeded:

```hal
loadrt stepgen-ninja ip_address="192.168.50.2:8888"
```

The component became ready and exported the expected pins, including the new
HAL types `bool`, `real`, `sint` and `uint`. Module unload/cleanup also succeeded.

## End-to-end transport

LinuxCNC at `192.168.50.1` connected successfully to the simulator namespace at
`192.168.50.2`, UDP port `8888`, through the original Stepper-Ninja protocol:
37-byte requests and 61-byte responses.

More than 900,000 packets were exchanged during testing. All reported error
counters remained zero:

| Counter | Result |
|---|---:|
| Invalid packets | 0 |
| Send errors | 0 |
| Length errors | 0 |
| Checksum errors | 0 |
| Timing errors | 0 |
| Position overflows | 0 |
| Packet-ID gaps | 0 |

## Functional acceptance

### XYZ motion, virtual limits and homing

The tested machine position confirmed the expected 400 steps/mm scaling:

| Axis | Position (mm) | Steps |
|---|---:|---:|
| X | 20.000 | 8000 |
| Y | 5.000 | 2000 |
| Z | -10.000 | -4000 |

LinuxCNC 2.10 successfully completed homing using the simulator's virtual
limit/home inputs returned through the Stepper-Ninja UDP feedback path.

### Probe

The virtual probe plane was `Z <= -6000 steps`, on input 28. Starting at
Z = -10 mm, LinuxCNC executed `G38.2 Z-20 F60`. The simulator asserted gp28,
and LinuxCNC stopped the probing move at approximately Z = -15.015 mm.
The simulator showed `Z = -6006 steps = -15.0150 mm`, `Probe: ON`, and
`Wire 28 = 1`. The small overshoot is the observed result of the discrete
realtime cycle; this is not an exact zero-error probe stop.

### Persistent material removal

The workpiece was 30 × 20 × 10 mm with 0.1 mm voxels. The flat-end tool had
a 6 mm diameter and 20 mm cutting length.

The plunge at X15 Y4, from Z -5 to -12 at F120, produced:

- Removed voxels: **56,560**.
- Removed volume: **56.560 mm³**.
- Worker lag: **0.000 ms**.

Starting from X15 Y4 Z-12, the curved-motion test executed:

```gcode
G17
G2 X5 Y4 I-5 J0 F300
```

The final result after the plunge and half-circle was:

- Position: X = 5.000 mm, Y = 4.000 mm, Z = -12.000 mm.
- Removed voxels: **166,400**.
- Removed volume: **166.400 mm³**.
- Worker lag: **0.000 ms**.

The new LinuxCNC 2.10 runtime voxel count exactly matches the frozen synthetic
plunge+arc reference of **166,400** voxels. The previous real LinuxCNC 2.9
acceptance run produced **166,440** voxels for the corresponding run; that
historical result is distinct from the synthetic reference and remains unchanged.

## Conclusion and scope

The LinuxCNC 2.10 HAL API compatibility implementation has now been validated
at runtime, not merely compile-tested, for the project's supported pinned
Stepper-Ninja Board-0 UDP profile. The tested combination is specifically
LinuxCNC **2.10.0~pre2**, **HAL API 1**, and **uspace/POSIX realtime**.
This does not claim testing of every LinuxCNC 2.10 build, realtime backend,
Stepper-Ninja board variant, or future 2.10 release.

This acceptance record does not change the implementation or build logic.
The known upstream checksum/connected-reference bug remains unfixed; zero
checksum errors in this run do not validate recovery from that failure.
