# Phase 5 design

## Performance review of 34fd135

The clean baseline was `34fd1359701b58b02fac77110650945e06e31a05`.
No AGENTS.md applies. The real run proved geometric correctness (296560 removed
voxels), but **real Phase-5 acceptance remains NOT YET PASSED** because of backlog.
The supplied observations were checked against all material, volume, mesher,
renderer, transform, tool, simulation, queue, CLI and test code before editing.

Confirmed: one material jthread; serial candidate/voxel traversal; an event for
every accepted XYZ position change; per-voxel dirty/halo marking; both removal
and SurfaceMesher on that same worker; complete dirty batches blocking further
motion consumption. MotionQueue is already lossless and independent of worker
locks. It is retained unchanged, as are the UDP loop, wire protocol and vendor code.

Qualifications: there is no material event for unchanged XYZ or A-only packets.
4000 is an estimate for constant F300 at 1 ms (5 mm/s, 0.005 mm/update), not a
protocol invariant: step quantization and acceleration determine actual events.
DirtyChunks already deduplicated its set, but repeated insertion/hash/coordinate
work remained. Rendering already consumed immutable complete mesh directories;
the problem was their serial production, not GPU ownership. A halo mesh may need
an update even when that chunk's own material version has not changed.

The staged plan, followed with tests and measurements at each stage:

1. Extend existing diagnostics; reproduce the unchanged baseline with the same
   compiler and workload, preserving a baseline executable locally.
2. A: exact, conservative coalescing on the consumer; retain every turn/barrier.
3. B: one own/face-neighbour dirty mark per changed candidate chunk.
4. C: detach meshing through immutable volume snapshots, initially one mesh thread.
5. D: bounded configurable mesh pool; validate generation and all halo versions.
6. E: only if measurements justify chunk-parallel material mutation. Not implemented:
   the measured material cost after A-D fits comfortably within the four-second
   reference motion. Concurrent writes would require partitioned chunk ownership,
   safe map insertion and deterministic reduction of volume versions, counts and
   dirty sets. There is no evidence that this added complexity is needed yet.

See [phase5-test.md](phase5-test.md) for stage measurements and limits.

## Ownership and data flow

```text
Original UDP -> StepperNinjaProtocol::accept -> int64 MachineState
                  | accepted XYZ change (previous -> current, steps/scales)
                  v
             SPSC MotionQueue<MaterialEvent>
                  v
             exact coalescer (20 ms target collection window)
                  v
             MaterialWorker (one authoritative owner, headless too)
                  -> MaterialRemoval -> SparseVoxelVolume -> DirtyChunks
                  -> immutable volume copy + chunk jobs -> mesh thread pool
                  <- version-checked results -> complete immutable MaterialMeshes
                  v
             main/context thread -> changed GPU chunks -> OpenGL
```

Simulation remains the sole UDP/step/VirtualIO/recorder owner. Material events
are published after accepted integration, before replying; invalid datagrams
never publish motion. Duplicate/out-of-order IDs retain the original protocol's
integration semantics. A copied machine snapshot drives the visible tool,
independently of material latency. No G-code parser, mode guessing, G54/G55
application or tool compensation is added. Step scale must represent steps/mm.

The existing WorkpieceSnapshot remains immutable **raw** stock/configuration,
with its existing revision semantics. MaterialWorker copies that sparse raw
volume once per generation and owns all mutations. `Simulation::volume()` thus
continues to describe raw stock, not current cut material. The diagnostic/test
`material_snapshot()` queues a barrier and copies current material on the worker;
it is not part of the packet or rendering hot path. Prior snapshots stay valid.

Workpiece validation/allocation remains on the console caller. A valid replacement
is queued through the existing command mailbox at a packet boundary, then its
configuration snapshot is published to console readers. Thus even a configuration
edit while cutting has a defined position in the material history. UDP never
acquires the workpiece or material-publication mutex, nor performs voxel, meshing,
GPU, full-volume-copy or mesh-directory work. The existing short UI mailbox is
unchanged in purpose. No vendor or wire implementation changes are needed.

