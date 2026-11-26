#!/usr/bin/env python3

import contextlib
import io
import os
import pathlib
import stat
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))
from utilities.ui_skin import generate_default as generator
from utilities.ui_skin import skin_publisher as publisher


NAMES = ("source.png", "atlas.nwb", "texture.nwb", "texture.tex")


def generate_fixture(directory: pathlib.Path) -> None:
    (directory / "source.png").write_bytes(b"new source")
    (directory / "atlas.nwb").write_bytes(b"new atlas")


def convert_fixture(command, **kwargs):
    base = pathlib.Path(command[3])
    base.with_suffix(".nwb").write_bytes(b"new texture metadata")
    base.with_suffix(".tex").write_bytes(b"new texture")
    return subprocess.CompletedProcess(command, 0)


class UiSkinPublicationTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="nwb_skin_publish_")
        self.addCleanup(self.temporary.cleanup)
        self.directory = pathlib.Path(self.temporary.name)
        for name in NAMES:
            (self.directory / name).write_bytes(("old " + name).encode())
        (self.directory / "unrelated").write_bytes(b"keep")

    def assert_previous(self):
        for name in NAMES:
            self.assertEqual((self.directory / name).read_bytes(), ("old " + name).encode())
        self.assertEqual((self.directory / "unrelated").read_bytes(), b"keep")

    def make_replace_fault(self, step_errors):
        replace = publisher.os.replace
        calls = 0

        def wrapped(source, target):
            nonlocal calls
            calls += 1
            if calls in step_errors:
                error = step_errors[calls]
                raise (error() if callable(error) else error)
            return replace(source, target)

        return wrapped

    def broken_stderr_stream(self):
        stream = mock.Mock()
        stream.write.side_effect = BrokenPipeError("stderr closed")
        return stream

    def fail_generation(self, directory):
        (directory / "source.png").write_bytes(b"partial")
        raise OSError("generation failed")

    def assert_recovery_stage_preserved(self, expected_backup=None):
        stages = list(self.directory.glob(".ui_skin_*"))
        self.assertEqual(len(stages), 1)
        if expected_backup is None:
            self.assertTrue(any(stages[0].glob("*.previous")))
        else:
            self.assertTrue((stages[0] / expected_backup).is_file())
        return stages[0]

    def test_converter_failure_and_incomplete_output_preserve_all_files(self):
        with mock.patch.object(generator, "_generate_into", side_effect=generate_fixture):
            for result in (subprocess.CalledProcessError(1, "converter"), subprocess.CompletedProcess([], 0)):
                with self.subTest(result=result), mock.patch.object(publisher.subprocess, "run", side_effect=result if isinstance(result, Exception) else None, return_value=result):
                    with self.assertRaises((subprocess.CalledProcessError, RuntimeError)):
                        publisher.SkinPublisher(self.directory, pathlib.Path("converter"), generator._generate_into).publish()
                    self.assert_previous()

    def test_each_publication_failure_restores_all_previous_files(self):
        for failure_step in range(1, 9):
            with self.subTest(failure_step=failure_step), mock.patch.object(generator, "_generate_into", side_effect=generate_fixture), mock.patch.object(publisher.subprocess, "run", side_effect=convert_fixture), mock.patch.object(publisher.os, "replace", side_effect=self.make_replace_fault({failure_step: OSError("injected publication failure")})):
                with self.assertRaises(OSError):
                    publisher.SkinPublisher(self.directory, pathlib.Path("converter"), generator._generate_into).publish()
                self.assert_previous()
                self.assertEqual(list(self.directory.glob(".ui_skin_*")), [])

    def test_rollback_failure_preserves_recovery_files(self):
        fault = self.make_replace_fault({4: OSError("injected failure"), 5: OSError("injected failure")})
        with mock.patch.object(generator, "_generate_into", side_effect=generate_fixture), mock.patch.object(publisher.subprocess, "run", side_effect=convert_fixture), mock.patch.object(publisher.os, "replace", side_effect=fault):
            with self.assertRaisesRegex(RuntimeError, "recovery files remain"):
                publisher.SkinPublisher(self.directory, pathlib.Path("converter"), generator._generate_into).publish()
        self.assert_recovery_stage_preserved()

    def test_broken_stderr_does_not_replace_rollback_failure(self):
        fault = self.make_replace_fault({4: OSError("injected failure"), 5: OSError("injected failure")})
        with mock.patch.object(generator, "_generate_into", side_effect=generate_fixture), mock.patch.object(publisher.subprocess, "run", side_effect=convert_fixture), mock.patch.object(publisher.os, "replace", side_effect=fault), mock.patch.object(publisher.sys, "stderr", self.broken_stderr_stream()):
            with self.assertRaisesRegex(RuntimeError, "recovery files remain"):
                publisher.SkinPublisher(self.directory, pathlib.Path("converter"), generator._generate_into).publish()
        self.assert_recovery_stage_preserved()

    def test_failure_removes_new_files_and_restores_existing_files(self):
        (self.directory / "atlas.nwb").unlink()
        (self.directory / "texture.tex").unlink()
        fault = self.make_replace_fault({6: OSError("injected publication failure")})
        with mock.patch.object(generator, "_generate_into", side_effect=generate_fixture), mock.patch.object(publisher.subprocess, "run", side_effect=convert_fixture), mock.patch.object(publisher.os, "replace", side_effect=fault):
            with self.assertRaises(OSError):
                publisher.SkinPublisher(self.directory, pathlib.Path("converter"), generator._generate_into).publish()
        self.assertEqual((self.directory / "source.png").read_bytes(), b"old source.png")
        self.assertEqual((self.directory / "texture.nwb").read_bytes(), b"old texture.nwb")
        self.assertFalse((self.directory / "atlas.nwb").exists())
        self.assertFalse((self.directory / "texture.tex").exists())

    def test_success_without_converter_leaves_texture_pair_unchanged(self):
        with mock.patch.object(generator, "_generate_into", side_effect=generate_fixture):
            publisher.SkinPublisher(self.directory, None, generator._generate_into).publish()
        self.assertEqual((self.directory / "source.png").read_bytes(), b"new source")
        self.assertEqual((self.directory / "atlas.nwb").read_bytes(), b"new atlas")
        for name in NAMES[2:]:
            self.assertEqual((self.directory / name).read_bytes(), ("old " + name).encode())

    def test_success_with_converter_publishes_all_outputs(self):
        with mock.patch.object(generator, "_generate_into", side_effect=generate_fixture), mock.patch.object(publisher.subprocess, "run", side_effect=convert_fixture):
            publisher.SkinPublisher(self.directory, pathlib.Path("converter"), generator._generate_into).publish()
        for name, expected in zip(NAMES, (b"new source", b"new atlas", b"new texture metadata", b"new texture")):
            self.assertEqual((self.directory / name).read_bytes(), expected)
        self.assertEqual((self.directory / "unrelated").read_bytes(), b"keep")
        self.assertEqual(list(self.directory.glob(".ui_skin_*")), [])

    def test_generation_failure_preserves_outputs(self):
        with mock.patch.object(generator, "_generate_into", side_effect=self.fail_generation):
            with self.assertRaises(OSError):
                generator.generate(self.directory)
        self.assert_previous()

    def test_broken_stderr_does_not_replace_generation_failure(self):
        with mock.patch.object(generator, "_generate_into", side_effect=self.fail_generation), mock.patch.object(pathlib.Path, "rmdir", side_effect=OSError("cleanup failed")), mock.patch.object(publisher.sys, "stderr", self.broken_stderr_stream()):
            with self.assertRaisesRegex(OSError, "generation failed"):
                generator.generate(self.directory)
        self.assert_previous()
        self.assertEqual(len(list(self.directory.glob(".ui_skin_*"))), 1)

    @unittest.skipUnless(os.name == "posix", "POSIX mode and ownership semantics")
    def test_existing_restrictive_modes_survive_publication(self):
        for name in NAMES:
            os.chmod(self.directory / name, 0o600)
        with mock.patch.object(generator, "_generate_into", side_effect=generate_fixture), mock.patch.object(publisher.subprocess, "run", side_effect=convert_fixture):
            publisher.SkinPublisher(self.directory, pathlib.Path("converter"), generator._generate_into).publish()
        for name in NAMES:
            self.assertEqual(stat.S_IMODE((self.directory / name).stat().st_mode), 0o600)

    def test_target_inspection_error_does_not_replace_existing_files(self):
        lstat = pathlib.Path.lstat
        blocked = self.directory / "atlas.nwb"

        def fail_inspection(path, *args, **kwargs):
            if path == blocked:
                raise PermissionError("injected inspection failure")
            return lstat(path, *args, **kwargs)

        with mock.patch.object(generator, "_generate_into", side_effect=generate_fixture), mock.patch.object(pathlib.Path, "lstat", autospec=True, side_effect=fail_inspection):
            with self.assertRaises(PermissionError):
                publisher.SkinPublisher(self.directory, None, generator._generate_into).publish()
        self.assert_previous()

    def test_interrupted_rollback_keeps_unrestored_backups(self):
        fault = self.make_replace_fault({4: OSError("injected publication failure"), 5: KeyboardInterrupt})
        output = io.StringIO()
        with mock.patch.object(generator, "_generate_into", side_effect=generate_fixture), mock.patch.object(publisher.subprocess, "run", side_effect=convert_fixture), mock.patch.object(publisher.os, "replace", side_effect=fault), contextlib.redirect_stderr(output):
            with self.assertRaises(KeyboardInterrupt):
                publisher.SkinPublisher(self.directory, pathlib.Path("converter"), generator._generate_into).publish()
        stage = self.assert_recovery_stage_preserved()
        self.assertIn(str(stage), output.getvalue())

    def test_interrupted_rollback_reports_recovery_with_legacy_exception_and_broken_stderr(self):
        class LegacyInterrupt(KeyboardInterrupt):
            add_note = None

        fault = self.make_replace_fault({4: OSError("injected publication failure"), 5: LegacyInterrupt})
        with mock.patch.object(generator, "_generate_into", side_effect=generate_fixture), mock.patch.object(publisher.subprocess, "run", side_effect=convert_fixture), mock.patch.object(publisher.os, "replace", side_effect=fault), mock.patch.object(publisher.sys, "stderr", self.broken_stderr_stream()):
            with self.assertRaises(LegacyInterrupt) as caught:
                publisher.SkinPublisher(self.directory, pathlib.Path("converter"), generator._generate_into).publish()
        stage = self.assert_recovery_stage_preserved()
        self.assertIn(str(stage), str(caught.exception))

    def test_backup_inspection_failure_keeps_recovery_files(self):
        lstat = pathlib.Path.lstat
        fault = self.make_replace_fault({4: OSError("injected publication failure")})

        def fail_backup_inspection(path, *args, **kwargs):
            if path.name == "atlas.nwb.previous":
                raise PermissionError("injected backup inspection failure")
            return lstat(path, *args, **kwargs)

        with mock.patch.object(generator, "_generate_into", side_effect=generate_fixture), mock.patch.object(publisher.subprocess, "run", side_effect=convert_fixture), mock.patch.object(publisher.os, "replace", side_effect=fault), mock.patch.object(pathlib.Path, "lstat", autospec=True, side_effect=fail_backup_inspection):
            with self.assertRaisesRegex(RuntimeError, "recovery files remain"):
                publisher.SkinPublisher(self.directory, pathlib.Path("converter"), generator._generate_into).publish()
        self.assert_recovery_stage_preserved("atlas.nwb.previous")


if __name__ == "__main__":
    unittest.main()
