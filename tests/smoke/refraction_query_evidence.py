"""Completed refraction query diagnostics and conservation evidence."""

import re

from refraction_density_image_evidence import RefractionDensityImageEvidence
from window_capture_smoke import SmokeFailure


COUNTER_FIELDS = (
    "hardwarePixels", "entryQueries", "entryAssociationMisses", "entryWrongEntities", "entryFailures", "associatedEntries",
    "invalidEntryDirections", "exitQueries", "exitMisses", "exitFailures", "tirEvents", "tirCappedPixels", "volumeExits",
    "backgroundQueries", "backgroundMisses", "backgroundFailures", "secondaryGlassPixels", "transparentHits", "backgroundLayerCapPixels",
    "cachedOpaqueReturns", "shadedOpaqueReturns", "exitScreenBackgroundReturns", "exitStraightFallbackPixels", "materialShadeCalls",
    "cachedHitAttempts", "cachedHitMisses", "physicalQueries", "queryBudgetUnits", "overflowFlags",
)
METADATA_FIELDS = (
    "sequence", "generation", "graphics_frame", "observed_frame", "target_start_frame", "completed_target_frames", "frame_index",
    "width", "height", "hardware_requested", "hardware_available", "hardware_ready", "csg_enabled", "token_valid", "physical_valid",
    "token_queue", "token_value", "physical_queue", "device_generation",
)
HEADER = re.compile(r"^RefractionQueryDiagnostics: (sequence=.*)$", re.MULTILINE)
COUNTER = re.compile(r"^RefractionQueryCounter: sequence=(\d+) generation=(\d+) name=([A-Za-z]+) value=(\d+)$", re.MULTILINE)
COMPLETE = re.compile(r"^RefractionQueryDiagnostics: complete sequence=(\d+) generation=(\d+) counters=(\d+)$", re.MULTILINE)

