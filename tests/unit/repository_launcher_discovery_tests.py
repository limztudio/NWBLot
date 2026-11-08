import re
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from launcher import constants as launcher
from launcher.discovery import LauncherDiscovery


LIT_GROUP = "group"
LIT_SOURCE = "source"
LIT_UTF_8 = "utf-8"
LIT_LAUNCH_PY = "launch.py"
LIT_TESTS = "tests"
LIT_SMOKE = "smoke"
LIT_LAUNCHER_PY = "launcher.py"
LIT_AB = "ab"
LIT_ASYNC_SHADOW_M4 = "async_shadow_m4"
LIT_COMMAND_IR = "command_ir"
LIT_FRAME_LAGGED_ASYNC_LIGHTING = "frame_lagged_async_lighting"
LIT_RUN_PY = "run.py"
LIT_HARDWARE_SHADOW_BOUNDARY = "hardware_shadow_boundary"
LIT_TRANSFER_QUEUE = "transfer_queue"
LIT_UTILITIES = "utilities"
LIT_TEX_CONV = "tex_conv"
LIT_ASYNC_SHADOW_M4_2 = "async-shadow-m4"
LIT_SAME_NAME = "same_name"
LIT_A = r"\A"
LIT_Z = r"\Z"


class LauncherDiscoveryTests(unittest.TestCase):
    def test_directory_discovery_only_returns_direct_children(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            paths = (root / LIT_TESTS / LIT_LAUNCH_PY, root / LIT_TESTS / LIT_AB / LIT_LAUNCH_PY, root / LIT_TESTS / LIT_AB / LIT_ASYNC_SHADOW_M4 / LIT_LAUNCH_PY, root / LIT_TESTS / LIT_AB / LIT_COMMAND_IR / LIT_LAUNCH_PY,
                root / LIT_TESTS / LIT_AB / LIT_FRAME_LAGGED_ASYNC_LIGHTING / LIT_LAUNCH_PY, root / LIT_TESTS / LIT_AB / LIT_HARDWARE_SHADOW_BOUNDARY / LIT_LAUNCH_PY, root / LIT_TESTS / LIT_AB / LIT_TRANSFER_QUEUE / LIT_LAUNCH_PY,
                root / LIT_TESTS / LIT_SMOKE / LIT_LAUNCH_PY, root / LIT_TESTS / LIT_SMOKE / LIT_LAUNCHER_PY, root / LIT_TESTS / LIT_SMOKE / LIT_RUN_PY,)
            for path in paths:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("", encoding=LIT_UTF_8)

            tests_launchers = LauncherDiscovery.discover_directory_launchers(Path(LIT_TESTS), root)
            ab_launchers = LauncherDiscovery.discover_directory_launchers(Path(LIT_TESTS) / LIT_AB, root)

        self.assertEqual({LIT_AB, LIT_SMOKE}, set(tests_launchers))
        self.assertNotIn(LIT_ASYNC_SHADOW_M4_2, tests_launchers)
        self.assertIn(LIT_ASYNC_SHADOW_M4_2, ab_launchers)
    def test_generated_source_copies_do_not_hide_current_authored_launchers(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            category = root / LIT_TESTS
            authored = (category / LIT_LAUNCH_PY, category / LIT_SMOKE / LIT_LAUNCH_PY, category / LIT_GROUP / LIT_LAUNCH_PY, category / LIT_GROUP / LIT_SOURCE / LIT_LAUNCH_PY)
            generated = tuple(path for artifact in launcher.LAUNCHER_ARTIFACT_DIRECTORIES for path in (category / artifact / LIT_LAUNCH_PY, category / artifact / "frozen" / LIT_TESTS / LIT_SMOKE / LIT_LAUNCH_PY,
                category / LIT_SMOKE / artifact / "old" / LIT_LAUNCH_PY))
            for path in (*authored, *generated):
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("", encoding=LIT_UTF_8)
            generated_bytes = {path: path.read_bytes() for path in generated}

            direct = LauncherDiscovery.discover_directory_launchers(Path(LIT_TESTS), root)
            leaves = LauncherDiscovery.discover_repo_launchers(root)

            self.assertEqual({LIT_SMOKE, LIT_GROUP}, set(direct))
            self.assertEqual({LIT_SMOKE, LIT_SOURCE}, set(leaves))
            self.assertEqual(Path(LIT_TESTS) / LIT_GROUP / LIT_SOURCE / LIT_LAUNCH_PY, leaves[LIT_SOURCE].script)
            self.assertEqual(generated_bytes, {path: path.read_bytes() for path in generated})
    def test_duplicate_launch_commands_are_rejected(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            for category in (root / LIT_TESTS, root / LIT_UTILITIES):
                category.mkdir(parents=True)
                (category / LIT_LAUNCH_PY).write_text("", encoding=LIT_UTF_8)
            for path in (root / LIT_TESTS / LIT_SAME_NAME / LIT_LAUNCH_PY, root / LIT_UTILITIES / LIT_SAME_NAME / LIT_LAUNCH_PY,):
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("", encoding=LIT_UTF_8)

            with self.assertRaisesRegex(SystemExit, "duplicate launch command 'same-name'"):
                LauncherDiscovery.discover_repo_launchers(root)
    def test_category_with_leaf_launchers_requires_a_router(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            path = root / LIT_UTILITIES / LIT_TEX_CONV / LIT_LAUNCH_PY
            path.parent.mkdir(parents=True)
            path.write_text("", encoding=LIT_UTF_8)

            with self.assertRaisesRegex(SystemExit, LIT_A + re.escape(f"missing category launcher: {Path(LIT_UTILITIES) / LIT_LAUNCH_PY}") + LIT_Z,):
                LauncherDiscovery.discover_repo_launchers(root)
    def test_nested_leaf_requires_an_intermediate_router(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            for path in (root / LIT_TESTS / LIT_LAUNCH_PY, root / LIT_TESTS / LIT_AB / LIT_ASYNC_SHADOW_M4 / LIT_LAUNCH_PY,):
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("", encoding=LIT_UTF_8)

            with self.assertRaisesRegex(SystemExit, LIT_A + re.escape(f"missing directory launcher: {Path(LIT_TESTS) / LIT_AB / LIT_LAUNCH_PY}") + LIT_Z,):
                LauncherDiscovery.discover_repo_launchers(root)


class LauncherDiscoveryFailureTests(unittest.TestCase):
    def test_failed_walk_and_directory_scan_do_not_return_partial_lists(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            search = root / LIT_TESTS
            search.mkdir()

            with mock.patch("launcher.discovery.os.scandir", side_effect=PermissionError("walk denied")):
                with self.assertRaisesRegex(PermissionError, "walk denied"):
                    LauncherDiscovery.discover_leaf_launchers(search, root)
            with mock.patch("launcher.discovery.os.scandir", side_effect=PermissionError("scan denied")):
                with self.assertRaisesRegex(PermissionError, "scan denied"):
                    LauncherDiscovery.discover_directory_launchers(search, root)
            self.assertEqual({}, LauncherDiscovery.discover_leaf_launchers(root / "missing", root))

    def test_entry_type_failure_does_not_hide_a_subtree(self):
        entry = mock.Mock()
        entry.is_symlink.return_value = False
        entry.is_dir.side_effect = PermissionError("entry denied")
        scan = mock.MagicMock()
        scan.__enter__.return_value = iter([entry])
        with mock.patch("launcher.discovery.os.scandir", return_value=scan):
            with self.assertRaisesRegex(PermissionError, "entry denied"):
                LauncherDiscovery._launcher_scripts(Path("ignored"))

    def test_ignored_artifacts_are_not_inspected(self):
        entry = mock.Mock()
        entry.name = next(iter(launcher.LAUNCHER_ARTIFACT_DIRECTORIES))
        entry.is_dir.side_effect = PermissionError("artifact denied")
        scan = mock.MagicMock()
        scan.__enter__.return_value = iter([entry])
        with mock.patch("launcher.discovery.os.scandir", return_value=scan):
            self.assertEqual([], LauncherDiscovery._launcher_scripts(Path("ignored")))
        entry.is_dir.assert_not_called()

    def test_unrelated_directory_link_target_is_not_inspected(self):
        entry = mock.Mock()
        entry.name = "external"
        entry.is_symlink.return_value = True
        entry.is_dir.side_effect = PermissionError("link target denied")
        scan = mock.MagicMock()
        scan.__enter__.return_value = iter([entry])
        with mock.patch("launcher.discovery.os.scandir", return_value=scan):
            self.assertEqual([], LauncherDiscovery._launcher_scripts(Path("ignored")))
        entry.is_dir.assert_not_called()

    def test_launcher_symlink_is_kept_unless_it_points_to_a_directory(self):
        for is_directory in (False, True):
            entry = mock.Mock()
            entry.name = LIT_LAUNCH_PY
            entry.path = str(Path("ignored") / LIT_LAUNCH_PY)
            entry.is_symlink.return_value = True
            entry.is_dir.return_value = is_directory
            scan = mock.MagicMock()
            scan.__enter__.return_value = iter([entry])
            with self.subTest(is_directory=is_directory), mock.patch("launcher.discovery.os.scandir", return_value=scan):
                self.assertEqual([] if is_directory else [Path(entry.path)], LauncherDiscovery._launcher_scripts(Path("ignored")))

    def test_inspection_errors_are_not_reported_as_absent(self):
        with mock.patch.object(Path, "stat", side_effect=PermissionError("stat denied")):
            with self.assertRaisesRegex(PermissionError, "stat denied"):
                LauncherDiscovery._is_directory(Path("unreadable"))
            with self.assertRaisesRegex(PermissionError, "stat denied"):
                LauncherDiscovery._is_file(Path("unreadable"))

