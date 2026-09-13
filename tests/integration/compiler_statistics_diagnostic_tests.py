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

gather = diagnostic.gather


def compiler_rows(count=384, offset=0):
    values = [{"type": "compiler_statistics_configuration", "schema": 1, "capacity": 1024}]
    for sequence in range(count):
        values.append({"type": "compiler_statistics", "sequence": sequence, "source_frame": offset + sequence,
            "status": "valid", "identity": {"graph_generation": 7, "plan_generation": sequence + 1,
                "device_generation": 2, "recording_attempt_generation": sequence + 100},
            "seconds": {field: (index + 1) * .001 for index, field in enumerate(diagnostic.SECONDS_FIELDS)},
            "counts": {field: index + 1 for index, field in enumerate(diagnostic.COUNT_FIELDS)},
            "submission": {"accepted_packets": 2, "accepted_tasks": 12, "rejected_packets": 0, "rejected_tasks": 0}})
    values.append({"type": "compiler_statistics_complete", "attempts": count, "stored": count,
                   "overflow": 0, "invalid": 0, "complete": count > 0})
    return values


def gather_rows(offset=0):
    values = [dict(gather.FROZEN_SETTINGS, type="configuration", schema=1, workload="unique", mode="timing",
        successful_frames=384, renderers=65, runtime_renderers=0, runtime_owners=0, transparent_renderers=64)]
    for source in range(offset + 96, offset + 352):
        values.append({"type": "cpu", "source_frame": source, "publish_frame": source + 1, "scopes_ms": {
            "graphics.frame": 8., "graphics.frame_preamble": .05, "graphics.prepare_resources": 2.,
            "graphics.render_passes": 2., "graphics.render": 5., "frame.project_update": .1,
            "graphics.present": .2, "graphics.begin_frame": .4}})
    for scope in gather.GPU_BASE + gather.GPU_TRANSPARENT:
        values.append({"type": "gpu", "scope": scope, "first_source_frame": offset + 96,
            "last_source_frame": offset + 351, "publish_frame": offset + 353, "total_ms": 256., "samples": 256})
    values.append({"type": "complete"})
    return values


def encode(rows):
    return ("\n".join(json.dumps(row, allow_nan=False) for row in rows) + "\n").encode("utf-8")


def cli_arguments(compiler, result, output):
    return ["--compiler-statistics", str(compiler), "--gather-result", str(result),
            "--workload", "unique", "--output", str(output)]