## Motion FIFO, ordering and overload

`MotionQueue` is an unbounded linked SPSC FIFO with a dummy node and acquire/release
publication. There is exactly one producer (UDP thread, including console requests
forwarded through its mailbox) and one consumer (material worker). Nodes are
reclaimed iteratively by the consumer. No shared material lock, capacity wait,
overwrite, eviction or approximate path simplification is used.
XYZ-unchanged packets and A-only changes do not enqueue sweeps: repeated identical
occupancy contributes nothing. Enabling and changing tools explicitly stamp the
current authoritative position, covering the zero-length case.

All XYZ movement events, including OFF movement, retain their order relative to
ON/OFF, tool, reset and workpiece events. OFF motion is consumed without removal;
the next ON stamps only its current position, never the intervening disabled
travel. Command replies say **queued**: they acknowledge the packet boundary,
not completion of expensive worker processing. `material show` shows the processed
state; wait for ON/OFF and a drained queue when performing interactive acceptance.

The consumer drains up to 65536 input events or approximately 20 ms per pass.
An accumulated motion is flushed on a nonmergeable segment, any control/capture
barrier, shutdown, or after a 20 ms collection window. The window is measured from
its first dequeue; it does not restart when more segments arrive. Deadline checks
run once per consumer pass, so this is a target, not a strict latency bound. A sweep is still
indivisible and may exceed that budget. This also coalesces a live 1 kHz stream
instead of only an already accumulated backlog. At shutdown every segment is
flushed. There is no producer-side timer or extra producer synchronization.

Mesh completion is polled without waiting. During mesh jobs, the owner continues
motion consumption and records new dirty chunks. It sleeps 1 ms if no input was
consumed, avoiding a busy wait. Diagnostic publication is approximately 20 ms.
Shutdown stops/joins UDP first, then drains coalescing, material, dirty and mesh
work before joining the material owner and mesh pool. Allocation failures or mesh
exceptions produce the existing fatal/incomplete state, not silent lost geometry.

### Exact coalescing rule

Only consecutive Motion events with exactly equal numeric connection coordinates
(`previous.to == next.from`) can merge. Every other event kind is a barrier,
including Capture, repeated Enable, Tool, Disable, Reset and Workpiece.
For A -> B -> C, B must lie coordinate-wise between A and C, preventing reversal.
All coordinates must be finite and within the existing supported range.

Axis-aligned motion is proven directly by constant coordinates and monotonicity.
Zero-length segments add no geometry and can join a valid connected run.
For general XYZ, subtraction uses an error-free residual check: both B-A and C-B
must be exactly representable. All three cross-product equalities must hold as
**exact products**, checking both rounded product and FMA residual. Nonzero
operands outside [2^-400, 2^400] are rejected to exclude product/residual underflow.
This relies on IEEE binary64, normal rounding and the project's non-fast-math build.
There is no angle/distance epsilon. Inexact differences or uncertain cases retain
the original segments. Thus even mathematically intended diagonal decimal samples
may remain separate if their represented coordinates are not proven collinear.
Exactly representable diagonal XYZ lines (including negative directions) merge.

The union of translated fixed-axis cylinders along connected, collinear,
equidirectional segments equals the cylinder sweep along the combined segment.
The existing spatial sweep tolerance and inclusion predicate are unchanged.
Corners, reversals, discontinuities and arc-like polylines remain complete.
No every-Nth-packet filtering, curve fitting or chord approximation is performed.

Current/max FIFO depth includes control and diagnostic events, but excludes the
currently executing event. `OVERLOAD (lossless backlog)` appears at depth >=1000
or measured dequeue lag >=1000 ms. Ordinary overload grows RAM usage and delays
material/graphics; it never drops required geometry; merging preserves the swept union. There is one small
allocation per queued event. The allocator and OS scheduler do not provide a
hard real-time guarantee; the design guarantees independence from worker locks
and expensive material/mesh work, not a worst-case userspace allocation latency.

