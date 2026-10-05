"""Synthetic JSONL identity/output contracts; no renderer, compiler or GPU execution."""

import contextlib
import hashlib
import io
import json
from pathlib import Path
import sys
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))
import compiler_statistics_diagnostic as diagnostic

LIT_TYPE = "type"
LIT_SCHEMA = "schema"
LIT_CAPACITY = "capacity"
LIT_SEQUENCE = "sequence"
LIT_SOURCE_FRAME = "source_frame"
LIT_STATUS = "status"
LIT_IDENTITY = "identity"
LIT_GRAPH_GENERATION = "graph_generation"
LIT_DEVICE_GENERATION = "device_generation"
LIT_RECORDING_ATTEMPT_GENERATION = "recording_attempt_generation"
LIT_SECONDS = "seconds"
LIT_COUNTS = "counts"
LIT_SUBMISSION = "submission"
LIT_ACCEPTED_TASKS = "accepted_tasks"
LIT_REJECTED_PACKETS = "rejected_packets"
LIT_ATTEMPTS = "attempts"
LIT_STORED = "stored"
LIT_OVERFLOW = "overflow"
LIT_INVALID = "invalid"
LIT_COMPLETE = "complete"
LIT_UNIQUE = "unique"
LIT_TIMING = "timing"
LIT_PUBLISH_FRAME = "publish_frame"
LIT_SCOPE = "scope"
LIT_SAMPLES = "samples"
LIT_N = "\n"
LIT_UTF_8 = "utf-8"
LIT_COMPILER_STATISTICS = "--compiler-statistics"
LIT_WORKLOAD = "--workload"
LIT_JOINED_FRAMES = "joined_frames"
LIT_JOINED = "joined"
LIT_RAW_COMPILER_ROWS = "raw_compiler_rows"
LIT_PHASE_SECONDS = "phase_seconds"
LIT_FIELDS = "fields"
LIT_INVALID_SNAPSHOT = "invalid_snapshot"
LIT_INVALID_DURATION = "invalid_duration"
LIT_INCOMPLETE = "incomplete"
LIT_COMPILER_JSONL = "compiler.jsonl"
LIT_GATHER_JSONL = "gather.jsonl"
LIT_REPORT_JSON = "report.json"
LIT_INPUTS = "inputs"
LIT_SHA256 = "sha256"
LIT_NWB_GATHER_COMPILER_STATISTICS_FILE = "NWB_GATHER_COMPILER_STATISTICS_FILE"
LIT_INHERITED_JSONL = "inherited.jsonl"
LIT_SAMPLE_JSONL = "sample.jsonl"
LIT_TRIAL_COMPILER_STATISTICS_JSONL = "trial/compiler_statistics.jsonl"
LIT_MAIN = "__main__"

gather = diagnostic.gather


def compiler_rows(count=384, offset=0):
    values = [{LIT_TYPE: "compiler_statistics_configuration", LIT_SCHEMA: 1, LIT_CAPACITY: 1024}]
    for sequence in range(count):
        values.append({LIT_TYPE: "compiler_statistics", LIT_SEQUENCE: sequence, LIT_SOURCE_FRAME: offset + sequence,
            LIT_STATUS: "valid", LIT_IDENTITY: {LIT_GRAPH_GENERATION: 7, "plan_generation": sequence + 1,
                LIT_DEVICE_GENERATION: 2, LIT_RECORDING_ATTEMPT_GENERATION: sequence + 100},
            LIT_SECONDS: {field: (index + 1) * .001 for index, field in enumerate(diagnostic.SECONDS_FIELDS)},
            LIT_COUNTS: {field: index + 1 for index, field in enumerate(diagnostic.COUNT_FIELDS)},
            LIT_SUBMISSION: {"accepted_packets": 2, LIT_ACCEPTED_TASKS: 12, LIT_REJECTED_PACKETS: 0, "rejected_tasks": 0}})
    values.append({LIT_TYPE: "compiler_statistics_complete", LIT_ATTEMPTS: count, LIT_STORED: count,
                   LIT_OVERFLOW: 0, LIT_INVALID: 0, LIT_COMPLETE: count > 0})
    return values