class CompilerStatisticsDiagnosticTests(unittest.TestCase):
    def test_joins_actual_source_ids_without_sorting_or_reindexing(self):
        values = compiler_rows(offset=5000)
        report = diagnostic.analyze_rows(values, gather_rows(offset=5000), "unique")
        self.assertEqual(report["joined_frames"], 256)
        self.assertEqual(report["source_window"], [5096, 5351])
        self.assertEqual([row["compiler_sequence"] for row in report["joined"]], list(range(96, 352)))
        self.assertEqual(report["joined"][0]["gather_publish_frame"], 5097)
        self.assertEqual(report["raw_compiler_rows"], values)
        self.assertEqual(report["joined"][0]["identity"], values[97]["identity"])
        self.assertAlmostEqual(report["phase_seconds"]["resource_state_planning"]["mean_seconds"], .011)
        self.assertEqual(report["phase_seconds"]["total"]["samples"], 256)
        self.assertEqual(len(report["unjoined_source_frames"]), 128)

    def test_configuration_requires_exact_fields_types_and_capacity(self):
        for key, bad in (("capacity", 1023), ("capacity", True), ("schema", True), ("schema", 2), ("extra", 0)):
            values = compiler_rows()
            values[0][key] = bad
            with self.subTest(key=key, value=bad), self.assertRaises(diagnostic.SmokeFailure):
                diagnostic.validate_compiler_rows(values)

    def test_sequence_and_source_order_fail_before_any_sorting(self):
        for field, bad in (("sequence", 0), ("sequence", 2), ("sequence", True), ("source_frame", 0)):
            values = compiler_rows()
            values[2][field] = bad
            with self.subTest(field=field, bad=bad), self.assertRaises(diagnostic.SmokeFailure):
                diagnostic.validate_compiler_rows(values)

    def test_each_identity_is_positive_exact_integer(self):
        for field in diagnostic.IDENTITY_FIELDS:
            for bad in (0, -1, True, 1., 1 << 64):
                values = compiler_rows()
                values[1]["identity"][field] = bad
                with self.subTest(field=field, bad=bad), self.assertRaisesRegex(diagnostic.SmokeFailure, "identity"):
                    diagnostic.validate_compiler_rows(values)

    def test_duplicate_plan_is_rejected_even_when_attempt_changes(self):
        values = compiler_rows()
        values[2]["identity"] = {**values[1]["identity"], "recording_attempt_generation": 999}
        with self.assertRaisesRegex(diagnostic.SmokeFailure, "duplicate compiler plan"):
            diagnostic.validate_compiler_rows(values)

    def test_device_generation_respects_owning_native_width(self):
        values = compiler_rows()
        values[1]["identity"]["device_generation"] = 65536
        with self.assertRaisesRegex(diagnostic.SmokeFailure, "native u16"):
            diagnostic.validate_compiler_rows(values)

    def test_plan_generations_are_scoped_by_graph_and_device(self):
        for field in ("graph_generation", "device_generation"):
            values = compiler_rows()
            values[2]["identity"] = {**values[1]["identity"], field: 999}
            self.assertEqual(len(diagnostic.validate_compiler_rows(values)), 384)

    def test_duration_and_count_schema_rejects_missing_extra_and_invalid_values(self):
        for bad in (-1., float("nan"), float("inf"), True, "0"):
            values = compiler_rows()
            values[1]["seconds"]["planning"] = bad
            with self.subTest(bad=bad), self.assertRaisesRegex(diagnostic.SmokeFailure, "duration"):
                diagnostic.validate_compiler_rows(values)
        for category, field in (("seconds", "analysis"), ("counts", "task"), ("submission", "accepted_tasks")):
            values = compiler_rows()
            del values[1][category][field]
            with self.assertRaisesRegex(diagnostic.SmokeFailure, "fields"):
                diagnostic.validate_compiler_rows(values)
            values = compiler_rows()
            values[1][category]["unexpected"] = 1
            with self.assertRaisesRegex(diagnostic.SmokeFailure, "fields"):
                diagnostic.validate_compiler_rows(values)
        for category, field in (("counts", "resource"), ("submission", "rejected_packets")):
            for bad in (-1, True, 1., 1 << 64):
                values = compiler_rows()
                values[1][category][field] = bad
                with self.assertRaisesRegex(diagnostic.SmokeFailure, "count"):
                    diagnostic.validate_compiler_rows(values)

    def test_flagged_early_invalid_rows_are_retained_without_claiming_clean_capture(self):
        for status in ("invalid_snapshot", "invalid_duration"):
            values = compiler_rows()
            values[1] = {key: values[1][key] for key in diagnostic.BASE_ROW_FIELDS}
            values[1]["status"] = status
            values[-1].update(invalid=1, complete=True)
            with self.subTest(status=status):
                report = diagnostic.analyze_rows(values, gather_rows(), "unique")
                self.assertEqual(report["status"], "joined_diagnostic_with_invalid_unmeasured")
                self.assertFalse(report["clean"])
                self.assertEqual(report["raw_invalid_count"], 1)
                self.assertEqual(report["invalid_unmeasured_rows"], [values[1]])
                self.assertEqual(report["raw_compiler_rows"], values)
                self.assertEqual(report["joined_frames"], 256)

    def test_same_invalid_flags_on_successful_measured_sources_fail(self):
        for status in ("invalid_snapshot", "invalid_duration"):
            values = compiler_rows()
            values[101] = {key: values[101][key] for key in diagnostic.BASE_ROW_FIELDS}
            values[101]["status"] = status
            values[-1].update(invalid=1, complete=True)
            with self.subTest(status=status), self.assertRaisesRegex(diagnostic.SmokeFailure, "invalid compiler snapshot.*100"):
                diagnostic.analyze_rows(values, gather_rows(), "unique")

    def test_structural_identity_order_status_fails_even_outside_measured_window(self):
        for status in ("duplicate_plan", "non_monotonic_frame"):
            values = compiler_rows()
            values[1] = {key: values[1][key] for key in diagnostic.BASE_ROW_FIELDS}
            values[1]["status"] = status
            values[-1].update(invalid=1, complete=True)
            with self.subTest(status=status), self.assertRaisesRegex(diagnostic.SmokeFailure, "structural"):
                diagnostic.analyze_rows(values, gather_rows(), "unique")

    def test_invalid_record_must_not_fabricate_zero_statistics(self):
        values = compiler_rows()
        values[1]["status"] = "invalid_snapshot"
        values[-1].update(invalid=1, complete=True)
        with self.assertRaisesRegex(diagnostic.SmokeFailure, "row fields"):
            diagnostic.validate_compiler_rows(values)
        values[1]["status"] = "unknown"
        with self.assertRaisesRegex(diagnostic.SmokeFailure, "unknown compiler status"):
            diagnostic.validate_compiler_rows(values)

    def test_footer_is_exact_consistent_and_complete(self):
        for field, bad in (("attempts", 385), ("stored", 383), ("overflow", 1), ("invalid", 1),
                           ("complete", False), ("complete", 1)):
            values = compiler_rows()
            values[-1][field] = bad
            with self.subTest(field=field), self.assertRaises(diagnostic.SmokeFailure):
                diagnostic.validate_compiler_rows(values)
        with self.assertRaises(diagnostic.SmokeFailure):
            diagnostic.validate_compiler_rows(compiler_rows()[:-1])
        with self.assertRaises(diagnostic.SmokeFailure):
            diagnostic.validate_compiler_rows(compiler_rows() + [compiler_rows()[-1]])
        with self.assertRaisesRegex(diagnostic.SmokeFailure, "incomplete"):
            diagnostic.validate_compiler_rows(compiler_rows(count=0))

    def test_real_capacity_overflow_is_not_accepted(self):
        values = compiler_rows(count=1024)
        values[-1].update(attempts=1025, overflow=1, complete=False)
        with self.assertRaisesRegex(diagnostic.SmokeFailure, "incomplete"):
            diagnostic.validate_compiler_rows(values)

    def test_missing_measured_source_is_rejected_even_with_valid_raw_sequence(self):
        values = compiler_rows()
        del values[101]  # source100 is measured; repair sequence/footer only, never the missing source.
        for sequence, row in enumerate(values[1:-1]):
            row["sequence"] = sequence
        values[-1].update(attempts=383, stored=383)
        self.assertEqual(len(diagnostic.validate_compiler_rows(values)), 383)
        with self.assertRaisesRegex(diagnostic.SmokeFailure, "missing compiler snapshot.*100"):
            diagnostic.analyze_rows(values, gather_rows(), "unique")

    def test_join_reuses_actual_gather_gpu_and_cpu_gates(self):
        values = gather_rows()
        values = [row for row in values if row.get("scope") != "render.frame"]
        with self.assertRaisesRegex(diagnostic.SmokeFailure, "coverage is missing"):
            diagnostic.analyze_rows(compiler_rows(), values, "unique")
        values = gather_rows()
        values[2]["source_frame"] = values[1]["source_frame"]
        with self.assertRaisesRegex(diagnostic.SmokeFailure, "duplicate, skipped"):
            diagnostic.analyze_rows(compiler_rows(), values, "unique")

    def test_jsonl_rejects_duplicate_keys_nonfinite_blank_and_nonobject_rows(self):
        for data in (b'{"x":1,"x":2}\n', b'{"x":NaN}\n', b'{}\n\n', b'[]\n'):
            with self.subTest(data=data), self.assertRaises(diagnostic.SmokeFailure):
                diagnostic.decode_jsonl(data)

    def test_cli_publishes_exact_inputs_and_preserves_existing_output(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            compiler, result, output = (root / name for name in ("compiler.jsonl", "gather.jsonl", "report.json"))
            raw = encode(compiler_rows())
            compiler.write_bytes(raw)
            result.write_bytes(encode(gather_rows()))
            arguments = cli_arguments(compiler, result, output)
            self.assertEqual(diagnostic.main(arguments), 0)
            published = output.read_bytes()
            report = json.loads(published)
            self.assertEqual(report["status"], "clean_joined_diagnostic")
            self.assertEqual(report["inputs"][0]["sha256"], hashlib.sha256(raw).hexdigest())
            self.assertEqual(report["raw_compiler_rows"], compiler_rows())
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(diagnostic.main(arguments), 1)
            self.assertEqual(output.read_bytes(), published)
            self.assertEqual(compiler.read_bytes(), raw)

    def test_cli_retains_failed_input_identity_without_success_claim(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            compiler, result, output = (root / name for name in ("compiler.jsonl", "gather.jsonl", "report.json"))
            raw = encode(compiler_rows()[:-1])
            compiler.write_bytes(raw)
            result.write_bytes(encode(gather_rows()))
            self.assertEqual(diagnostic.main(cli_arguments(compiler, result, output)), 1)
            report = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(report["status"], "failed_diagnostic")
            self.assertNotIn("phase_seconds", report)
            self.assertEqual(report["inputs"][0]["sha256"], hashlib.sha256(raw).hexdigest())
            self.assertEqual(compiler.read_bytes(), raw)

    def test_input_tampering_during_analysis_prevents_publication(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            compiler, result, output = (root / name for name in ("compiler.jsonl", "gather.jsonl", "report.json"))
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

    def test_diagnostic_environment_is_default_off_and_strips_inherited_path(self):
        env = gather.environment({"NWB_GATHER_COMPILER_STATISTICS_FILE": "inherited.jsonl"}, "unique", "timing", "sample.jsonl")
        self.assertNotIn("NWB_GATHER_COMPILER_STATISTICS_FILE", env)
        common = ["--baseline-executable", "a", "--baseline-runtime", "ar", "--baseline-source-manifest", "as.json",
            "--candidate-executable", "b", "--candidate-runtime", "br", "--candidate-source-manifest", "bs.json",
            "--logserver-executable", "logger", "--output-directory", "output", "--workload", "unique"]
        self.assertFalse(gather.parse_args(common).compiler_statistics)
        self.assertTrue(gather.parse_args(common + ["--compiler-statistics"]).compiler_statistics)

    def test_explicit_diagnostic_path_preserves_all_other_environment_settings(self):
        base = {"PATH": "kept", "NWB_GATHER_COMPILER_STATISTICS_FILE": "inherited.jsonl"}
        original = gather.environment(base, "unique", "timing", "sample.jsonl")
        changed = gather.environment(base, "unique", "timing", "sample.jsonl", "trial/compiler_statistics.jsonl")
        self.assertEqual(changed.pop("NWB_GATHER_COMPILER_STATISTICS_FILE"), "trial/compiler_statistics.jsonl")
        self.assertEqual(changed, original)


if __name__ == "__main__":
    unittest.main()
