#!/usr/bin/env python3
import os
import argparse
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

import launcher


class LauncherPlatformTests(unittest.TestCase):
    def test_host_platform_names_match_cmake_output_names(self):
        self.assertEqual("windows", launcher.host_platform_name("Windows"))
        self.assertEqual("linux", launcher.host_platform_name("Linux"))
        self.assertEqual("darwin", launcher.host_platform_name("Darwin"))

    def test_default_build_dirs_follow_platform_domain_and_arch(self):
        root = Path(os.sep) / "repo"
        self.assertEqual(
            root / "__cmake" / "build" / "windows-clang-engine-x64",
            launcher.default_build_dir(root, "windows", "engine", "x64"),
        )
        self.assertEqual(
            root / "__cmake" / "build" / "linux-clang-x64",
            launcher.default_build_dir(root, "linux", "full", "x64"),
        )

    def test_default_build_presets_follow_platform_domain_and_config(self):
        self.assertEqual("linux-clang-dbg", launcher.default_build_preset_name("linux", "full", "dbg"))
        self.assertEqual("linux-clang-engine-fin", launcher.default_build_preset_name("linux", "engine", "fin"))

    def test_explicit_configure_preset_selects_matching_build_directory(self):
        root = Path(os.sep) / "repo"
        args = argparse.Namespace(
            repo_root=root,
            platform="linux",
            arch="x64",
            domain=None,
            configure_preset="linux-clang-engine-x64",
            build_dir=None,
            config="dbg",
            cmake=None,
        )
        settings = launcher.resolve_launch_settings(args, "full")
        self.assertEqual(root / "__cmake" / "build" / "linux-clang-engine-x64", settings.build_dir)
        self.assertEqual("engine", settings.domain)

    def test_output_root_matches_engine_and_domain_layouts(self):
        root = Path(os.sep) / "repo"
        self.assertEqual(
            root / "__exec" / "windows" / "x64",
            launcher.output_root(root, "windows", "x64", "engine"),
        )
        self.assertEqual(
            root / "__exec" / "linux" / "x64" / "full",
            launcher.output_root(root, "linux", "x64", "full"),
        )

    def test_infer_output_domain_from_known_preset_names(self):
        build_root = Path(os.sep) / "repo" / "__cmake" / "build"
        self.assertEqual("engine", launcher.infer_output_domain(build_root / "linux-clang-engine-x64", "linux", "x64"))
        self.assertEqual("full", launcher.infer_output_domain(build_root / "linux-clang-x64", "linux", "x64"))
        self.assertEqual("testbed", launcher.infer_output_domain(build_root / "windows-clang-testbed-x64", "windows", "x64"))

    def test_target_output_convention_strips_nwb_prefix(self):
        self.assertEqual("resource_cooker", launcher.target_default_executable_base_name("nwb_resource_cooker"))
        self.assertEqual("testbed", launcher.target_default_executable_base_name("testbed"))

    def test_cmake_cache_domain_overrides_directory_name(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            build_dir = Path(temp_dir)
            (build_dir / "CMakeCache.txt").write_text("NWB_OUTPUT_DOMAIN:UNINITIALIZED=full\n", encoding="utf-8")
            self.assertEqual("full", launcher.infer_output_domain(build_dir, "linux", "x64"))

    def test_required_cache_defines_accept_cmake_bool_values(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            build_dir = Path(temp_dir)
            (build_dir / "CMakeCache.txt").write_text("NWB_BUILD_TESTS:BOOL=ON\n", encoding="utf-8")
            self.assertTrue(launcher.cache_matches_required_defines(build_dir, {"NWB_BUILD_TESTS": "TRUE"}))
            self.assertFalse(launcher.cache_matches_required_defines(build_dir, {"NWB_BUILD_TESTS": "OFF"}))

    def test_profile_required_defines_are_opt_in(self):
        self.assertEqual({}, launcher.profile_required_defines(argparse.Namespace(with_profile=False)))
        self.assertEqual(
            {"NWB_BUILD_LOGSERVER": "ON"},
            launcher.profile_required_defines(argparse.Namespace(with_profile=True)),
        )

    def test_profile_client_args_point_runtime_at_logserver(self):
        args = argparse.Namespace(profile_log_address="http://localhost")
        session = launcher.ProfileSession(8123, Path(os.sep) / "repo" / "logserver", None)
        self.assertEqual(["-a", "http://localhost", "-p", "8123"], launcher.profile_client_args(args, session))

    def test_run_parser_accepts_profile_options(self):
        args = launcher.make_parser().parse_args(["run", "testbed", "--with-profile", "--profile-log-port", "8123"])
        self.assertTrue(args.with_profile)
        self.assertEqual(8123, args.profile_log_port)

    def test_discovers_directory_and_explicit_script_launchers(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            paths = (
                root / "CoolStuff" / "launch.py",
                root / "CoolStuff" / "Testbed" / "launch.py",
                root / "tests" / "launch.py",
                root / "tests" / "smoke" / "launch.py",
                root / "tests" / "smoke" / "launcher.py",
                root / "tests" / "ab" / "async_shadow_m4" / "launch.py",
                root / "tests" / "ab" / "frame_lagged" / "run.py",
                root / "tests" / "ab" / "frame_lagged" / "helper.py",
                root / "utilities" / "launch.py",
                root / "utilities" / "tex_conv" / "launch.py",
            )
            for path in paths:
                path.parent.mkdir(parents=True, exist_ok=True)
                contents = "NWB_LAUNCH_COMMAND = 'frame-lagged'\n" if path.name == "run.py" else ""
                path.write_text(contents, encoding="utf-8")

            launchers = launcher.discover_repo_launchers(root)

        self.assertEqual(
            {
                "async-shadow-m4": Path("tests/ab/async_shadow_m4/launch.py"),
                "frame-lagged": Path("tests/ab/frame_lagged/run.py"),
                "smoke": Path("tests/smoke/launch.py"),
                "testbed": Path("CoolStuff/Testbed/launch.py"),
                "tex-conv": Path("utilities/tex_conv/launch.py"),
            },
            {command: discovered.script for command, discovered in launchers.items()},
        )
        self.assertEqual(
            {
                "async-shadow-m4": Path("tests/launch.py"),
                "frame-lagged": Path("tests/launch.py"),
                "smoke": Path("tests/launch.py"),
                "testbed": Path("CoolStuff/launch.py"),
                "tex-conv": Path("utilities/launch.py"),
            },
            {command: discovered.router for command, discovered in launchers.items()},
        )
        self.assertFalse({"coolstuff", "tests", "utilities"}.intersection(launchers))

    def test_launch_py_takes_precedence_over_legacy_launcher_py(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            category = root / "tests"
            category.mkdir(parents=True)
            (category / "launch.py").write_text("", encoding="utf-8")
            directory = root / "tests" / "smoke"
            directory.mkdir(parents=True)
            (directory / "launch.py").write_text("", encoding="utf-8")
            (directory / "launcher.py").write_text("", encoding="utf-8")

            launchers = launcher.discover_repo_launchers(root)

        self.assertEqual(Path("tests/smoke/launch.py"), launchers["smoke"].script)

    def test_duplicate_launch_commands_require_an_override(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            for category in (root / "tests", root / "utilities"):
                category.mkdir(parents=True)
                (category / "launch.py").write_text("", encoding="utf-8")
            for path in (
                root / "tests" / "same_name" / "launch.py",
                root / "utilities" / "same_name" / "launch.py",
            ):
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("", encoding="utf-8")

            with self.assertRaisesRegex(SystemExit, "duplicate launch command 'same-name'"):
                launcher.discover_repo_launchers(root)

    def test_category_with_leaf_launchers_requires_a_router(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            path = root / "utilities" / "tex_conv" / "launch.py"
            path.parent.mkdir(parents=True)
            path.write_text("", encoding="utf-8")

            with self.assertRaisesRegex(SystemExit, "missing category launcher: utilities/launch.py"):
                launcher.discover_repo_launchers(root)

    def test_discovered_launcher_forwards_its_arguments(self):
        repo_launchers = {
            "async-shadow-m4": launcher.RepoLauncher(
                "async-shadow-m4",
                Path("tests/ab/async_shadow_m4/launch.py"),
                Path("tests/launch.py"),
            )
        }
        with (
            mock.patch.object(launcher, "discover_repo_launchers", return_value=repo_launchers),
            mock.patch.object(launcher, "run_repo_script", return_value=0) as run_repo_script,
        ):
            self.assertEqual(0, launcher.main(["async-shadow-m4", "--dry-run"]))
        run_repo_script.assert_called_once_with(
            Path("tests/launch.py"),
            ["async-shadow-m4", "--dry-run"],
            echo=True,
        )

    def test_category_launcher_forwards_to_its_leaf(self):
        repo_launchers = {
            "async-shadow-m4": launcher.RepoLauncher(
                "async-shadow-m4",
                Path("tests/ab/async_shadow_m4/launch.py"),
            )
        }
        with (
            mock.patch.object(launcher, "discover_directory_launchers", return_value=repo_launchers),
            mock.patch.object(launcher, "run_repo_script", return_value=0) as run_repo_script,
        ):
            self.assertEqual(0, launcher.run_directory_launcher(Path("tests"), ["async-shadow-m4", "--dry-run"]))
        run_repo_script.assert_called_once_with(
            Path("tests/ab/async_shadow_m4/launch.py"),
            ["--dry-run"],
            echo=True,
        )

    def test_root_dispatch_preserves_application_argument_separator(self):
        repo_launchers = {
            "tex-conv": launcher.RepoLauncher(
                "tex-conv",
                Path("utilities/tex_conv/launch.py"),
                Path("utilities/launch.py"),
            )
        }
        with (
            mock.patch.object(launcher, "discover_repo_launchers", return_value=repo_launchers),
            mock.patch.object(launcher, "run_repo_script", return_value=0) as run_repo_script,
        ):
            self.assertEqual(0, launcher.main(["tex-conv", "--", "--help"]))
        run_repo_script.assert_called_once_with(
            Path("utilities/launch.py"),
            ["tex-conv", "--", "--help"],
            echo=True,
        )


if __name__ == "__main__":
    unittest.main()
