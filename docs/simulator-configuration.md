# Simulator configuration (Phase 5.5)

`simulator.ini` describes the simulator's startup scene, axis scales, network and
worker settings. It is independent of the LinuxCNC INI and the separate
`virtual-io.conf` sensor-command file. No configuration file is required.

```bash
./build/cnc-sim --config examples/mill/simulator.ini
./build/cnc-sim --config examples/mill/simulator.ini --material-batch-ms 50 --print-config
```

The first command uses the configured address `192.168.50.2`; run it in the
existing configured network namespace for LinuxCNC, as in the
[accepted Phase 5 procedure](phase5-test.md). For a local scene preview:

```bash
./build/cnc-sim --config examples/mill/simulator.ini --bind 127.0.0.1
```

The example explicitly selects rendering, 400 steps/unit, four mesh workers and
the accepted 30×20×10-mm milling scene. Material remains **off** during homing;
position the machine and use the existing `material on` command to start cutting.
No new LinuxCNC acceptance run is claimed by this configuration-only change.

## Precedence and inspection

**compiled defaults < INI < CLI < interactive runtime commands**

For example, the material batch default is 20 ms. `BATCH_MS = 10` sets 10 ms;
`--material-batch-ms 50` then sets 50 ms. The position of `--config FILE` among
CLI arguments does not affect this order. Only one `--config` is accepted.
Repeated ordinary CLI settings retain their existing last-option-wins behavior.
Repeated `--io-config` options retain their existing ordered concatenation behavior;
the first CLI I/O file replaces the INI I/O file, and further CLI files append.

`--print-config` validates and prints the effective **startup** configuration,
then exits without binding UDP, starting workers or opening an OpenGL window.
It works without a display. An enabled renderer still requires a render-capable
build. The output includes types and scale units, legacy display labels, resolved
mesh worker count, workpiece numeric origin and bounds, rotation, tool and I/O
file paths. It is diagnostic text, not a round-trippable INI or an export command.
Per-field provenance annotations are not included.

Existing runtime commands override mutable workpiece, tool, material and virtual
I/O state. Use `workpiece show`, `tool show`, `material show`, `input show`,
`limits show` and `probe show` to inspect current runtime state. Startup configuration
is not rewritten. No interactive change is saved to disk. The existing
`workpiece reset` restores compiled workpiece defaults; `material reset` rebuilds
stock from the current workpiece and retains tool/enabled state. Worker counts,
axis scales and network settings have no new runtime setters.

## Complete schema

All sections and keys are optional. Values not supplied inherit the existing
compiled defaults. Names, enum strings and booleans are case-sensitive. Surrounding
whitespace around section names, keys and values is ignored. Blank lines and
whole-line `#` or `;` comments are accepted; inline comments, quotes, interpolation,
includes and environment expansion are not supported. Numeric vectors have exactly
three comma-separated components; components may have surrounding whitespace.

