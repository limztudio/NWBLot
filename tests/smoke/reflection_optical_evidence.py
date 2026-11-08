"""Validate completed optical transport and queue evidence."""

import re

from reflection_statistics import ReflectionStatistics
from reflection_optical_reference import DENSE_CASES
from reflection_smoke import DEFAULT_RAY_BUDGET
from window_capture_smoke import SmokeFailure


LIT_OPTICAL_GROUP_ENTRY = "optical_group_entry"
LIT_OPTICAL_CSG_GROUP_ENTRY = "optical_csg_group_entry"
LIT_OPTICAL_CSG_CAP = "optical_csg_cap"
LIT_OPTICAL_UNSPECIFIED_REFERENCE = "optical_unspecified_reference"
LIT_OPTICAL_CSG_UNSPECIFIED_CAP = "optical_csg_unspecified_cap"
LIT_SINGLE_TASK_PACKETS = "single_task_packets"
LIT_GRAPHICS_PACKETS = "graphics_packets"
LIT_OPTICAL_CSG_OPEN_TAIL = "optical_csg_open_tail"
LIT_FRAME = "frame"
LIT_CANDIDATES = "candidates"
LIT_EFFECTIVE_BUDGET = "effective_budget"
LIT_HARDWARE_RAYS = "hardware_rays"
LIT_COMPLETED_FRAMES = "completed_frames"
LIT_CONTRACT = "contract"
LIT_OPTICAL_CSG_REFERENCE = "optical_csg_reference"
LIT_REFLECTIONSMOKESLICEPACKETS = "ReflectionSmokeSlicePackets:"
LIT_SOURCE_FRAME = "source_frame"
LIT_DEVICE_GENERATION = "device_generation"
LIT_OPTICAL_CSG_DENSE = "optical_csg_dense"
LIT_HARDWARE_HITS = "hardware_hits"
LIT_OPAQUE_PIXELS = "opaque_pixels"
LIT_GLASS_PIXELS = "glass_pixels"
LIT_FALLBACK_PIXELS = "fallback_pixels"
LIT_OPTICAL_SLIVER = "optical_sliver"
LIT_OPTICAL_CSG_SLIVER = "optical_csg_sliver"
LIT_OPTICAL_SUB_ULP = "optical_sub_ulp"
LIT_OPTICAL_CSG_SUB_ULP = "optical_csg_sub_ulp"
LIT_OPTICAL_GROUP_GAP = "optical_group_gap"
LIT_OPTICAL_CSG_GROUP_GAP = "optical_csg_group_gap"
LIT_OPTICAL_GROUP_GAP_SUB_ULP = "optical_group_gap_sub_ulp"
LIT_OPTICAL_CSG_GROUP_GAP_SUB_ULP = "optical_csg_group_gap_sub_ulp"
LIT_OPTICS = "optics"
LIT_QUEUE_COVERAGE = "queue_coverage"
LIT_OPTICAL_QUERY_LIMIT = "optical_query_limit"
LIT_OPTICAL_TIR_LIMIT = "optical_tir_limit"
LIT_OPTICAL_TIR = "optical_tir"
LIT_SEQUENCE = "sequence"
LIT_GENERATION = "generation"
LIT_MAX_QUERIES = "max_queries"
LIT_QUERY_BUDGET_UNITS = "query_budget_units"
LIT_PHYSICAL_QUERIES = "physical_queries"
LIT_BOOTSTRAP_EVENTS = "bootstrap_events"
LIT_TRANSPARENT_PATHS = "transparent_paths"
LIT_UNSUPPORTED_PATHS = "unsupported_paths"
LIT_LIMITED_PATHS = "limited_paths"
LIT_AMBIGUOUS_PATHS = "ambiguous_paths"
LIT_TIR_EVENTS = "tir_events"
LIT_MEDIUM_OVERFLOW_PATHS = "medium_overflow_paths"
LIT_TRANSPORT_ENABLED = "transport_enabled"
LIT_REFLECTIONSMOKEOPTICS = "ReflectionSmokeOptics:"
LIT_HARDWARE = "hardware"
LIT_OPTICAL_REFERENCE = "optical_reference"
LIT_STATISTICS = "statistics"
AMBIGUOUS_VOLUME_CASES = ("optical_sliver", "optical_csg_sliver", "optical_sub_ulp", "optical_csg_sub_ulp", "optical_group_gap", "optical_csg_group_gap", "optical_group_gap_sub_ulp", "optical_csg_group_gap_sub_ulp",
    LIT_OPTICAL_GROUP_ENTRY, LIT_OPTICAL_CSG_GROUP_ENTRY)