Finite memory cannot hold unlimited overload. Allocation/publication failure or
a geometry/worker exception is a **fatal, visibly incomplete simulation**, not a
successful result with missing cuts. UDP stops accepting further work when the
worker's atomic failure flag is observed; process exit is nonzero. Pending capture
promises are failed during teardown. Recovery is a new simulation run; there is
no bounded overflow policy, disk spool or automatic recovery that could hide a
missing segment. The failure test exercises this explicit path using an
out-of-range material pose; it does not claim to exhaust system RAM.

## Tool and exact continuous sweep predicate

The sole implemented cutting geometry is `ToolKind::FlatEndMill`, with finite
positive `diameter_mm` and `length_mm` (cutting length), each <=1e12 mm. Existing
ToolDefinition is extended rather than introducing another tool representation.
Its reference point is the **bottom-centre tip** in G53 machine space. The cutting
cylinder extends along **+machine Z**; default diameter/length are 6/20 mm. Future
tool kinds can dispatch their own predicate; no shaft, holder or collision
geometry is implemented. A is retained in ToolPose but does not rotate stock/tool.

Let endpoint tips be A and B, d=B-A, radius r=D/2 and cutting length L. For a voxel
centre P, let q=P-A. At time t in [0,1], inclusion in the untranslated-axis cylinder
means:

```text
0 <= q.z - t*d.z <= L
(q.x - t*d.x)^2 + (q.y - t*d.y)^2 <= r^2
```

The implementation uses a fixed spatial tolerance e=1e-9 mm:

1. Intersect [0,1] with `-e <= q.z - t*d.z <= L+e`. If d.z=0, retain [0,1]
   when q.z is inside that interval, otherwise reject. For nonzero d.z, compute
   `(q.z-L-e)/d.z` and `(q.z+e)/d.z`, sort them, and clip to [0,1]. Reject if empty.
2. Minimize squared XY distance on that interval [lo,hi]. If
   `s=d.x^2+d.y^2 > 0`, use
   `t*=clamp((q.x*d.x+q.y*d.y)/s, lo, hi)`; otherwise choose lo.
3. Remove the material voxel exactly when
   `(q.x-t*d.x)^2+(q.y-t*d.y)^2 <= (r+e)^2`.

This is an analytic existential test over the entire segment, not discrete stamps
or arbitrary subdivision. It handles horizontal, vertical, diagonal, reversed,
short, long and zero-length motion. Arithmetic for clipping and minimization uses
`long double` from double endpoints. No arbitrary near-zero velocity threshold
changes the geometry. Collinear subdivision describes the same union of cylinders;
tests require **identical binary occupancy**, including diagonal and randomized
paths. This does not equate different polygonal approximations of a curved path.

## Broad phase, coordinates and accuracy

The machine sweep AABB is `min(A,B)-(r,r,0)` through `max(A,B)+(r,r,L)`.
All eight corners are transformed using the existing `WorkpieceTransform::to_local`.
That remains conservative for the pre-existing static legacy orientation, without
assuming a local-Z cutter. A padding of `4e + 64*DBL_EPSILON*M` is applied, where M
is the maximum of 1, endpoint/translation magnitudes and tool dimensions. This
protects broad-phase outward rounding; it does not enlarge the narrow predicate.
Bounds are clipped to the voxel grid before integer conversion. Only overlapping
chunks and their intersecting voxel index boxes are tested; Empty chunks are
skipped. There is no full-stock scan per segment. A very long sweep crossing the
whole stock can naturally have a large candidate box.

A voxel's local centre is `voxel_to_local(index) + (h/2,h/2,h/2)`. The same existing
transform maps that centre to machine space for the predicate. Thus the cutter
always remains aligned with machine Z, including for a statically rotated legacy
stock. Phase-4A.1 remains `machine_min=position-origin`, `machine_max=machine_min+size`.
There is no parallel origin/offset implementation and no application of G54/G55.