| Section | Key | Compiled default | Accepted values / meaning |
|---|---|---|---|
| `SIMULATOR` | `UNITS` | `mm` | Currently only `mm`; no automatic length conversion |
| `SIMULATOR` | `STATS_INTERVAL_MS` | `0` | `0`: normal interactive terminal, no scrolling stats; `100..3600000`: periodic legacy statistics, noninteractive terminal |
| `NETWORK` | `BIND_ADDRESS` | `192.168.50.2` | Numeric IPv4 address |
| `NETWORK` | `PORT` | `8888` | Integer `0..65535`; `0` asks the OS for a port, preserving test usage |
| `AXIS_X` | `TYPE` | `linear` | `linear` or `rotary` |
| `AXIS_X` | `STEPS_PER_UNIT` | `1000` | Finite positive scale |
| `AXIS_Y` | `TYPE` | `linear` | `linear` or `rotary` |
| `AXIS_Y` | `STEPS_PER_UNIT` | `1000` | Finite positive scale |
| `AXIS_Z` | `TYPE` | `linear` | `linear` or `rotary` |
| `AXIS_Z` | `STEPS_PER_UNIT` | `1000` | Finite positive scale |
| `AXIS_A` | `TYPE` | `rotary` | `linear` or `rotary` |
| `AXIS_A` | `STEPS_PER_UNIT` | `1000` | Finite positive scale |
| `MATERIAL` | `ENABLED` | `false` | `true` or `false`; enabling at startup also cuts at the initial zero-step pose |
| `MATERIAL` | `BATCH_MS` | `20` | Integer `0..1000`; `0` is the existing segmentwise mode |
| `MATERIAL` | `WORKERS` | `1` | Must remain `1` |
| `MESH` | `WORKERS` | `0` (auto) | Integer `0..32`; auto reserves two logical CPUs, uses at least one worker and caps at four |
| `RENDER` | `ENABLED` | `false` | `true` or `false`; OpenGL support required when true |
| `WORKPIECE` | `SIZE` | `50,50,10` | Positive finite physical dimensions in mm |
| `WORKPIECE` | `POSITION` | `0,0,0` | Machine-space position of the selected reference point, mm |
| `WORKPIECE` | `ORIGIN` | `0,0,10` | Per-axis `min`, `center`, `max` or numeric mm offset in `[0,size]`; mixed forms accepted |
| `WORKPIECE` | `VOXEL_SIZE` | `0.10` | Positive finite voxel edge length in mm |
| `TOOL` | `TYPE` | `flat-end` | Only `flat-end` is implemented |
| `TOOL` | `DIAMETER` | `6.0` | Finite mm in `(0,1e12]` |
| `TOOL` | `CUTTING_LENGTH` | `20.0` | Finite mm in `(0,1e12]`, extending along machine +Z from the bottom-centre tip |
| `VIRTUAL_IO` | `CONFIG` | no file | Nonempty filename of an existing, readable sensor-command file |

Physical volume/index limits and the existing 65536-chunk budget also apply.
Defaults come from the existing `WorkpieceConfig`, `SceneConfig`, `ToolDefinition`
and `MaterialWorkerConfig`, not from the example values.

## Units, axes and machine coordinates

Axis configuration is explicitly named X/Y/Z/A and retains the four-axis wire
order. Linear scales mean steps/mm; rotary scales mean steps/degree. `TYPE` is
configuration metadata, not a kinematics implementation. The existing milling
scene still consumes X/Y/Z as Cartesian millimetres; configure those axes as
linear for milling. A is retained but does not rotate or transform material.
Describing another axis as rotary does not add rotary kinematics, a table transform
or a new material path.

For compatibility, no-config display/CSV labels remain `mm,mm,mm,unit`. Explicit
INI `TYPE` selects the corresponding `mm` or `deg` display label for that axis.
The legacy CLI `--units X,Y,Z,A` can override labels without altering physical
units, axis types or scales. Rendering retains its requirement for XYZ `mm` labels.
Only millimetre geometry is supported in this phase; arbitrary CLI labels still
perform no conversion.

LinuxCNC owns **G54/G55/etc., G92, G41/G42, G43/tool-length compensation, G-code
interpretation and trajectory planning**. The simulator receives the resulting
machine motion. `WORKPIECE.POSITION` is in machine space. There are no simulator
G54/G55/G92/G43 keys, and offsets must not be applied a second time.

Workpiece placement reuses Phase 4A.1:

```text
machine_min = position - origin_offset
machine_max = machine_min + size
```

For `SIZE = 30,20,10`, `POSITION = 15,10,-10`, `ORIGIN = center,center,max`,
the origin becomes `(15,10,10)` mm and the bounds become `(0,0,-20)` to
`(30,20,-10)` mm. Symbols resolve **once**, using the INI size independent of key
order, before CLI overrides. Later size changes preserve that numeric origin;
shrinking below it is rejected, just like interactive workpiece edits. An INI
that supplies workpiece keys uses the documented numeric defaults for omitted
workpiece keys, including origin `(0,0,10)`; specify an appropriate origin when
configuring stock thinner than 10 mm.