def gather_rows(offset=0):
    values = [dict(gather.FROZEN_SETTINGS, type="configuration", schema=1, workload=LIT_UNIQUE, mode=LIT_TIMING,
        successful_frames=384, renderers=65, runtime_renderers=0, runtime_owners=0, transparent_renderers=64)]
    for source in range(offset + 96, offset + 352):
        values.append({LIT_TYPE: "cpu", LIT_SOURCE_FRAME: source, LIT_PUBLISH_FRAME: source + 1, "scopes_ms": {
            "graphics.frame": 8., "graphics.frame_preamble": .05, "graphics.prepare_resources": 2.,
            "graphics.render_passes": 2., "graphics.render": 5., "frame.project_update": .1,
            "graphics.present": .2, "graphics.begin_frame": .4}})
    for scope in gather.GPU_BASE + gather.GPU_TRANSPARENT:
        values.append({LIT_TYPE: "gpu", LIT_SCOPE: scope, "first_source_frame": offset + 96,
            "last_source_frame": offset + 351, LIT_PUBLISH_FRAME: offset + 353, "total_ms": 256., LIT_SAMPLES: 256})
    values.append({LIT_TYPE: LIT_COMPLETE})
    return values


def encode(rows):
    return (LIT_N.join(json.dumps(row, allow_nan=False) for row in rows) + LIT_N).encode(LIT_UTF_8)


def cli_arguments(compiler, result, output):
    return [LIT_COMPILER_STATISTICS, str(compiler), "--gather-result", str(result),
            LIT_WORKLOAD, LIT_UNIQUE, "--output", str(output)]