Only centres are classified. A voxel becomes wholly empty if its centre is
included; intersection with just its corner does not remove it. Binary boundary
uncertainty lies within a voxel half diagonal `sqrt(3)*h/2` (about 0.0866 mm at
h=0.10 mm), plus predicate/coordinate rounding. Sub-voxel features may disappear.
Grid-aligned groove width/depth tests use h-derived tolerances. Non-divisible raw
stock sizes still round outward by less than one voxel per axis, as in Phase 4A.
At extreme supported coordinates, double spacing may exceed e or h; e is not a
promise of nanometre physical accuracy. Supported material tips are finite within
+/-1e12 mm. This is not CAD-exact geometry or a mechanical simulation.

The sweep joins integrated packet endpoints by straight lines. Motion between
those endpoints is not independently transmitted; the simulator neither recreates
LinuxCNC trajectory planning nor invents missing UDP motion after a wire loss.
Existing packet-gap/error diagnostics remain relevant.

## Sparse mutation, invalidation and immutable rendering

Only 255 -> 0 is allowed. Values 1..254 remain reserved. An implicit full chunk
materializes a 32 cubed array on its first actual removal; a partial boundary
chunk initializes valid stock cells and empty outside cells. No-hit candidates
allocate nothing. Occupancy counts collapse fully removed chunks to Empty and
release their arrays. Global and per-chunk material versions increment per actual
voxel change, never for an identical repeat cut.

Dirty invalidation collects a six-bit boundary mask from the removed voxels in
each candidate chunk. The own chunk is marked once and each touched in-stock face
neighbour once. The six-face halo semantics are unchanged; no edge/corner halo is
needed by SurfaceMesher. The existing face-area and X/Y/Z boundary tests remain.

### Snapshot, versions and publication

A consistent material state is the owner's completed sweep/control-event prefix.
At scheduling, it deep-copies the sparse material volume once for an entire dirty
batch. Mesh threads read only this immutable copy, including implicit stock and
all neighbour samples. They never access mutable engine data. Snapshot time is
measured separately; the copy pauses only the material owner, never UDP.

Each job identifies a generation, chunk and snapshot versions of the chunk and
its six face neighbours. Reset/workpiece replacement advances generation.
When the batch completes, the owner checks generation and all seven versions.
An older job cannot replace a mesh for a changed chunk or halo: it is rejected
and the current-generation chunk is marked dirty again. A job for an abandoned
generation is simply discarded (the replacement generation already dirtied all
chunks). Unaffected jobs remain valid even when unrelated material has changed.

Only the material owner writes the staged mesh directory. A dirty chunk is fully
processed once a result matching its latest material/halo state is accepted and
no later invalidation remains. Updates arriving during a job accumulate in the
existing deduplicated DirtyChunks set, coalescing obsolete mesh work. A conservative
extra rebuild can occur when a valid result overlaps a newly marked dirty entry.

A complete directory is published only with no active batch and no dirty entries.
Its `material_version` is the current global volume version, and its generation
and monotonic publication revision prevent ambiguity across reset. Every mesh in
that directory matches this committed state, including both sides of boundaries.
Later mutation can make the **already visible** snapshot historical; it remains
immutable and coherent until replacement. No new stale directory is published.
Renderer can skip directories safely because each is complete; shared chunk mesh
identities retain the existing selective GPU-upload behavior.

### Thread safety and configuration

`--material-workers 1` is the sole supported setting; other values fail explicitly.
`--mesh-workers N` accepts 1..32; 0 (default) selects
`min(4, hardware_concurrency > 2 ? hardware_concurrency - 2 : 1)`.
This reserves two logical CPUs in the count when available, not via CPU affinity.
The UDP and material owner threads are additional to the mesh pool.