CSG_TRACE_SLICE_RAYS = 8192
SLICE_PACKET_FIELDS = ("source_frame", "plan_generation", "device_generation", "graphics_queue", "runtime_present", "ray_capacity", "expected_slices", "hardware_nodes", "indexed_slices", "compiled_nodes", "unique_packets", "accepted_packets",
    LIT_SINGLE_TASK_PACKETS, LIT_GRAPHICS_PACKETS)
OPTICS_FIELDS = (LIT_SEQUENCE, LIT_GENERATION, LIT_MAX_QUERIES, LIT_QUERY_BUDGET_UNITS, LIT_PHYSICAL_QUERIES, LIT_BOOTSTRAP_EVENTS, LIT_TRANSPARENT_PATHS,
    LIT_UNSUPPORTED_PATHS, LIT_LIMITED_PATHS, LIT_AMBIGUOUS_PATHS, LIT_TIR_EVENTS, LIT_MEDIUM_OVERFLOW_PATHS, LIT_TRANSPORT_ENABLED)


class ReflectionOpticalEvidence:
    @staticmethod
    def parse_optics(log_text):
        samples = []
        for line in log_text.splitlines():
            if LIT_REFLECTIONSMOKEOPTICS not in line:
                continue
            pairs = re.findall(r"([a-z_]+)=(\d+)", line.split(LIT_REFLECTIONSMOKEOPTICS, 1)[1])
            if len(pairs) != len(OPTICS_FIELDS) or {name for name, _ in pairs} != set(OPTICS_FIELDS):
                raise SmokeFailure("malformed completed optical statistics")
            samples.append({name: int(value) for name, value in pairs})
        if not samples:
            raise SmokeFailure("no completed optical statistics")
        return samples

    @staticmethod
    def validate_queue_coverage(statistics, spec):
        # Optical query limits apply after hardware admission on mixed opaque and glass queues.
        if spec.case not in ("optical_inside", "optical_inside_nested", LIT_OPTICAL_TIR, LIT_OPTICAL_CSG_CAP, LIT_OPTICAL_CSG_OPEN_TAIL, LIT_OPTICAL_UNSPECIFIED_REFERENCE, LIT_OPTICAL_CSG_UNSPECIFIED_CAP, *DENSE_CASES):
            return None
        completed = []
        for sample in statistics:
            if sample[LIT_FRAME] < 3:
                continue
            admitted = min(sample[LIT_CANDIDATES], sample[LIT_EFFECTIVE_BUDGET])
            if sample[LIT_HARDWARE_RAYS] != admitted:
                raise SmokeFailure("CSG reflection must execute every admitted queue entry exactly once in each stable frame")
            completed.append({name: sample[name] for name in (LIT_SEQUENCE, LIT_GENERATION, LIT_FRAME, LIT_CANDIDATES, LIT_EFFECTIVE_BUDGET, LIT_HARDWARE_RAYS)})
        if not completed:
            raise SmokeFailure("CSG queue coverage lacks a stable accepted frame")
        return {"requested_ray_budget": spec.ray_budget, LIT_COMPLETED_FRAMES: completed, LIT_CONTRACT: "hardware_rays equals min(candidates, effective_budget) in every stable accepted snapshot"}

    @staticmethod
    def validate_slice_packets(log_text, statistics, spec):
        if not spec.case.startswith("optical_csg_") or spec.case == LIT_OPTICAL_CSG_REFERENCE:
            return None
        from reflection_history_evidence import ReflectionHistoryEvidence
        history = ReflectionHistoryEvidence.parse_history(log_text)
        by_key = {}
        for sample in history:
            key = (sample[LIT_SEQUENCE], sample[LIT_GENERATION])
            if key in by_key:
                raise SmokeFailure("duplicate completed source-frame identity for CSG slice packets")
            by_key[key] = sample
        observations = {}
        for line in log_text.splitlines():
            if LIT_REFLECTIONSMOKESLICEPACKETS not in line:
                continue
            record = {}
            for field in line.split(LIT_REFLECTIONSMOKESLICEPACKETS, 1)[1].strip().split():
                match = re.fullmatch(r"([a-z_]+)=([0-9]+)", field)
                if not match or match[1] in record:
                    raise SmokeFailure("malformed accepted CSG slice-packet observation")
                record[match[1]] = int(match[2])
            if set(record) != set(SLICE_PACKET_FIELDS):
                raise SmokeFailure("accepted CSG slice-packet observation lacks exact fields")
            if record[LIT_SOURCE_FRAME] in observations:
                raise SmokeFailure("duplicate accepted CSG slice-packet source frame")
            observations[record[LIT_SOURCE_FRAME]] = record
        completed = []
        for sample in statistics:
            if sample[LIT_FRAME] < 3:
                continue
            source = by_key.get((sample[LIT_SEQUENCE], sample[LIT_GENERATION]))
            record = observations.get(source["graphics_frame"]) if source is not None else None
            if record is None:
                raise SmokeFailure("completed CSG snapshot lacks accepted slice-packet evidence for its exact source frame")
            capacity = min(sample[LIT_EFFECTIVE_BUDGET], sample["queue_capacity"])
            expected = (capacity + CSG_TRACE_SLICE_RAYS - 1) // CSG_TRACE_SLICE_RAYS
            if (record["runtime_present"] != 1 or record["plan_generation"] == 0 or record[LIT_DEVICE_GENERATION] != sample[LIT_DEVICE_GENERATION] or record[LIT_DEVICE_GENERATION] == 0 or record["graphics_queue"] >= 65535):
                raise SmokeFailure("CSG slice packets lack current accepted plan and primary Graphics queue identity")
            if record["ray_capacity"] != capacity or record["expected_slices"] != expected:
                raise SmokeFailure("CSG slice-packet count describes a different frozen queue capacity")
            counts = ("hardware_nodes", "indexed_slices", "compiled_nodes", "unique_packets", "accepted_packets", LIT_SINGLE_TASK_PACKETS, LIT_GRAPHICS_PACKETS)
            if any(record[name] != expected for name in counts):
                raise SmokeFailure("every CSG hardware slice must accept as a distinct single-task primary Graphics packet")
            completed.append({LIT_SEQUENCE: sample[LIT_SEQUENCE], LIT_GENERATION: sample[LIT_GENERATION], **record})
        if not completed:
            raise SmokeFailure("CSG slice packets lack a stable completed frame")
        return {"slice_ray_limit": CSG_TRACE_SLICE_RAYS, LIT_COMPLETED_FRAMES: completed, LIT_CONTRACT: "Each frozen CSG hardware slice has a distinct accepted single-task primary Graphics packet; zero budget has none. "
                "One complete hardware timing range still spans all slices per frame."}

    @staticmethod
    def validate_optics(log_text, spec):
        statistics = ReflectionStatistics.parse_statistics(log_text)
        ReflectionStatistics.validate_statistics(statistics, spec.case, LIT_HARDWARE, budget=spec.ray_budget)
        queue_coverage = ReflectionOpticalEvidence.validate_queue_coverage(statistics, spec)
        slice_packets = ReflectionOpticalEvidence.validate_slice_packets(log_text, statistics, spec)
        by_key = {(sample[LIT_SEQUENCE], sample[LIT_GENERATION]): sample for sample in statistics}
        samples = ReflectionOpticalEvidence.parse_optics(log_text)
        stable = []
        for optical in samples:
            sample = by_key.get((optical[LIT_SEQUENCE], optical[LIT_GENERATION]))
            if sample is None:
                raise SmokeFailure("optical counters lack matching accepted-token source metadata")
            if optical[LIT_MAX_QUERIES] != spec.queries:
                raise SmokeFailure("optical counters describe a different frozen query budget")
            rays, queries = sample[LIT_HARDWARE_RAYS], optical[LIT_QUERY_BUDGET_UNITS]
            if not rays <= queries <= rays * spec.queries:
                raise SmokeFailure("reserved traversal units violate the per-path query bound")
            physical_queries = optical[LIT_PHYSICAL_QUERIES]
            if physical_queries > queries:
                raise SmokeFailure("physical hardware queries exceed reserved traversal units")
            for field in (LIT_TRANSPARENT_PATHS, LIT_UNSUPPORTED_PATHS, LIT_LIMITED_PATHS, LIT_AMBIGUOUS_PATHS, LIT_MEDIUM_OVERFLOW_PATHS):
                if optical[field] > rays:
                    raise SmokeFailure("optical path counter exceeds admitted primary paths: " + field)
            if optical[LIT_TIR_EVENTS] > queries or optical[LIT_BOOTSTRAP_EVENTS] > rays * 32:
                raise SmokeFailure("optical event counters exceed their bounded traversal capacity")
            if sample[LIT_FRAME] >= 3:
                if spec.case in DENSE_CASES:
                    expected_queries = rays * (2 if spec.case == LIT_OPTICAL_CSG_DENSE else 1)
                    if (not rays or rays != min(sample[LIT_CANDIDATES], sample[LIT_EFFECTIVE_BUDGET]) or sample[LIT_HARDWARE_HITS] != rays or sample[LIT_OPAQUE_PIXELS] != rays or sample[LIT_GLASS_PIXELS] or sample[LIT_FALLBACK_PIXELS]
                        or queries != expected_queries or physical_queries != expected_queries):
                        raise SmokeFailure("dense opaque receiver lost an admitted surface hit or replayed its hardware query")
                    if optical[LIT_TRANSPORT_ENABLED] or any(optical[field] for field in (LIT_TRANSPARENT_PATHS, LIT_BOOTSTRAP_EVENTS, LIT_UNSUPPORTED_PATHS, LIT_LIMITED_PATHS, LIT_AMBIGUOUS_PATHS, LIT_TIR_EVENTS, LIT_MEDIUM_OVERFLOW_PATHS)):
                        raise SmokeFailure("dense opaque receiver used optical transport or reported an unresolved path")
                if spec.case in AMBIGUOUS_VOLUME_CASES and optical[LIT_AMBIGUOUS_PATHS] != rays:
                    raise SmokeFailure("negative optical fixture did not report ambiguity for every admitted hardware ray")
                stable.append((sample, optical))
        if not stable:
            raise SmokeFailure("no stable completed optical frame")
        sample, optical = stable[-1]
        if optical[LIT_TRANSPORT_ENABLED] != int(spec.case not in (LIT_OPTICAL_REFERENCE, *DENSE_CASES) and sample["hardware_ready"]):
            raise SmokeFailure("completed snapshot used the wrong plain/optical hardware kernel")
        if spec.ray_budget == DEFAULT_RAY_BUDGET and spec.case not in (LIT_OPTICAL_REFERENCE, *DENSE_CASES) and spec.case not in AMBIGUOUS_VOLUME_CASES and optical[LIT_TRANSPARENT_PATHS] == 0:
            raise SmokeFailure("authored optical scene produced no transparent reflected paths")
        required = {"optical_unspecified": LIT_UNSUPPORTED_PATHS, "optical_mixed": LIT_AMBIGUOUS_PATHS, "optical_overflow": LIT_MEDIUM_OVERFLOW_PATHS, LIT_OPTICAL_QUERY_LIMIT: LIT_LIMITED_PATHS,
            LIT_OPTICAL_TIR_LIMIT: LIT_LIMITED_PATHS, "optical_coincident_independent": LIT_AMBIGUOUS_PATHS, LIT_OPTICAL_SLIVER: LIT_AMBIGUOUS_PATHS, LIT_OPTICAL_CSG_SLIVER: LIT_AMBIGUOUS_PATHS,
            LIT_OPTICAL_SUB_ULP: LIT_AMBIGUOUS_PATHS, LIT_OPTICAL_CSG_SUB_ULP: LIT_AMBIGUOUS_PATHS, LIT_OPTICAL_GROUP_GAP: LIT_AMBIGUOUS_PATHS, LIT_OPTICAL_CSG_GROUP_GAP: LIT_AMBIGUOUS_PATHS,
            LIT_OPTICAL_GROUP_GAP_SUB_ULP: LIT_AMBIGUOUS_PATHS, LIT_OPTICAL_CSG_GROUP_GAP_SUB_ULP: LIT_AMBIGUOUS_PATHS, LIT_OPTICAL_GROUP_ENTRY: LIT_AMBIGUOUS_PATHS, LIT_OPTICAL_CSG_GROUP_ENTRY: LIT_AMBIGUOUS_PATHS}.get(spec.name)
        if required and optical[required] == 0:
            raise SmokeFailure("negative optical fixture did not exercise " + required)
        if spec.case.startswith("optical_inside") and optical[LIT_BOOTSTRAP_EVENTS] == 0:
            raise SmokeFailure("inside-origin fixture did not exercise membership bootstrap")
        if spec.case == LIT_OPTICAL_TIR and optical[LIT_TIR_EVENTS] == 0:
            raise SmokeFailure("prism fixture did not exercise total internal reflection")
        return {LIT_STATISTICS: statistics, LIT_OPTICS: samples, "stable_optics": optical, LIT_QUEUE_COVERAGE: queue_coverage, "slice_packets": slice_packets}

    @staticmethod
    def validate_collection_reuse(evidence, spec):
        if spec.case == LIT_OPTICAL_CSG_OPEN_TAIL:
            expected_units, expected_physical, limited = 12, 12, False
        elif spec.case == LIT_OPTICAL_CSG_CAP and spec.queries == 8:
            expected_units, expected_physical, limited = 8, 5, True
        elif spec.case in (LIT_OPTICAL_CSG_CAP, LIT_OPTICAL_CSG_UNSPECIFIED_CAP) and spec.queries == 16:
            expected_units, expected_physical, limited = 11, 7, False
        elif spec.case == LIT_OPTICAL_UNSPECIFIED_REFERENCE and spec.queries == 16:
            expected_units, expected_physical, limited = 5, 5, False
        else:
            raise SmokeFailure("collection reuse evidence requires its declared closed, open or query-limit control")
        by_key = {(sample[LIT_SEQUENCE], sample[LIT_GENERATION]): sample for sample in evidence[LIT_OPTICS]}
        completed = []
        for sample in evidence[LIT_STATISTICS]:
            if sample[LIT_FRAME] < 3:
                continue
            optical = by_key.get((sample[LIT_SEQUENCE], sample[LIT_GENERATION]))
            rays = sample[LIT_HARDWARE_RAYS]
            if (optical is None or not rays or rays != min(sample[LIT_CANDIDATES], sample[LIT_EFFECTIVE_BUDGET]) or sample[LIT_HARDWARE_HITS] != rays or sample[LIT_OPAQUE_PIXELS] != rays or sample[LIT_GLASS_PIXELS] or sample[LIT_FALLBACK_PIXELS]):
                raise SmokeFailure("collection reuse control lost an admitted retained-slab path")
            if (optical[LIT_MAX_QUERIES] != spec.queries or optical[LIT_TRANSPORT_ENABLED] != 1 or optical[LIT_QUERY_BUDGET_UNITS] != expected_units * rays or optical[LIT_PHYSICAL_QUERIES] != expected_physical * rays or optical[LIT_TRANSPARENT_PATHS] != rays
                or optical[LIT_LIMITED_PATHS] != (rays if limited else 0) or any(optical[field] for field in (LIT_BOOTSTRAP_EVENTS, LIT_UNSUPPORTED_PATHS, LIT_AMBIGUOUS_PATHS, LIT_TIR_EVENTS, LIT_MEDIUM_OVERFLOW_PATHS))):
                raise SmokeFailure("collection reuse control changed reserved work, physical queries or conservative termination")
            completed.append({LIT_SEQUENCE: sample[LIT_SEQUENCE], LIT_GENERATION: sample[LIT_GENERATION], LIT_FRAME: sample[LIT_FRAME], LIT_HARDWARE_RAYS: rays, LIT_QUERY_BUDGET_UNITS: optical[LIT_QUERY_BUDGET_UNITS],
                LIT_PHYSICAL_QUERIES: optical[LIT_PHYSICAL_QUERIES]})
        if not completed:
            raise SmokeFailure("collection reuse control lacks a stable completed source frame")
        return {"reserved_units_per_ray": expected_units, "physical_queries_per_ray": expected_physical, "query_limit_termination": limited, LIT_COMPLETED_FRAMES: completed,
            LIT_CONTRACT: "Closed receivers reuse exact tie certificates; a positive open event beyond the nearest chart disables reuse. "
                "The extended open root also requires one terminal no-hit collection. Query exhaustion preserves reserved work and black residual radiance. "
                "Valid-air controls author the sole transparent receiver as Unspecified, selecting the normal all-Unspecified optical kernel."}

