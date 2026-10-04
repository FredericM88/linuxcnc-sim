# linuxcnc-sim

An early experimental standalone CNC machine simulator for LinuxCNC. It emulates
motion hardware behind LinuxCNC, visualizes the machine and stock, and removes
material from a persistent voxel workpiece as the tool moves.

**v0.1.0 is experimental development software. It is not a replacement for
real-machine safety systems.** The current simulator and INI configuration have
been manually accepted against real LinuxCNC, alongside automated regression and
sanitizer tests. This is not a hard-real-time or physical-machine accuracy guarantee.

## Architecture

```text
LinuxCNC
    |
    | original Stepper-Ninja HAL driver
    v
Stepper-Ninja UDP protocol  <---- virtual digital inputs
    |                                      ^
    v                                      |
linuxcnc-sim -------------------------------+
    +-- virtual machine motion
    +-- digital I/O, limit switches and homing interaction
    +-- virtual probe input / probe plane
    +-- OpenGL visualization
    +-- persistent material removal
```

linuxcnc-sim does **not independently interpret G-code**. LinuxCNC owns G-code
interpretation, trajectory planning, coordinate systems, offsets and compensation.
The simulator receives the resulting low-level machine motion and returns
simulated digital inputs. Workpiece positions are in machine space; LinuxCNC
G54/G55, G92 and tool-length offsets are not applied a second time.

## Why Stepper-Ninja?

To simulate the machine behind LinuxCNC, the interface belongs where LinuxCNC
normally communicates with motion hardware. Stepper-Ninja already provides a
LinuxCNC HAL driver, a compact UDP protocol, step-generator commands and
bidirectional digital I/O. linuxcnc-sim emulates the hardware side of that existing
interface instead of introducing another HAL driver and protocol.

```text
LinuxCNC -> Stepper-Ninja HAL driver -> UDP -> linuxcnc-sim
```

The return path matters: virtual limit switches and probe signals can feed back
into LinuxCNC's existing homing and probing logic. The current probe is a point
against a configured plane, **not geometric workpiece probing**.

Stepper-Ninja is the current transport/interface. A physical CNC machine used
later does not have to use Stepper-Ninja hardware. Additional transport adapters
may be explored in the future; v0.1.0 supports this interface only.

## Current features

- Four simulated step axes (X/Y/Z/A), configurable scales and exact integer step
  positions; the supplied LinuxCNC milling example uses XYZ.
- Virtual digital inputs, six configurable limit switches, LinuxCNC homing
  interaction and a separate probe-plane input.
- Motion recorder with CSV export and an interactive terminal console.
- Optional OpenGL 3.3 visualization with orbit, pan, zoom and fit-to-scene controls.
- Configurable machine-space workpiece placement and sparse voxel stock.
- Flat-end cutter, persistent material removal, continuous swept-cylinder removal
  and exact sweep batching without replacing curved motion with endpoint chords.
- Simulator INI configuration, CLI overrides and effective startup configuration
  inspection through `--print-config`.

## Requirements

- Linux on a little-endian system; C11/C++20 compiler, CMake 3.20 or newer, Make
  or Ninja, and GLM.
- For visualization: OpenGL 3.3, GLFW 3 and a working graphical session.
- For tests: Python 3 and Bash. The network-namespace test additionally needs
  iproute2 and unprivileged user/network/mount namespaces.
- For the LinuxCNC example: LinuxCNC userspace, its matching development files,
  and the Stepper-Ninja HAL module built with the pinned Board-0 UDP profile.
  The [HAL compatibility build](docs/hal-api-compatibility.md) supports the 2.9
  and 2.10 APIs. Manual acceptance used LinuxCNC 2.9.10 and, subsequently,
  [2.10.0~pre2 RIP with HAL API 1 and POSIX/uspace realtime](docs/linuxcnc-2.10-runtime-acceptance.md)
  for the pinned Board-0 UDP profile.
- `sudo`/root for veth/network-namespace setup and HAL module installation.
  Ordinary simulator builds and most tests do not need root or LinuxCNC.

On Debian with the LinuxCNC packages available:

```bash
sudo apt-get install git build-essential cmake libglm-dev libgl1-mesa-dev \
  libglfw3-dev python3 patch iproute2 linuxcnc-uspace linuxcnc-uspace-dev
```

## Build

Clone the public repository, then run the remaining commands from the checkout root:

```bash
git clone https://github.com/FredericM88/linuxcnc-sim.git
cd linuxcnc-sim
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build --output-on-failure
```

For the complete graphics suite on a working desktop, configure with
`-DCNC_SIM_RENDER_TESTS=ON`. A build without OpenGL/GLFW is also supported:

```bash
cmake -S . -B build-headless -DCMAKE_BUILD_TYPE=Release -DCNC_SIM_RENDER=OFF
cmake --build build-headless -j4
env -u DISPLAY -u WAYLAND_DISPLAY ctest --test-dir build-headless --output-on-failure
```

CTest reports the namespace test as skipped if the kernel disables the required
unprivileged namespaces. Release validation requires it to run; see the
[validation report](docs/release-validation-v0.1.0.md) for the full matrix.