## Files and legacy CLI

Relative `VIRTUAL_IO.CONFIG` paths resolve against the directory containing the
INI, and are displayed as normalized absolute paths. They do not depend on the
process working directory. Absolute paths remain absolute. For the example at
`examples/mill/simulator.ini`, the reference is `../phase3/virtual-io.conf`.
Future simulator INI file references should follow the same rule. Sensor content
continues to use the established step-coordinate syntax; it is not embedded in
the simulator INI. CLI `--io-config` paths retain their process-working-directory
semantics, as do recorder save paths.

Every previous CLI option remains available:

```text
--bind --port --steps-per-unit --units --io-config
--stats --stats-ms --no-stats --verbose
--material-workers --material-batch-ms --mesh-workers --render
--voxel-size --stock-size --stock-origin --stock-rotation --help
```

`--steps-per-unit` maps four values onto X/Y/Z/A. The existing CLI accepts finite
**nonzero signed** scales; this is deliberately preserved for compatibility,
while INI scales must be positive. Port zero is similarly retained. Neither
changes Stepper-Ninja wire data.

Legacy-only options live in the central model without adding new INI keys:
interactive/verbose terminal state, per-axis display labels, ordered I/O files,
and Euler stock rotation. `--stock-origin` remains the **minimum-corner
translation**, overriding both INI position/origin placement with the legacy
reference point. `--stock-size` without any INI workpiece keys retains that
minimum corner exactly, including the historical Z-origin adjustment. With INI
workpiece placement it changes size and retains the numeric origin/position.
`--stock-rotation` remains Euler degrees in Rz·Ry·Rx order about the stock minimum;
a rotated scene selects that corner as reference point. As before, the first
successful interactive workpiece edit clears legacy rotation.

There are no new CLI setters for tool dimensions or material enable; those are
INI settings and existing runtime commands. `--render` can enable rendering;
there is no legacy CLI disable switch. Use `RENDER.ENABLED = false` for a headless
INI. `--no-stats` disables the fixed terminal and scrolling stats regardless of INI.

## Validation and architecture

Malformed syntax, unknown sections/keys, duplicate sections/keys, invalid enums,
non-finite or malformed numbers, invalid vector lengths and invalid effective
geometry/worker/network settings are rejected before UDP or simulation threads
start. Diagnostics include filename, line, section and key for INI values; joint
validation failures list the contributing configured locations. CLI diagnostics
identify the option. Syntax and scalar conversion are checked while loading;
constraints on combined values are checked after merging. Referenced I/O files
are read only for the effective configuration, so a CLI I/O override replaces
an unused INI reference. Bad I/O content additionally reports its own filename
and line. No unknown future key is silently ignored.

`src/config/SimulatorConfig.*` owns the typed model, small self-contained INI
reader, CLI parsing/merge, diagnostics, validation and startup display. Axis types
and scales are together in `AxisConfig`; network and I/O have their own models.
Existing workpiece, tool and worker types remain authoritative, avoiding duplicate
geometry or thread-limit definitions. The validated config owns a prepared immutable
workpiece snapshot. `main.cpp` only consumes it and dispatches startup/runtime UI.
The shared `workpiece_from_scene` adapter preserves legacy scene transforms exactly.
Initial tool/enable controls enter the existing ordered material event path before
UDP motion can be produced. Material algorithms, worker count, batching and wire
protocol remain unchanged.

`SPINDLE`, `TOOLSETTER` and `PROBE` are future Phase 6 concepts, not accepted sections.
This phase adds no spindle/holder simulation, geometric probing, toolsetter,
new cutters, collision detection, offset handling or rotary material transform.
