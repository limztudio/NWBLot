"""Validation of the authored stress workload and its startup evidence."""

import math
import re

from window_capture_outcome import SmokeFailure


LIT_TOKEN_FIELD_PATTERN = r"=(\S+)"
LIT_CHARACTERS_PER_CLASS = "characters_per_class"
LIT_TOTAL = "total"
LIT_TRANSPARENT = "transparent"
LIT_OPAQUE = "opaque"
LIT_LAYOUT = "layout"
LIT_ROWS = "rows"
LIT_COLUMNS = "columns"
LIT_FRONT_Z = "front_z"
LIT_BACK_Z = "back_z"
LIT_OBSERVED = "observed"
LIT_SIGNATURE = "signature"
WORKLOAD = "StressTestSmokeProject: workload "
SPAWN = "StressTestSmokeProject: spawned "
WORKLOAD_FIELDS = (LIT_CHARACTERS_PER_CLASS, LIT_TOTAL, LIT_TRANSPARENT, LIT_OPAQUE, LIT_LAYOUT, LIT_ROWS, LIT_COLUMNS, "row_spacing_x", "row_stagger_x", LIT_FRONT_Z, LIT_BACK_Z, "body_scale", "camera_x", "camera_y", "camera_z",
    "camera_pitch", "vertical_fov", "near_plane", "far_plane", "aspect")
WORKLOAD_INTEGERS = (LIT_CHARACTERS_PER_CLASS, LIT_TOTAL, LIT_TRANSPARENT, LIT_OPAQUE, LIT_ROWS, LIT_COLUMNS)


class StressWorkload:
    def __init__(self, characters_per_class, start_marker, interval_marker):
        self._characters_per_class = characters_per_class
        self._start_marker = start_marker
        self._interval_marker = interval_marker

    def parse(self, lines):
        characters_per_class = self._characters_per_class
        if characters_per_class not in (5, 10):
            raise SmokeFailure("characters per class must select the five or ten character profile")
        records = [line for line in lines if line.startswith(WORKLOAD)]
        spawns = [line for line in lines if line.startswith(SPAWN)]
        if len(records) != 1 or len(spawns) != 1:
            raise SmokeFailure("exactly one workload signature and spawned-character record are required")
        pattern = re.escape(WORKLOAD) + " ".join(re.escape(field) + LIT_TOKEN_FIELD_PATTERN for field in WORKLOAD_FIELDS)
        match = re.fullmatch(pattern, records[0])
        if not match:
            raise SmokeFailure("malformed stress workload signature")
        row = dict(zip(WORKLOAD_FIELDS, match.groups()))
        try:
            for field in WORKLOAD_INTEGERS:
                if not re.fullmatch(r"[0-9]{1,2}", row[field]):
                    raise ValueError("invalid integer")
                row[field] = int(row[field])
            for field in WORKLOAD_FIELDS:
                if field not in WORKLOAD_INTEGERS and field != LIT_LAYOUT:
                    row[field] = float(row[field])
                    if not math.isfinite(row[field]):
                        raise ValueError("nonfinite workload value")
        except ValueError as error:
            raise SmokeFailure("invalid stress workload count or camera/layout number") from error
        comparison = characters_per_class == 5
        expected = dict(characters_per_class=characters_per_class, total=characters_per_class * 2, transparent=characters_per_class, opaque=characters_per_class, layout="zigzag_v1" if comparison else "two_rows_v1", rows=2, columns=characters_per_class,
            row_spacing_x=1.44 if comparison else .72, row_stagger_x=.36 if comparison else .18, front_z=-.55, back_z=.55, body_scale=1., camera_x=0., camera_y=1.8 if comparison else 2.7,
            camera_z=-4.8 if comparison else -7.2, camera_pitch=.2 if comparison else .25, vertical_fov=math.pi / 3., near_plane=.001, far_plane=10000., aspect=0.)
        for field, value in expected.items():
            matches = row[field] == value if field in WORKLOAD_INTEGERS or field == LIT_LAYOUT else math.isclose(row[field], value, rel_tol=1e-6, abs_tol=1e-6)
            if not matches:
                raise SmokeFailure(f"requested stress workload profile disagrees with observed {field}")
        expected_spawn = (SPAWN + f"{row[LIT_TOTAL]} spinning characters ({row[LIT_TRANSPARENT]} transparent + " f"{row[LIT_OPAQUE]} opaque) over ground, directional + point light")
        if spawns[0] != expected_spawn:
            raise SmokeFailure("spawned characters disagree with the requested stress workload")
        first_interval = next((index for index, line in enumerate(lines) if line.startswith(self._interval_marker)), len(lines))
        if not lines.index(self._start_marker) < lines.index(spawns[0]) < lines.index(records[0]) < first_interval:
            raise SmokeFailure("stress workload must be observed after startup and before measurement intervals")
        return {"requested_characters_per_class": characters_per_class, LIT_OBSERVED: row, LIT_SIGNATURE: records[0]}
