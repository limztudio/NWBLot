#!/usr/bin/env python3
import os
from pathlib import Path
import sys
import tempfile
import unittest


SMOKE_DIRECTORY = Path(__file__).resolve().parent
sys.path.insert(0, str(SMOKE_DIRECTORY))

from testbed_window_capture_smoke import (  # noqa: E402
    launch_captured_process,
    read_process_tail,
    terminate_process,
)


class ProcessOutputCaptureTests(unittest.TestCase):
    def test_large_output_does_not_block_and_tail_is_preserved(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            working_directory = Path(temp_dir)
            script = (
                "import sys\n"
                "sys.stdout.buffer.write(b'x' * (512 * 1024))\n"
                "sys.stdout.flush()\n"
                "sys.stderr.write('\\nwindow-capture-output-sentinel\\n')\n"
                "sys.stderr.flush()\n"
            )
            process = launch_captured_process(
                [sys.executable, "-c", script],
                working_directory,
                os.environ.copy(),
                "unit",
            )
            capture = process._nwb_output_capture
            try:
                self.assertIsNone(process.stdout)
                self.assertEqual(process.wait(timeout=5.0), 0)
                self.assertTrue(capture.path.exists())
                self.assertIn("window-capture-output-sentinel", read_process_tail(process))
            finally:
                terminate_process(process, "unit")

            self.assertFalse(capture.path.exists())


if __name__ == "__main__":
    unittest.main()