## Quick Start

Use the separate supplied virtual-machine configuration, with no physical machine
connected. Install dependencies and build as above first.

### 1. Build the Stepper-Ninja LinuxCNC HAL driver

If the matching module is not already installed, obtain the pinned upstream
revision in a **separate sibling checkout**. The project-owned compatibility
build verifies it and patches a private build-directory copy for the installed
LinuxCNC API. Neither upstream nor the bundled vendor snapshot is modified:

```bash
git clone https://github.com/atrex66/stepper-ninja.git ../stepper-ninja-hal
git -C ../stepper-ninja-hal checkout --detach eb7e5dfa2e76477e606a47038b07cca5e8a4b424
cmake -S compat/stepper-ninja -B build/compat-hal \
  -DSTEPPER_NINJA_SOURCE="$PWD/../stepper-ninja-hal"
cmake --build build/compat-hal -j4
ctest --test-dir build/compat-hal --output-on-failure
```

The result is `build/compat-hal/stepgen-ninja.so`; this build has no install step.
For a subsequent manual installation on the Debian userspace package layout:

```bash
sudo install -m 644 build/compat-hal/stepgen-ninja.so /usr/lib/linuxcnc/modules/stepgen-ninja.so
```

That explicit installation replaces an existing module of the same name. Use
headers and build rules from the LinuxCNC installation that will load it. Keep
upstream's default Board-0 UDP profile unchanged: it must match the simulator's
pinned protocol profile. The minimal files in `third_party/stepper-ninja/` are
not a complete HAL driver distribution. See the [compatibility notes](docs/hal-api-compatibility.md)
for a 2.10 source-tree compile check that requires no installation.

### 2. Set up virtual networking

```bash
sudo ./scripts/setup-veth.sh
```

This creates `veth-lcnc` at `192.168.50.1/24` on the host and `veth-sim` at
`192.168.50.2/24` inside `cnc-sim-ns`. These are deliberate example addresses.
The separate namespace lets both endpoints use UDP port 8888; the original HAL
driver binds that port on the host. The script checks resource ownership and
refuses a conflicting route or foreign interface. It does not configure NAT,
IP forwarding or persistent network-manager settings.

### 3. Start the simulator first

From the checkout root in a graphical terminal, enter the namespace as root and
run the simulator as your desktop user, preserving the existing display access:

```bash
sudo ip netns exec cnc-sim-ns \
  runuser -u "$USER" -- env \
  DISPLAY="$DISPLAY" \
  XAUTHORITY="${XAUTHORITY:-$HOME/.Xauthority}" \
  XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}" \
  ./build/cnc-sim --config examples/mill/simulator.ini
```

This is the accepted X11/XWayland launch path. It opens the 3D view with
400 steps/unit, a 30×20×10-mm workpiece, a 6-mm flat-end tool and material removal
**off**. For a headless session, copy the INI alongside the original, set
`[RENDER] ENABLED = false`, and use the existing namespace wrapper:

```bash
# After creating examples/mill/headless.ini with RENDER.ENABLED = false:
sudo ./scripts/run-simulator.sh --config examples/mill/headless.ini --no-stats
```

Keeping the copy in the same directory preserves its relative virtual-I/O path.
With a headless build, set `CNC_SIM_BINARY` to that executable's absolute path
when invoking the wrapper. Start the simulator before LinuxCNC: the original HAL
watchdog does not recover merely because the simulator appears later.

### 4. Start LinuxCNC and observe motion

In another terminal, from the checkout root, as your normal user:

```bash
linuxcnc examples/phase3/phase3.ini
```

In LinuxCNC AXIS, release E-stop (F1), enable the machine (F2), and choose
**Home All**. Keep material removal off and the probe plane disabled during
homing. Observe the simulator connection, increasing RX/accepted/TX counters,
virtual switches and XYZ positions. The example has a 1-ms servo period and
400 steps/mm, with machine travel X=0..600 mm, Y=0..400 mm and Z=-200..0 mm.
X/Y home toward their lower switches; Z homes upward toward its max switch at
+1 mm and then returns to G53 Z=0. Once homing is complete, use LinuxCNC MDI:

```gcode
G21 G90 G40 G49 G80
G53 G1 Z-5 F300
G53 G1 X15 Y4 F300
```