class RefractionQueryEvidence:
    @staticmethod
    def parse_completed_statistics(text):
        text = text.replace("\r\n", "\n")
        headers, completions, records = list(HEADER.finditer(text)), list(COMPLETE.finditer(text)), list(COUNTER.finditer(text))
        if len(headers) != 1 or len(completions) != 1:
            raise SmokeFailure("query diagnostics require one completed statistics block")
        header, completion = headers[0], completions[0]
        pairs = [part.split("=", 1) for part in header.group(1).split()]
        if any(len(pair) != 2 or not pair[1].isdecimal() for pair in pairs):
            raise SmokeFailure("malformed query statistics metadata")
        metadata = {name: int(value) for name, value in pairs}
        if len(metadata) != len(pairs) or set(metadata) != set(METADATA_FIELDS):
            raise SmokeFailure("query statistics metadata is missing, duplicated, or unknown")
        key = metadata["sequence"], metadata["generation"]
        if tuple(map(int, completion.groups()[:2])) != key or int(completion.group(3)) != len(COUNTER_FIELDS):
            raise SmokeFailure("query statistics completion identity or counter count differs")
        counters = {}
        for record in records:
            sequence, generation, name, value = record.groups()
            if (int(sequence), int(generation)) != key or not header.end() < record.start() < completion.start():
                raise SmokeFailure("query counter does not belong to the completed statistics block")
            if name in counters or name not in COUNTER_FIELDS:
                raise SmokeFailure("query counter is duplicated or unknown")
            counters[name] = int(value)
        if set(counters) != set(COUNTER_FIELDS):
            raise SmokeFailure("query statistics counters are incomplete")
        ready = text.find("FramebufferCapture: capture ready")
        if ready < completion.end():
            raise SmokeFailure("query statistics did not complete before framebuffer capture")
        captures = re.findall(r"^FramebufferCapture: graphics source frame (\d+)$", text, re.MULTILINE)
        if len(captures) != 1 or int(captures[0]) <= metadata["graphics_frame"]:
            raise SmokeFailure("diagnostic framebuffer source must follow the completed statistics source")
        if any(line.startswith(("RefractionQueryCounter:", "RefractionQueryDiagnostics:")) and not (
                HEADER.fullmatch(line) or COUNTER.fullmatch(line) or COMPLETE.fullmatch(line)) for line in text.splitlines()):
            raise SmokeFailure("malformed query statistics record")
        return {"metadata": metadata, "counters": counters, "capture_graphics_frame": int(captures[0])}

    @staticmethod
    def validate_statistics(statistics, cell):
        metadata, counters = statistics["metadata"], statistics["counters"]
        if set(metadata) != set(METADATA_FIELDS) or set(counters) != set(COUNTER_FIELDS):
            raise SmokeFailure("query statistics schema differs")
        if any(type(value) is not int or value < 0 or value > (1 << 64) - 1 for value in metadata.values()):
            raise SmokeFailure("query statistics metadata is outside unsigned bounds")
        if any(type(value) is not int or value < 0 or value > (1 << 32) - 1 for value in counters.values()):
            raise SmokeFailure("query counters are outside unsigned bounds")
        if (metadata["width"], metadata["height"]) != (cell.width, cell.height):
            raise SmokeFailure("completed query statistics extent differs from the selected target")
        if any(metadata[field] != 1 for field in ("hardware_requested", "hardware_available", "hardware_ready", "token_valid", "physical_valid")):
            raise SmokeFailure("query statistics lack an accepted ready hardware route and valid physical token")
        if metadata["csg_enabled"] != 0:
            raise SmokeFailure("ordinary density query profiling cannot claim CSG physical work")
        if min(metadata[field] for field in ("sequence", "generation", "token_value", "device_generation")) == 0 or metadata["physical_queue"] >= (1 << 32) - 1:
            raise SmokeFailure("query statistics token or publication identity is invalid")
        source, start = metadata["graphics_frame"], metadata["target_start_frame"]
        if source < start or source > metadata["observed_frame"] or source - start != metadata["completed_target_frames"] or source - start < 16:
            raise SmokeFailure("query statistics source is stale or lacks sixteen target graphics frames")
        if counters["overflowFlags"]:
            raise SmokeFailure("query diagnostic counter overflow invalidates the frame")
        left, top, right, bottom = RefractionDensityImageEvidence.projected_bounds(cell.width, cell.height, cell.density)
        expected_pixels = (right - left) * (bottom - top)
        if counters["hardwarePixels"] != expected_pixels:
            raise SmokeFailure("hardware pixel count differs from the independent projected rectangle")

        def equal(label, actual, expected):
            if actual != expected:
                raise SmokeFailure(f"query conservation failed for {label}: observed={actual}, expected={expected}")

        c = counters
        pixels = c["hardwarePixels"]
        equal("entry queries", c["entryQueries"], 0)
        if c["entryAssociationMisses"] + c["entryWrongEntities"] + c["entryFailures"] > c["entryQueries"]:
            raise SmokeFailure("entry query rejections exceed entry queries")
        equal("entry association", pixels, c["entryAssociationMisses"] + c["entryWrongEntities"] + c["entryFailures"] + c["associatedEntries"])
        exit_starts = c["associatedEntries"] - c["invalidEntryDirections"]
        if exit_starts < 0 or not exit_starts <= c["exitQueries"] <= 3 * exit_starts or c["tirCappedPixels"] > exit_starts:
            raise SmokeFailure("exit query or TIR counts exceed the bounded primary loop")
        equal("exit queries and TIR", c["exitQueries"], exit_starts + c["tirEvents"] - c["tirCappedPixels"])
        equal("volume exits", exit_starts, c["exitMisses"] + c["exitFailures"] + c["tirCappedPixels"] + c["volumeExits"])
        if c["tirEvents"] < 3 * c["tirCappedPixels"] or c["tirEvents"] > c["exitQueries"]:
            raise SmokeFailure("TIR events do not support the capped primary paths")
        volumes = c["volumeExits"]
        if not volumes <= c["backgroundQueries"] <= 3 * volumes or c["backgroundLayerCapPixels"] > volumes or c["transparentHits"] > c["backgroundQueries"]:
            raise SmokeFailure("background counts exceed the bounded continuation loop")
        equal("background query continuation", c["backgroundQueries"], volumes + c["transparentHits"] - c["backgroundLayerCapPixels"])
        if c["transparentHits"] < 3 * c["backgroundLayerCapPixels"]:
            raise SmokeFailure("background layer caps lack three transparent hits")
        approximation = c["backgroundMisses"] + c["secondaryGlassPixels"] + c["backgroundLayerCapPixels"]
        equal("background terminal paths", volumes, c["backgroundFailures"] + c["cachedOpaqueReturns"] + c["shadedOpaqueReturns"] + approximation)
        equal("background approximation", approximation, c["exitScreenBackgroundReturns"] + c["exitStraightFallbackPixels"])
        equal("cache attempts", c["cachedHitAttempts"], c["cachedOpaqueReturns"] + c["shadedOpaqueReturns"])
        equal("cache misses", c["cachedHitMisses"], c["shadedOpaqueReturns"])
        equal("material shading", c["materialShadeCalls"], c["transparentHits"] + c["shadedOpaqueReturns"])
        equal("ordinary failures", c["entryFailures"] + c["exitFailures"] + c["backgroundFailures"], 0)
        logical = c["entryQueries"] + c["exitQueries"] + c["backgroundQueries"]
        equal("ordinary physical queries", c["physicalQueries"], logical)
        equal("ordinary query budget", c["queryBudgetUnits"], logical)
        if not pixels and any(c.values()):
            raise SmokeFailure("empty density must publish zero completed query work")
        return {"independent_hardware_pixels": expected_pixels, "logical_queries": logical,
            "queries_per_hardware_pixel": logical / pixels if pixels else None,
            "entry_query_fraction": c["entryQueries"] / logical if logical else None,
            "exit_query_fraction": c["exitQueries"] / logical if logical else None,
            "background_query_fraction": c["backgroundQueries"] / logical if logical else None,
            "conservation_passed": True, "physical_query_contract": "one inline operation per ordinary logical stage call"}
