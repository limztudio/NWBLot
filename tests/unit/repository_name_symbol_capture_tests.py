import importlib.util
import tempfile
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]


LIT_UNUSED = "unused"
LIT_RUN_WORKLOADS = "run_workloads"
LIT_LOG = "log"
LIT_BUILD = "build"
LIT_CONFIG = "--config"
LIT_OPT = "opt"
LIT_SKIP_BUILD = "--skip-build"
LIT_BUILD_DIR = "--build-dir"
LIT_CONFIGURE_PRESET = "--configure-preset"


NAME_SYMBOL_CAPTURE_SPEC = importlib.util.spec_from_file_location("nwb_test_generate_name_symbols", ROOT / "tests" / "name_symbols" / "generate_name_symbols.py")
if NAME_SYMBOL_CAPTURE_SPEC is None or NAME_SYMBOL_CAPTURE_SPEC.loader is None:
    raise ImportError("Could not load the test-owned name-symbol capture helper")
generate_name_symbols = importlib.util.module_from_spec(NAME_SYMBOL_CAPTURE_SPEC)
NAME_SYMBOL_CAPTURE_SPEC.loader.exec_module(generate_name_symbols)


class NameSymbolCaptureBoundaryTests(unittest.TestCase):
    def test_stale_sidecar_cleanup_failure_blocks_workloads_and_preserves_publication(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            capture = root / "capture"
            destination = root / "published"
            capture.mkdir()
            destination.mkdir()
            stale = capture / "probe.namesym"
            published = destination / stale.name
            stale_bytes = b"stale source symbols"
            published_bytes = b"previous valid symbols"
            fresh_bytes = b"freshly captured symbols"
            stale.write_bytes(stale_bytes)
            published.write_bytes(published_bytes)
            arguments = ["--source-dir", str(root), LIT_CONFIGURE_PRESET, LIT_UNUSED, "--build-preset", LIT_UNUSED, LIT_BUILD_DIR, str(root / LIT_BUILD), LIT_CONFIG, LIT_OPT,
                "--buildmode-bin-dir", str(capture), "--dest", str(destination), "--run", "probe", LIT_SKIP_BUILD,]
            removal_error = PermissionError("sidecar is locked")
            with (mock.patch.object(generate_name_symbols.os, "remove", side_effect=removal_error) as remove, mock.patch.object(generate_name_symbols, LIT_RUN_WORKLOADS) as workloads,
                mock.patch.object(generate_name_symbols, "collect_sidecars", wraps=generate_name_symbols.collect_sidecars) as collect, mock.patch.object(generate_name_symbols, LIT_LOG) as log,):
                self.assertEqual(1, generate_name_symbols.main(arguments))
                remove.assert_called_once_with(str(stale))
                workloads.assert_not_called()
                collect.assert_not_called()
                log.assert_any_call(generate_name_symbols.MSG_STALE_REMOVE_FAIL.format(str(stale), removal_error))
            self.assertEqual(stale_bytes, stale.read_bytes())
            self.assertEqual(published_bytes, published.read_bytes())
            self.assertEqual([published], list(destination.iterdir()))

            def capture_fresh_sidecar(_arguments):
                self.assertFalse(stale.exists())
                stale.write_bytes(fresh_bytes)

            with (mock.patch.object(generate_name_symbols, LIT_RUN_WORKLOADS, side_effect=capture_fresh_sidecar) as workloads, mock.patch.object(generate_name_symbols, LIT_LOG),):
                self.assertEqual(0, generate_name_symbols.main(arguments))
                workloads.assert_called_once()
            self.assertEqual(fresh_bytes, published.read_bytes())