The mesh pool has one batch, a short job-dispatch mutex/condition variable, and
preallocated independent result slots. Each slot has exactly one writer.
An acquire/release completion counter makes all completed slots visible to the
material owner before it reads them. Worker exceptions travel in result slots.
Pool initialization failure also follows the material failure path; partial thread
creation and shutdown join all created threads. No material map, voxel array,
occupied/version/removal counter or DirtyChunks object has concurrent writers.
The existing publication mutex protects only status and immutable directory pointer
exchange with console/renderer. None of these locks is acquired by UDP.

At most one mesh batch plus a deduplicated next dirty set is scheduled. Unlike
motion, obsolete mesh work may be discarded. Snapshot memory is proportional to
**stored** sparse voxels, not the implicit raw box; deep copies can become expensive
for large, extensively cut volumes. Copy-on-write or chunk-plus-halo snapshots are
a possible next step if snapshot measurements justify it. Sustained workloads
faster than the mesh pool can still delay complete coherent publication; no hard
frame-rate or latency guarantee is made. Material keeps progressing in that case.

Renderer uploads only meshes whose shared identities changed. An empty mesh
removes old GPU geometry. Reset/configuration changes replace the generation and
clear old GPU chunks. Camera interaction and actual machine/tool positioning remain
on the context thread, independent of CPU removal/meshing. Only new generations
request Home/Fit; ordinary cutting does not reset the camera. A synchronous
`set_workpiece` compatibility path remains for standalone scene tests; production
uses worker meshes and does no CPU extraction on the GL thread.

## Commands, reset and diagnostics

```text
tool show
tool flat-end DIAMETER CUTTING_LENGTH
material show
material on
material off
material reset
```

Material starts OFF. ON at a stationary tool performs one static sweep; repeated
ON while already enabled is idempotent. A tool change while ON stamps the new
cutting geometry at the current tip. OFF preserves all already removed material.
Neither spindle state nor G0/G1 is inferred from the low-level interface.

`material reset` restores raw stock from the current configuration, including a
legacy static transform, at its ordered event boundary. Tool and enable state are
retained. It resets removal counters/material versions and advances material
generation; it does not change machine position, workpiece configuration revision
or recorder state. Reset itself does not cut the fresh stock. Subsequent actual
motion cuts again if ON; toggling OFF then ON stamps a stationary tool again.
`workpiece` edits still create new raw stock, and `workpiece reset` still selects
the original default configuration. These are intentionally distinct commands.

`material show`, periodic/final statistics and the existing compact dashboard
extend the same MaterialStatus diagnostics. Detailed output includes motion events
received by the consumer, events after coalescing, sweeps, chunks/voxels tested and
changed/removed, queue current/max, mesh queue current/max, rebuilds, discarded stale
jobs, material/mesh worker counts and observed simultaneous mesh-build peak.

Timers distinguish broad AABB/clipping, narrow sampling/inclusion/hit collection,
voxel mutations, dirty-mask/marking, event processing, coalescing/dequeue overhead,
sparse snapshot copies, mesh builds and publication. Clocks are read per chunk or
batch, never per voxel or additionally on UDP. Candidate chunk loop bookkeeping
is part of total processing, not all attributed to broad-phase time. Mesh time is
the **sum of elapsed job durations**, which can exceed wall time with parallelism;
it is not an OS CPU-time measurement. Publication measures CPU directory/pointer
work, not GPU uploads or screen latency. Status can trail the newest timing sample
by one publication interval.

Removal counters/timers are per generation; event counts, other timers, rebuilds
and high-water marks are lifetime totals. The mesh queue counts queued/running jobs
plus pending dirty entries (these may name the same chunk). Dirty count additionally
includes a completed batch until it is collected and validated. Motion FIFO depth
excludes its executing/coalescing event; queue 0 alone is not a capture barrier.
Worker lag is the age of the last dequeued event or pending coalesced event, zero
when fully idle, not a display latency measurement. For an exact material checkpoint
use the Capture barrier; shutdown drains material and meshes completely.
