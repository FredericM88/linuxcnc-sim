#include "console/MaterialCommands.hpp"
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace cnc {
std::string describe_material(const MaterialStatus& s, bool compact) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(3)
        << "Material removal: " << (s.enabled ? "ON" : "OFF")
        << " | queue " << s.queue_depth << " / max " << s.max_queue_depth;
    if (s.queue_depth >= 1000 || s.lag_ms >= 1000) out << " | OVERLOAD (lossless backlog)";
    if (!s.error.empty()) out << " | ERROR: " << s.error;
    out << "\nTool: flat-end diameter " << s.tool.diameter_mm << " cutting length " << s.tool.length_mm << " mm";
    if (compact) return out.str() + '\n';
    out << "\nWorkers: material " << s.material_workers << " | mesh " << s.mesh_workers << " | parallel mesh peak " << s.mesh_parallel_max
        << "\nMotion events received: " << s.motion_received << " | after coalescing: " << s.motion_coalesced
        << "\nMaterial batch interval: " << s.material_batch_ms << " ms (0: segmentwise reference)"
        << "\nBatches processed: " << s.removal.batch.batches << " | segments: " << s.removal.batch.segments
        << " | avg: " << (s.removal.batch.batches ? static_cast<double>(s.removal.batch.segments)/static_cast<double>(s.removal.batch.batches) : 0.0)
        << " | max: " << s.removal.batch.max_segments
        << "\nBatch candidate chunks: " << s.removal.batch.candidate_chunks << " | candidate voxels: " << s.removal.batch.candidate_voxels
        << " | occupied: " << s.removal.batch.occupied_candidates << " | chunk-segment refs: " << s.removal.batch.chunk_segment_refs
        << "\nSweep containment tests: " << s.removal.containment_tests << " | batch: " << s.removal.batch.containment_tests
        << "\nBatch envelope rejects: " << s.removal.batch.envelope_rejects << " | prefilter skipped refs: " << s.removal.batch.prefilter_rejected_refs
        << "\nBatch first-hit exits: " << s.removal.batch.first_hit_exits << " | remaining refs skipped: " << s.removal.batch.first_hit_skipped_refs
        << "\nBatch processing: " << s.removal.batch.processing_ms << " ms"
        << "\nMesh queue: " << s.mesh_queue_depth << " / max " << s.mesh_queue_max
        << "\nBroad phase: " << s.removal.broad_ms << " ms | narrow phase: " << s.removal.narrow_ms
        << " ms | voxel mutation: " << s.removal.mutation_ms << " ms | dirty invalidation: " << s.removal.invalidation_ms << " ms"
        << "\nCoalescing: " << s.coalescing_ms << " ms | snapshot: " << s.snapshot_ms << " ms | stale mesh jobs: " << s.stale_mesh_jobs
        << "\nMesh publication: " << s.publication_ms << " ms"
        << "\nSweeps processed: " << s.removal.sweeps
        << "\nEvents processed: " << s.events_processed << " | generation " << s.generation
        << "\nChunks tested: " << s.removal.chunks_tested << " | changed: " << s.removal.chunks_changed
        << "\nVoxels tested: " << s.removal.voxels_tested << " | removed: " << s.removal.voxels_removed
        << "\nRemoved volume: " << s.removed_volume_mm3 << " mm3"
        << "\nDirty mesh chunks: " << s.dirty_chunks << " | mesh rebuild count: " << s.mesh_rebuilds
        << "\nWorker processing: " << s.worker_ms << " ms | meshing: " << s.mesh_ms << " ms"
        << "\nWorker lag: " << s.lag_ms << " ms";
    return out.str();
}
std::string execute_material_command(Simulation& simulation, const std::string& line) {
    std::istringstream in(line);
    std::string first, action, token;
    in >> first >> action;
    MaterialEvent event;
    if (first == "tool" && action == "flat-end") {
        double* values[] = {&event.tool.diameter_mm, &event.tool.length_mm};
        for (auto value : values) {
            if (!(in >> token)) throw std::invalid_argument(material_help);
            std::size_t used = 0; *value = std::stod(token, &used);
            if (used != token.size()) throw std::invalid_argument("expected a millimetre value");
        }
        validate_tool(event.tool); event.kind = MaterialEventKind::Tool;
    } else if (first == "material" && action == "on") event.kind = MaterialEventKind::Enable;
    else if (first == "material" && action == "off") event.kind = MaterialEventKind::Disable;
    else if (first == "material" && action == "reset") event.kind = MaterialEventKind::Reset;
    else if ((first != "tool" && first != "material") || action != "show") throw std::invalid_argument(material_help);
    if (in >> token) throw std::invalid_argument("unexpected material/tool argument");
    if (action == "show") return describe_material(simulation.material_status());
    simulation.material_command(std::move(event));
    return first + " " + action + ": queued at authoritative motion boundary.";
}
} // namespace cnc