class CompilerStatisticsDiagnosticTests(unittest.TestCase):
    def test_joins_actual_source_ids_without_sorting_or_reindexing(self):
        values = compiler_rows(offset=5000)
        report = diagnostic.analyze_rows(values, gather_rows(offset=5000), LIT_UNIQUE)
        self.assertEqual(report[LIT_JOINED_FRAMES], 256)
        self.assertEqual(report["source_window"], [5096, 5351])
        self.assertEqual([row["compiler_sequence"] for row in report[LIT_JOINED]], list(range(96, 352)))
        self.assertEqual(report[LIT_JOINED][0]["gather_publish_frame"], 5097)
        self.assertEqual(report[LIT_RAW_COMPILER_ROWS], values)
        self.assertEqual(report[LIT_JOINED][0][LIT_IDENTITY], values[97][LIT_IDENTITY])
        self.assertEqual(len(report["unjoined_source_frames"]), 128)

    def test_configuration_requires_exact_fields_types_and_capacity(self):
        for key, bad in ((LIT_CAPACITY, 1023), (LIT_CAPACITY, True), (LIT_SCHEMA, True), (LIT_SCHEMA, 2), ("extra", 0)):
            values = compiler_rows()
            values[0][key] = bad
            with self.subTest(key=key, value=bad), self.assertRaises(diagnostic.SmokeFailure):
                diagnostic.validate_compiler_rows(values)

    def test_sequence_and_source_order_fail_before_any_sorting(self):
        for field, bad in ((LIT_SEQUENCE, 0), (LIT_SEQUENCE, 2), (LIT_SEQUENCE, True), (LIT_SOURCE_FRAME, 0)):
            values = compiler_rows()
            values[2][field] = bad
            with self.subTest(field=field, bad=bad), self.assertRaises(diagnostic.SmokeFailure):
                diagnostic.validate_compiler_rows(values)

    def test_each_identity_is_positive_exact_integer(self):
        for field in diagnostic.IDENTITY_FIELDS:
            for bad in (0, -1, True, 1., 1 << 64):
                values = compiler_rows()
                values[1][LIT_IDENTITY][field] = bad
                with self.subTest(field=field, bad=bad), self.assertRaisesRegex(diagnostic.SmokeFailure, LIT_IDENTITY):
                    diagnostic.validate_compiler_rows(values)

    def test_duplicate_plan_is_rejected_even_when_attempt_changes(self):
        values = compiler_rows()
        values[2][LIT_IDENTITY] = {**values[1][LIT_IDENTITY], LIT_RECORDING_ATTEMPT_GENERATION: 999}
        with self.assertRaisesRegex(diagnostic.SmokeFailure, "duplicate compiler plan"):
            diagnostic.validate_compiler_rows(values)

    def test_device_generation_respects_owning_native_width(self):
        values = compiler_rows()
        values[1][LIT_IDENTITY][LIT_DEVICE_GENERATION] = 65536
        with self.assertRaisesRegex(diagnostic.SmokeFailure, "native u16"):
            diagnostic.validate_compiler_rows(values)

    def test_plan_generations_are_scoped_by_graph_and_device(self):
        for field in (LIT_GRAPH_GENERATION, LIT_DEVICE_GENERATION):
            values = compiler_rows()
            values[2][LIT_IDENTITY] = {**values[1][LIT_IDENTITY], field: 999}
            self.assertEqual(len(diagnostic.validate_compiler_rows(values)), 384)

    def test_duration_and_count_schema_rejects_missing_extra_and_invalid_values(self):
        for bad in (-1., float("nan"), float("inf"), True, "0"):
            values = compiler_rows()
            values[1][LIT_SECONDS]["planning"] = bad
            with self.subTest(bad=bad), self.assertRaisesRegex(diagnostic.SmokeFailure, "duration"):
                diagnostic.validate_compiler_rows(values)
        for category, field in ((LIT_SECONDS, "analysis"), (LIT_COUNTS, "task"), (LIT_SUBMISSION, LIT_ACCEPTED_TASKS)):
            values = compiler_rows()
            del values[1][category][field]
            with self.assertRaisesRegex(diagnostic.SmokeFailure, LIT_FIELDS):
                diagnostic.validate_compiler_rows(values)
            values = compiler_rows()
            values[1][category]["unexpected"] = 1
            with self.assertRaisesRegex(diagnostic.SmokeFailure, LIT_FIELDS):
                diagnostic.validate_compiler_rows(values)
        for category, field in ((LIT_COUNTS, "resource"), (LIT_SUBMISSION, LIT_REJECTED_PACKETS)):
            for bad in (-1, True, 1., 1 << 64):
                values = compiler_rows()
                values[1][category][field] = bad
                with self.assertRaisesRegex(diagnostic.SmokeFailure, "count"):
                    diagnostic.validate_compiler_rows(values)

    def test_flagged_early_invalid_rows_are_retained_without_claiming_clean_capture(self):
        for status in (LIT_INVALID_SNAPSHOT, LIT_INVALID_DURATION):
            values = compiler_rows()
            values[1] = {key: values[1][key] for key in diagnostic.BASE_ROW_FIELDS}
            values[1][LIT_STATUS] = status
            values[-1].update(invalid=1, complete=True)
            with self.subTest(status=status):
                report = diagnostic.analyze_rows(values, gather_rows(), LIT_UNIQUE)
                self.assertEqual(report[LIT_STATUS], "joined_diagnostic_with_invalid_unmeasured")
                self.assertFalse(report["clean"])
                self.assertEqual(report["raw_invalid_count"], 1)
                self.assertEqual(report["invalid_unmeasured_rows"], [values[1]])
                self.assertEqual(report[LIT_RAW_COMPILER_ROWS], values)
                self.assertEqual(report[LIT_JOINED_FRAMES], 256)

    def test_same_invalid_flags_on_successful_measured_sources_fail(self):
        for status in (LIT_INVALID_SNAPSHOT, LIT_INVALID_DURATION):
            values = compiler_rows()
            values[101] = {key: values[101][key] for key in diagnostic.BASE_ROW_FIELDS}
            values[101][LIT_STATUS] = status
            values[-1].update(invalid=1, complete=True)
            with self.subTest(status=status), self.assertRaisesRegex(diagnostic.SmokeFailure, "invalid compiler snapshot.*100"):
                diagnostic.analyze_rows(values, gather_rows(), LIT_UNIQUE)

    def test_structural_identity_order_status_fails_even_outside_measured_window(self):
        for status in ("duplicate_plan", "non_monotonic_frame"):
            values = compiler_rows()
            values[1] = {key: values[1][key] for key in diagnostic.BASE_ROW_FIELDS}
            values[1][LIT_STATUS] = status
            values[-1].update(invalid=1, complete=True)
            with self.subTest(status=status), self.assertRaisesRegex(diagnostic.SmokeFailure, "structural"):
                diagnostic.analyze_rows(values, gather_rows(), LIT_UNIQUE)

    def test_invalid_record_must_not_fabricate_zero_statistics(self):
        values = compiler_rows()
        values[1][LIT_STATUS] = LIT_INVALID_SNAPSHOT
        values[-1].update(invalid=1, complete=True)
        with self.assertRaisesRegex(diagnostic.SmokeFailure, "row fields"):
            diagnostic.validate_compiler_rows(values)
        values[1][LIT_STATUS] = "unknown"
        with self.assertRaisesRegex(diagnostic.SmokeFailure, "unknown compiler status"):
            diagnostic.validate_compiler_rows(values)

    def test_footer_is_exact_consistent_and_complete(self):
        for field, bad in ((LIT_ATTEMPTS, 385), (LIT_STORED, 383), (LIT_OVERFLOW, 1), (LIT_INVALID, 1),
                           (LIT_COMPLETE, False), (LIT_COMPLETE, 1)):
            values = compiler_rows()
            values[-1][field] = bad
            with self.subTest(field=field), self.assertRaises(diagnostic.SmokeFailure):
                diagnostic.validate_compiler_rows(values)
        with self.assertRaises(diagnostic.SmokeFailure):
            diagnostic.validate_compiler_rows(compiler_rows()[:-1])
        with self.assertRaises(diagnostic.SmokeFailure):
            diagnostic.validate_compiler_rows(compiler_rows() + [compiler_rows()[-1]])
        with self.assertRaisesRegex(diagnostic.SmokeFailure, LIT_INCOMPLETE):
            diagnostic.validate_compiler_rows(compiler_rows(count=0))

    def test_real_capacity_overflow_is_not_accepted(self):
        values = compiler_rows(count=1024)
        values[-1].update(attempts=1025, overflow=1, complete=False)
        with self.assertRaisesRegex(diagnostic.SmokeFailure, LIT_INCOMPLETE):
            diagnostic.validate_compiler_rows(values)

    def test_missing_measured_source_is_rejected_even_with_valid_raw_sequence(self):
        values = compiler_rows()
        del values[101]  # source100 is measured; repair sequence/footer only, never the missing source.
        for sequence, row in enumerate(values[1:-1]):
            row[LIT_SEQUENCE] = sequence
        values[-1].update(attempts=383, stored=383)
        self.assertEqual(len(diagnostic.validate_compiler_rows(values)), 383)
        with self.assertRaisesRegex(diagnostic.SmokeFailure, "missing compiler snapshot.*100"):
            diagnostic.analyze_rows(values, gather_rows(), LIT_UNIQUE)

    def test_join_reuses_actual_gather_gpu_and_cpu_gates(self):
        values = gather_rows()
        values = [row for row in values if row.get(LIT_SCOPE) != "render.frame"]
        with self.assertRaisesRegex(diagnostic.SmokeFailure, "coverage is missing"):
            diagnostic.analyze_rows(compiler_rows(), values, LIT_UNIQUE)
        values = gather_rows()
        values[2][LIT_SOURCE_FRAME] = values[1][LIT_SOURCE_FRAME]
        with self.assertRaisesRegex(diagnostic.SmokeFailure, "duplicate, skipped"):
            diagnostic.analyze_rows(compiler_rows(), values, LIT_UNIQUE)

    def test_jsonl_rejects_duplicate_keys_nonfinite_blank_and_nonobject_rows(self):
        for data in (b'{"x":1,"x":2}\n', b'{"x":NaN}\n', b'{}\n\n', b'[]\n'):
            with self.subTest(data=data), self.assertRaises(diagnostic.SmokeFailure):
                diagnostic.decode_jsonl(data)

    def test_cli_publishes_exact_inputs_and_preserves_existing_output(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            compiler, result, output = (root / name for name in (LIT_COMPILER_JSONL, LIT_GATHER_JSONL, LIT_REPORT_JSON))
            raw = encode(compiler_rows())
            compiler.write_bytes(raw)
            result.write_bytes(encode(gather_rows()))
            arguments = cli_arguments(compiler, result, output)
            self.assertEqual(diagnostic.main(arguments), 0)
            published = output.read_bytes()
            report = json.loads(published)
            self.assertEqual(report[LIT_STATUS], "clean_joined_diagnostic")
            self.assertEqual(report[LIT_INPUTS][0][LIT_SHA256], hashlib.sha256(raw).hexdigest())
            self.assertEqual(report[LIT_RAW_COMPILER_ROWS], compiler_rows())
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(diagnostic.main(arguments), 1)
            self.assertEqual(output.read_bytes(), published)
            self.assertEqual(compiler.read_bytes(), raw)

    def test_cli_retains_failed_input_identity_without_success_claim(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            compiler, result, output = (root / name for name in (LIT_COMPILER_JSONL, LIT_GATHER_JSONL, LIT_REPORT_JSON))
            raw = encode(compiler_rows()[:-1])
            compiler.write_bytes(raw)
            result.write_bytes(encode(gather_rows()))
            self.assertEqual(diagnostic.main(cli_arguments(compiler, result, output)), 1)
            report = json.loads(output.read_text(encoding=LIT_UTF_8))
            self.assertEqual(report[LIT_STATUS], "failed_diagnostic")
            self.assertNotIn(LIT_PHASE_SECONDS, report)
            self.assertEqual(report[LIT_INPUTS][0][LIT_SHA256], hashlib.sha256(raw).hexdigest())
            self.assertEqual(compiler.read_bytes(), raw)

    def test_input_tampering_during_analysis_prevents_publication(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            compiler, result, output = (root / name for name in (LIT_COMPILER_JSONL, LIT_GATHER_JSONL, LIT_REPORT_JSON))
            compiler.write_bytes(encode(compiler_rows()))
            result.write_bytes(encode(gather_rows()))
            original = diagnostic.analyze_rows

            def changed(*arguments):
                report = original(*arguments)
                compiler.write_bytes(compiler.read_bytes() + b" ")
                return report

            with patch.object(diagnostic, "analyze_rows", side_effect=changed), contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(diagnostic.main(cli_arguments(compiler, result, output)), 1)
            self.assertFalse(output.exists())

    def test_diagnostic_environment_strips_inherited_path(self):
        env = gather.environment({LIT_NWB_GATHER_COMPILER_STATISTICS_FILE: LIT_INHERITED_JSONL}, LIT_UNIQUE, LIT_TIMING, LIT_SAMPLE_JSONL)
        self.assertNotIn(LIT_NWB_GATHER_COMPILER_STATISTICS_FILE, env)

    def test_explicit_diagnostic_path_preserves_all_other_environment_settings(self):
        base = {"PATH": "kept", LIT_NWB_GATHER_COMPILER_STATISTICS_FILE: LIT_INHERITED_JSONL}
        original = gather.environment(base, LIT_UNIQUE, LIT_TIMING, LIT_SAMPLE_JSONL)
        changed = gather.environment(base, LIT_UNIQUE, LIT_TIMING, LIT_SAMPLE_JSONL, LIT_TRIAL_COMPILER_STATISTICS_JSONL)
        self.assertEqual(changed.pop(LIT_NWB_GATHER_COMPILER_STATISTICS_FILE), LIT_TRIAL_COMPILER_STATISTICS_JSONL)
        self.assertEqual(changed, original)


if __name__ == LIT_MAIN:
    unittest.main()
