from contextlib import redirect_stderr, redirect_stdout
import ctypes
import io
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock


class FailureReportTests(unittest.TestCase):
    def test_unwritable_report_keeps_primary_exception(self):
        from smoke_failure_reporting import write_failure_report

        def broken_writer(_path, _value):
            raise PermissionError("report denied")

        captured = io.StringIO()
        with redirect_stderr(captured), self.assertRaisesRegex(ValueError, "primary failure"):
            try:
                raise ValueError("primary failure")
            except ValueError:
                write_failure_report(broken_writer, Path("failure.json"), {})
                raise
        self.assertIn("report denied", captured.getvalue())

class DepthWarpFailureReportTests(unittest.TestCase):
    def test_unwritable_skip_report_preserves_skip_exit(self):
        import avboit_depth_warp_probe_smoke as probe

        with tempfile.TemporaryDirectory() as root:
            args = SimpleNamespace(output_directory=Path(root))
            captured = io.StringIO()
            with mock.patch.object(probe, "parse_args", return_value=args), \
                    mock.patch.object(probe, "acquire", side_effect=probe.WindowsDesktopUnavailable("desktop unavailable")), \
                    mock.patch.object(probe, "write_json", side_effect=OSError("report denied")), \
                    redirect_stderr(captured), redirect_stdout(io.StringIO()):
                self.assertEqual(probe.main([]), probe.SKIP_EXIT_CODE)
            self.assertIn("desktop unavailable", captured.getvalue())
            self.assertIn("report denied", captured.getvalue())

class CleanupErrorPreservationTests(unittest.TestCase):
    def test_pdh_setup_error_survives_close_error(self):
        import gpu_load_telemetry as telemetry

        api = mock.Mock()
        def open_query(_source, _data, query):
            ctypes.cast(query, ctypes.POINTER(ctypes.c_void_p))[0] = ctypes.c_void_p(1)
            return 0
        api.PdhOpenQueryW.side_effect = open_query
        api.PdhAddEnglishCounterW.return_value = 1
        api.PdhCloseQuery.return_value = 2
        captured = io.StringIO()
        with mock.patch.object(telemetry, "os", SimpleNamespace(name="nt")), \
                mock.patch.object(telemetry.ctypes, "WinDLL", return_value=api, create=True), redirect_stderr(captured):
            with self.assertRaisesRegex(telemetry.SmokeFailure, "add engine counter"):
                telemetry.PdhEngineQuery()
        self.assertIn("PDH close failed", captured.getvalue())

    def test_partial_upload_error_survives_socket_close_error(self):
        module_path = str(Path(__file__).resolve().parent / "logger_server")
        with mock.patch.object(sys, "path", [module_path, *sys.path]):
            import aborted_upload_cli_tests as aborted

        connection = mock.Mock()
        connection.sendall.side_effect = OSError("send failed")
        connection.close.side_effect = OSError("close failed")
        captured = io.StringIO()
        with mock.patch.object(aborted.socket, "create_connection", return_value=connection), redirect_stderr(captured):
            with self.assertRaisesRegex(OSError, "send failed"):
                aborted.start_partial_upload(1, None, None, 1, b"marker")
        connection.close.assert_called_once_with()
        self.assertIn("close failed", captured.getvalue())