Wait for each move to finish. In the simulator console enter `material on`, then
in LinuxCNC MDI enter `G53 G1 Z-12 F120`. The tool should plunge into the stock
(top at machine Z=-10) and leave a persistent cut. Use `material show` to inspect
removal state and `material off` before further setup moves. Detailed homing,
probe-plane and material procedures are in the [documentation](#documentation).

### 5. Shut down

Close LinuxCNC, enter `quit` in the simulator (or close its rendering window),
then remove the temporary network resources:

```bash
sudo ./scripts/teardown-veth.sh
```

For a standalone scene preview without LinuxCNC or namespace setup:

```bash
./build/cnc-sim --config examples/mill/simulator.ini --bind 127.0.0.1
```

## Configuration

[examples/mill/simulator.ini](examples/mill/simulator.ini) is the practical milling
profile. Precedence is **defaults < INI < CLI < interactive runtime**. Existing
no-config defaults and CLI options remain valid; runtime edits are never written
back to the INI.

Supported sections: `SIMULATOR`, `NETWORK`, `AXIS_X`, `AXIS_Y`, `AXIS_Z`, `AXIS_A`,
`MATERIAL`, `MESH`, `RENDER`, `WORKPIECE`, `TOOL` and `VIRTUAL_IO`.

```bash
./build/cnc-sim --config examples/mill/simulator.ini \
  --material-batch-ms 50 --print-config
./build/cnc-sim --help
```

`--print-config` validates and displays startup settings without opening UDP or
starting workers. Relative INI file references resolve against the INI directory.
`VIRTUAL_IO.CONFIG = ../phase3/virtual-io.conf` keeps sensor definitions in their
existing separate file; CLI `--io-config` paths retain working-directory semantics.
Unknown sections/keys, duplicates and invalid values are rejected with diagnostics.
See the [complete schema and semantics](docs/simulator-configuration.md).
`SPINDLE`, `TOOLSETTER` and geometric `PROBE` sections are unsupported.

## Controls and simulator interaction

| Control / command | Action |
|---|---|
| Left / middle mouse button | Orbit / pan |
| Mouse wheel / Home | Zoom / fit scene |
| `help`, `help io` | Console command syntax |
| `status` | Packet, position and runtime diagnostics |
| `workpiece show` | Current dimensions, placement and voxel size |
| `tool show`, `tool flat-end 6 20` | Inspect / set flat-end diameter and cutting length in mm |
| `material on`, `material off`, `material show` | Enable, disable or inspect cutting |
| `material reset` | Fresh stock with current workpiece settings |
| `workpiece reset` | Restore compiled workpiece defaults |
| `input show`, `limits show`, `probe show` | Inspect virtual I/O |
| `record begin`, `record stop`, `record save motion.csv` | Record and export integrated motion |
| `quit` | Drain pending work and exit |

The terminal supports fixed status display on a TTY and plain stdin/stdout
commands without a TTY. Use `--no-stats` for plain command output. Sensor positions
are in steps; workpiece/tool geometry is in millimetres. Recording does not reset
machine position, and export refuses to overwrite an existing file.

## Current limitations

- Experimental v0.1.0, not a machine safety system or hard-real-time controller.
- Stepper-Ninja Board-0 UDP is the only supported LinuxCNC transport/profile.
- Flat-end cutter only; millimetre geometry; axis A does not transform material.
- Binary voxel-centre removal is resolution-dependent, not CAD-exact machining.
- No geometric stock probing, toolsetter, spindle/holder simulation or collision
  model, general collision detection, or rotary material transform.
- No mechanical dynamics, backlash, cutting forces or physical encoder feedback.
  The original HAL driver's position feedback is derived from its command.
- The 128 input wire bits expose only GP22/26/27/28 and their inverses in this HAL
  profile; the example shares min/max/home signals per axis.
- One material owner; meshing can use multiple workers. Sustained overload can
  grow the lossless motion queue in memory.

## Documentation

- [Simulator configuration: complete schema and CLI compatibility](docs/simulator-configuration.md)
- [Repository layout and vendored dependencies](docs/repository.md)
- [Original transport/protocol analysis](docs/stepper-ninja-protocol.md)
- [Virtual I/O, homing and probe-plane procedures](docs/phase3-test.md)
- [Workpiece coordinates and runtime semantics](docs/phase4a1-design.md)
- [Material-removal design](docs/phase5-design.md)
- [Material validation and real LinuxCNC acceptance](docs/phase5-test.md)
- [Configuration validation and acceptance](docs/phase55-test.md)
- [v0.1.0 release validation](docs/release-validation-v0.1.0.md)
- [v0.1.0 release-notes draft](docs/release-notes-v0.1.0.md)

Earlier phase-specific design and validation documents remain under `docs/` as
technical evidence. Their historical test counts describe their original revisions.

## Roadmap

Possible future work includes geometric workpiece probing, a machine-fixed
toolsetter, a spindle/holder/tool reference model, additional tool geometries,
collision detection and additional transport adapters. None is implemented or
scheduled by this release.

## Third-party attribution

[Stepper-Ninja](https://github.com/atrex66/stepper-ninja) is the upstream project
by Zsolt Viola. linuxcnc-sim uses its original HAL interface and protocol; it did
not author them. The minimal unchanged protocol files and HAL test fragment live
under [third_party/stepper-ninja](third_party/stepper-ninja/UPSTREAM.md), pinned to
`eb7e5dfa2e76477e606a47038b07cca5e8a4b424`. Their original MIT license and
**Copyright (c) 2025 Zsolt Viola** remain separately preserved.

## License

linuxcnc-sim is licensed under the [MIT License](LICENSE),
**Copyright (c) 2026 Frederic Müller**. Bundled Stepper-Ninja code retains its
[own original MIT license and attribution](third_party/stepper-ninja/LICENSE.txt).
