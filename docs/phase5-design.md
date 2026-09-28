# Phase 5 design

## Implementation plan (before code changes)

Baseline inspected: 1a2133a, tracked tree clean; the supplied Phase-5 specification
was the sole untracked input and will be retained in the implementation commit.
No AGENTS.md applies. Existing Simulation owns the authoritative UDP integration
thread; WorkpieceSnapshot publishes immutable raw stock/configuration revisions;
SurfaceMesher already samples six face neighbours across chunk boundaries.
DirtyChunks exists but has no material mutation/version implementation yet.
Renderer currently builds all CPU meshes itself on workpiece replacement.

1. Extend SparseVoxelVolume with binary erasure, occupancy counts and versions;
   retain implicit stock, 32 cubed chunks, Empty/Solid/Mixed and reserved values.
2. Extend ToolDefinition with validated flat-end cutting geometry and an analytic
   continuous inclusion predicate. Reuse WorkpieceTransform for broad/narrow phase.
3. Add a single-owner material engine and a lossless SPSC event queue. The UDP
   producer publishes accepted actual motion and ordered tool/enable/reset/stock
   events. The consumer alone mutates material and builds dirty meshes. No material
   or renderer lock is acquired by UDP. Allocation exhaustion is a visible fatal
   error, never silent overflow/drop or continued apparently valid simulation.
4. Publish immutable full mesh directories with shared per-chunk meshes: renderer
   can skip snapshots safely, compare chunk identities and upload only changes.
   Workpiece raw/config snapshots retain existing semantics; material is separate
   mutable state owned by the worker, never by OpenGL.
5. Integrate commands and diagnostics; reset uses current raw configuration and
   an ordered event boundary. Material starts OFF. No inference from G0/G1.
6. Add deterministic geometry, mutation, meshing, queue, UDP, CLI and renderer
   tests; run full graphics/headless suites and integrity checks; document results
   and manual acceptance (NOT YET PERFORMED); review and create one local commit.

Detailed mathematics, queue semantics and measured validation follow below.

## Ownership and data flow

```text
Original UDP -> StepperNinjaProtocol::accept -> int64 MachineState
                  | accepted XYZ change (previous -> current, steps/scales)
                  v
             SPSC MotionQueue<MaterialEvent>
                  v
             MaterialWorker (one thread, headless too)
                  -> MaterialRemoval -> SparseVoxelVolume
                  -> DirtyChunks -> SurfaceMesher
                  -> immutable MaterialMeshes directory
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
overwrite, eviction, path simplification or endpoint-only merging is used.
XYZ-unchanged packets and A-only changes do not enqueue sweeps: repeated identical
occupancy contributes nothing. Enabling and changing tools explicitly stamp the
current authoritative position, covering the zero-length case.

All XYZ movement events, including OFF movement, retain their order relative to
ON/OFF, tool, reset and workpiece events. OFF motion is consumed without removal;
the next ON stamps only its current position, never the intervening disabled
travel. Command replies say **queued**: they acknowledge the packet boundary,
not completion of expensive worker processing. `material show` shows the processed
state; wait for ON/OFF and a drained queue when performing interactive acceptance.

At most 256 events, or approximately 20 ms of event work, are handled per batch;
a single sweep is indivisible and may exceed that budget. Each pass then rebuilds
at most eight dirty meshes. Further motion consumption pauses until all dirty
meshes from that batch are complete, publishing diagnostics between slices.
This guarantees coherent progressive render checkpoints during sustained cuts;
only the material consumer pauses, never UDP. Diagnostic publication is approximately 20 ms; the
worker sleeps 1 ms only when idle. Shutdown first stops/joins the producer, then
drains every pending event and dirty mesh before joining the material worker.
Shutdown can take longer if substantial material work remains.

Current/max FIFO depth includes control and diagnostic events, but excludes the
currently executing event. `OVERLOAD (lossless backlog)` appears at depth >=1000
or measured dequeue lag >=1000 ms. Ordinary overload grows RAM usage and delays
material/graphics; it never drops or merges required geometry. There is one small
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

DirtyChunks deduplicates changed chunks. A removed cell on local index 0 or 31
also invalidates the in-stock face neighbour on that side. SurfaceMesher samples
only the six face neighbours; no edge/corner halo is required for this mesher.
A neighbour mesh can change while its material version remains unchanged.
The boundary test compares total mesh area against exhaustive exposed voxel faces
and verifies newly exposed faces in an unchanged adjacent solid chunk.

CPU meshes are immutable shared per-chunk values in a complete MeshDirectory.
Changed mesh identity, snapshot revision and generation identify updates. The
worker publishes a mesh directory **only when every currently dirty chunk has
been rebuilt**, so a snapshot never mixes old neighbour faces with new cut faces.
Partial internal rebuilds are not rendered. The previous coherent image remains
visible during heavier work; a large batch can delay the next visual update. New motion is consumed only after
this coherent mesh checkpoint, so sustained motion cannot starve publication.
Skipping renderer publications is safe because each directory is complete.

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

`material show`, the compact dashboard and periodic/final statistics expose:

- Processed ON/OFF and tool diameter/cutting length.
- Sweeps, current/maximum queue depth, overload indication, event count/generation.
- Chunks tested/changed, occupied voxel centres tested, voxels actually removed.
- `removed_volume_mm3 = removed_voxels*h^3`, pending dirty chunks and mesh rebuilds.
- Cumulative event processing and meshing time; last event's dequeue lag in ms
  (zero once idle; not an end-to-end render latency measurement).
- Explicit fatal material error if processing cannot remain complete.

Removal counts are per material generation; events, mesh rebuilds, maximum depth
and processing/mesh times are process-lifetime totals. A tested/changed chunk can
be counted once in each sweep. `queue 0` excludes an in-flight event; for a complete
interactive checkpoint also wait for stable counters and zero dirty chunks.
