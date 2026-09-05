import argparse
import os
import re
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

import launcher
import repository_windows_process


class FakeWindowsProcessApi:
    def __init__(self, image_paths, wait_results):
        self.image_paths = image_paths
        self.wait_results = {pid: list(results) for pid, results in wait_results.items()}
        self.handles = {}
        self.events = []

    def process_ids(self):
        return tuple(self.image_paths)

    def open_process(self, pid):
        handle = object()
        self.handles[pid] = handle
        self.events.append(("open", pid, handle))
        return handle

    def query_process_image_path(self, handle):
        self.events.append(("query", handle))
        return self.image_paths[next(pid for pid, value in self.handles.items() if value is handle)]

    def request_close(self, pid):
        self.events.append(("close", pid))
        return True

    def wait_for_exit(self, handle, timeout_seconds):
        self.events.append(("wait", handle, timeout_seconds))
        pid = next(pid for pid, value in self.handles.items() if value is handle)
        return self.wait_results[pid].pop(0)

    def force_terminate(self, handle):
        self.events.append(("force", handle))
        return True

    def exit_code(self, handle):
        return 0

    def close_process(self, handle):
        self.events.append(("release", handle))


class FakeSpawnedProcess:
    def __init__(self, pid, exit_code, events):
        self.pid = pid
        self.exit_code = exit_code
        self.events = events
        self.returncode = None

    def wait(self, timeout=None):
        self.events.append(("wait", timeout))
        self.returncode = self.exit_code
        return self.exit_code

    def poll(self):
        return self.returncode

    def terminate(self):
        self.events.append(("terminate", self.pid))

    def kill(self):
        self.events.append(("kill", self.pid))


class FakeBoundedProcessApi:
    def __init__(self, wait_results, events, can_terminate=True):
        self.wait_results = list(wait_results)
        self.events = events
        self.handle = object()
        self.can_terminate = can_terminate

    def open_process(self, pid):
        self.events.append(("open", pid, self.handle))
        return self.handle

    def request_close(self, pid):
        self.events.append(("close", pid))
        return True

    def wait_for_exit(self, handle, timeout_seconds):
        self.events.append(("native_wait", handle, timeout_seconds))
        return self.wait_results.pop(0)

    def force_terminate(self, handle):
        self.assert_retained_handle(handle)
        self.events.append(("force", handle))
        return self.can_terminate

    def close_process(self, handle):
        self.events.append(("release", handle))

    def assert_retained_handle(self, handle):
        if handle is not self.handle:
            raise AssertionError("hard termination did not use the retained process handle")


class LauncherPlatformTests(unittest.TestCase):
    def test_host_platform_names_match_cmake_output_names(self):
        self.assertEqual("windows", launcher.host_platform_name("Windows"))
        self.assertEqual("linux", launcher.host_platform_name("Linux"))
        self.assertEqual("darwin", launcher.host_platform_name("Darwin"))

    def test_host_architecture_names_match_cmake_output_names(self):
        self.assertEqual("x64", launcher.host_arch_name("AMD64"))
        self.assertEqual("x64", launcher.host_arch_name("x86_64"))
        self.assertEqual("arm64", launcher.host_arch_name("ARM64"))
        self.assertEqual("arm64", launcher.host_arch_name("aarch64"))

    def test_host_architecture_uses_native_windows_machine_under_emulation(self):
        with (
            mock.patch.object(launcher.platform, "system", return_value="Windows"),
            mock.patch.object(launcher.platform, "machine", return_value="AMD64"),
            mock.patch.object(launcher, "query_windows_native_machine_name", return_value="ARM64"),
            mock.patch.dict(launcher.os.environ, {}, clear=True),
        ):
            self.assertEqual("arm64", launcher.host_arch_name())

    def test_host_architecture_uses_wow64_environment_when_native_query_is_unavailable(self):
        with (
            mock.patch.object(launcher.platform, "system", return_value="Windows"),
            mock.patch.object(launcher.platform, "machine", return_value="AMD64"),
            mock.patch.object(launcher, "query_windows_native_machine_name", return_value=None),
            mock.patch.dict(launcher.os.environ, {"PROCESSOR_ARCHITEW6432": "ARM64"}, clear=True),
        ):
            self.assertEqual("arm64", launcher.host_arch_name())

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
        self.assertEqual(
            root / "__cmake" / "build" / "windows-clang-arm64",
            launcher.default_build_dir(root, "windows", "full", "arm64"),
        )

    def test_default_build_presets_follow_platform_domain_and_config(self):
        self.assertEqual("linux-clang-dbg", launcher.default_build_preset_name("linux", "full", "dbg"))
        self.assertEqual("linux-clang-engine-fin", launcher.default_build_preset_name("linux", "engine", "fin"))
        self.assertEqual("windows-clang-arm64-dbg", launcher.default_build_preset_name("windows", "full", "dbg", "arm64"))
        self.assertEqual(
            "windows-clang-engine-arm64-fin",
            launcher.default_build_preset_name("windows", "engine", "fin", "arm64"),
        )

    def test_explicit_configure_preset_selects_matching_build_directory(self):
        root = Path(os.sep) / "repo"
        resolved_root = root.resolve()
        args = argparse.Namespace(
            repo_root=root,
            platform="linux",
            arch=None,
            domain=None,
            configure_preset="linux-clang-engine-x64",
            build_dir=None,
            config="dbg",
            cmake=None,
        )
        settings = launcher.resolve_launch_settings(args, "full")
        self.assertEqual(resolved_root / "__cmake" / "build" / "linux-clang-engine-x64", settings.build_dir)
        self.assertEqual("engine", settings.domain)
        self.assertEqual("x64", settings.arch)

    def test_explicit_architecture_must_match_configure_preset(self):
        args = argparse.Namespace(
            repo_root=Path(os.sep) / "repo",
            platform="windows",
            arch="arm64",
            domain=None,
            configure_preset="windows-clang-x64",
            build_dir=None,
            config="dbg",
            cmake=None,
        )
        with self.assertRaisesRegex(SystemExit, "conflicts with configure preset"):
            launcher.resolve_launch_settings(args, "full")

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

    def test_windows_kill_existing_matches_exact_image_not_same_basename(self):
        x64_image = r"C:\Build\x64\viewer.exe"
        arm64_image = r"C:\Build\arm64\viewer.exe"
        api = FakeWindowsProcessApi(
            {
                101: x64_image,
                202: r"c:\build\ARM64\viewer.exe",
            },
            {202: [True]},
        )

        results = repository_windows_process.stop_processes_by_image_path(
            arm64_image,
            3.0,
            2.0,
            api=api,
            resolve_path=lambda path: path,
            current_process_id=999,
        )

        self.assertEqual((202,), tuple(result.pid for result in results))
        self.assertIn(("close", 202), api.events)
        self.assertNotIn(("close", 101), api.events)
        self.assertFalse(any(event[0] == "force" for event in api.events))

    def test_windows_existing_forced_shutdown_uses_retained_process_handle(self):
        api = FakeWindowsProcessApi(
            {202: r"C:\build\arm64\viewer.exe"},
            {202: [False, True]},
        )

        results = repository_windows_process.stop_processes_by_image_path(
            r"C:\build\arm64\viewer.exe",
            3.0,
            2.0,
            api=api,
            resolve_path=lambda path: path,
            current_process_id=999,
        )

        retained_handle = api.handles[202]
        self.assertTrue(results[0].forced)
        self.assertIn(("force", retained_handle), api.events)

    def test_windows_exact_image_query_does_not_require_termination_access(self):
        self.assertEqual(
            0,
            repository_windows_process.PROCESS_QUERY_AND_WAIT_ACCESS & repository_windows_process.PROCESS_TERMINATE,
        )

    def test_windows_query_only_handle_can_still_complete_gracefully(self):
        events = []
        process = FakeSpawnedProcess(302, 0, events)

        result = repository_windows_process.run_bounded_process(
            process,
            8.0,
            4.0,
            2.0,
            api=FakeBoundedProcessApi([False, True], events, can_terminate=False),
        )

        self.assertEqual(0, result.exit_code)
        self.assertFalse(result.forced)
        self.assertFalse(any(event[0] == "force" for event in events))

    def test_windows_query_only_handle_reports_live_process_at_hard_boundary(self):
        events = []
        process = FakeSpawnedProcess(306, 0, events)

        with self.assertRaisesRegex(repository_windows_process.WindowsProcessError, "did not grant terminate access"):
            repository_windows_process.run_bounded_process(
                process,
                8.0,
                4.0,
                2.0,
                api=FakeBoundedProcessApi([False, False, False], events, can_terminate=False),
            )

        retained_handle = next(event[2] for event in events if event[0] == "open")
        self.assertIn(("force", retained_handle), events)
        self.assertEqual(1, sum(event[0] == "open" for event in events))

    def test_windows_bounded_shutdown_exits_gracefully_without_hard_termination(self):
        events = []
        process = FakeSpawnedProcess(303, 0, events)

        result = repository_windows_process.run_bounded_process(
            process,
            8.0,
            4.0,
            2.0,
            api=FakeBoundedProcessApi([False, True], events),
        )

        self.assertEqual(0, result.exit_code)
        self.assertFalse(result.forced)
        self.assertEqual(
            [
                ("open", 303, mock.ANY),
                ("native_wait", mock.ANY, 8.0),
                ("close", 303),
                ("native_wait", mock.ANY, 4.0),
                ("wait", None),
                ("release", mock.ANY),
            ],
            events,
        )

    def test_windows_bounded_shutdown_propagates_nonzero_graceful_exit(self):
        events = []
        process = FakeSpawnedProcess(304, 23, events)

        result = repository_windows_process.run_bounded_process(
            process,
            8.0,
            4.0,
            2.0,
            api=FakeBoundedProcessApi([False, True], events),
        )

        self.assertEqual(23, result.exit_code)
        self.assertFalse(result.forced)
        self.assertNotIn(("force", 304), events)
        self.assertNotIn(("terminate", 304), events)
        self.assertNotIn(("kill", 304), events)

    def test_windows_bounded_shutdown_forces_exact_process_only_after_grace_timeout(self):
        events = []
        process = FakeSpawnedProcess(404, 7, events)

        result = repository_windows_process.run_bounded_process(
            process,
            9.0,
            5.0,
            2.0,
            api=FakeBoundedProcessApi([False, False, True], events),
        )

        self.assertEqual(7, result.exit_code)
        self.assertTrue(result.forced)
        self.assertEqual(
            [
                ("open", 404, mock.ANY),
                ("native_wait", mock.ANY, 9.0),
                ("close", 404),
                ("native_wait", mock.ANY, 5.0),
                ("force", mock.ANY),
                ("native_wait", mock.ANY, 2.0),
                ("wait", None),
                ("release", mock.ANY),
            ],
            events,
        )

    def test_windows_bounded_shutdown_rejects_success_status_after_forced_termination(self):
        events = []
        process = FakeSpawnedProcess(405, 0, events)

        with self.assertRaisesRegex(repository_windows_process.WindowsProcessError, "forced process .* successful"):
            repository_windows_process.run_bounded_process(
                process,
                9.0,
                5.0,
                2.0,
                api=FakeBoundedProcessApi([False, False, True], events),
            )

        grace_wait_index = next(index for index, event in enumerate(events) if event[0] == "native_wait" and event[2] == 5.0)
        force_index = next(index for index, event in enumerate(events) if event[0] == "force")
        self.assertLess(grace_wait_index, force_index)

    def test_launcher_propagates_bounded_windows_process_exit_status(self):
        events = []
        process = FakeSpawnedProcess(406, 23, events)
        args = argparse.Namespace(
            kill_existing=False,
            dry_run=False,
            gpudbg=False,
            detach=False,
            run_seconds=9.0,
        )
        run_result = repository_windows_process.WindowsBoundedRunResult(406, 23, True, True, False)

        with (
            mock.patch.object(launcher.subprocess, "Popen", return_value=process),
            mock.patch.object(launcher, "host_platform_name", return_value="windows"),
            mock.patch.object(repository_windows_process, "run_bounded_process", return_value=run_result) as bounded,
            mock.patch.object(launcher, "terminate_process") as generic_terminate,
        ):
            exit_code = launcher.launch_process(
                args,
                Path(r"C:\build\arm64\viewer.exe"),
                Path(r"C:\build\arm64"),
                {},
                (),
                paths_validated=True,
            )

        self.assertEqual(23, exit_code)
        bounded.assert_called_once_with(
            process,
            9.0,
            launcher.APPLICATION_GRACEFUL_STOP_TIMEOUT_SECONDS,
            launcher.APPLICATION_FORCED_STOP_TIMEOUT_SECONDS,
        )
        generic_terminate.assert_not_called()

    def test_launcher_help_describes_exact_image_and_graceful_bounded_shutdown(self):
        parser = launcher.make_parser()
        run_parser = next(action for action in parser._actions if action.dest == "command").choices["run"]
        help_by_destination = {action.dest: action.help for action in run_parser._actions}
        self.assertIn("exact executable image path", help_by_destination["kill_existing"])
        self.assertIn("gracefully close", help_by_destination["run_seconds"])
        self.assertIn("forced fallback", help_by_destination["run_seconds"])

    def test_discovers_leaf_launchers(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            paths = (
                root / "CoolStuff" / "launch.py",
                root / "CoolStuff" / "Testbed" / "launch.py",
                root / "tests" / "launch.py",
                root / "tests" / "smoke" / "launch.py",
                root / "tests" / "smoke" / "launcher.py",
                root / "tests" / "ab" / "launch.py",
                root / "tests" / "ab" / "async_shadow_m4" / "launch.py",
                root / "tests" / "ab" / "command_ir" / "launch.py",
                root / "tests" / "ab" / "frame_lagged_async_lighting" / "launch.py",
                root / "tests" / "ab" / "frame_lagged_async_lighting" / "run.py",
                root / "tests" / "ab" / "frame_lagged_async_lighting" / "helper.py",
                root / "tests" / "ab" / "hybrid_shadow_boundary" / "launch.py",
                root / "tests" / "ab" / "transfer_queue" / "launch.py",
                root / "utilities" / "launch.py",
                root / "utilities" / "tex_conv" / "launch.py",
            )
            for path in paths:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("", encoding="utf-8")

            launchers = launcher.discover_repo_launchers(root)

        self.assertEqual(
            {
                "async-shadow-m4": Path("tests/ab/async_shadow_m4/launch.py"),
                "command-ir": Path("tests/ab/command_ir/launch.py"),
                "frame-lagged-async-lighting": Path("tests/ab/frame_lagged_async_lighting/launch.py"),
                "hybrid-shadow-boundary": Path("tests/ab/hybrid_shadow_boundary/launch.py"),
                "transfer-queue": Path("tests/ab/transfer_queue/launch.py"),
                "smoke": Path("tests/smoke/launch.py"),
                "testbed": Path("CoolStuff/Testbed/launch.py"),
                "tex-conv": Path("utilities/tex_conv/launch.py"),
            },
            {command: discovered.script for command, discovered in launchers.items()},
        )
        self.assertEqual(
            {
                "async-shadow-m4": (Path("tests/launch.py"), Path("tests/ab/launch.py")),
                "command-ir": (Path("tests/launch.py"), Path("tests/ab/launch.py")),
                "frame-lagged-async-lighting": (Path("tests/launch.py"), Path("tests/ab/launch.py")),
                "hybrid-shadow-boundary": (Path("tests/launch.py"), Path("tests/ab/launch.py")),
                "transfer-queue": (Path("tests/launch.py"), Path("tests/ab/launch.py")),
                "smoke": (Path("tests/launch.py"),),
                "testbed": (Path("CoolStuff/launch.py"),),
                "tex-conv": (Path("utilities/launch.py"),),
            },
            {command: discovered.route for command, discovered in launchers.items()},
        )
        self.assertFalse({"ab", "coolstuff", "tests", "utilities"}.intersection(launchers))

    def test_ignores_nonstandard_leaf_script_names(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            category = root / "tests"
            category.mkdir(parents=True)
            (category / "launch.py").write_text("", encoding="utf-8")
            directory = root / "tests" / "smoke"
            directory.mkdir(parents=True)
            (directory / "launcher.py").write_text("", encoding="utf-8")
            (directory / "run.py").write_text("", encoding="utf-8")

            launchers = launcher.discover_repo_launchers(root)

        self.assertEqual({}, launchers)

    def test_directory_discovery_only_returns_direct_children(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            paths = (
                root / "tests" / "launch.py",
                root / "tests" / "ab" / "launch.py",
                root / "tests" / "ab" / "async_shadow_m4" / "launch.py",
                root / "tests" / "ab" / "command_ir" / "launch.py",
                root / "tests" / "ab" / "frame_lagged_async_lighting" / "launch.py",
                root / "tests" / "ab" / "hybrid_shadow_boundary" / "launch.py",
                root / "tests" / "ab" / "transfer_queue" / "launch.py",
                root / "tests" / "smoke" / "launch.py",
            )
            for path in paths:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("", encoding="utf-8")

            tests_launchers = launcher.discover_directory_launchers(Path("tests"), root)
            ab_launchers = launcher.discover_directory_launchers(Path("tests") / "ab", root)

        self.assertEqual(
            {
                "ab": Path("tests/ab/launch.py"),
                "smoke": Path("tests/smoke/launch.py"),
            },
            {command: discovered.script for command, discovered in tests_launchers.items()},
        )
        self.assertNotIn("async-shadow-m4", tests_launchers)
        self.assertEqual(
            {
                "async-shadow-m4": Path("tests/ab/async_shadow_m4/launch.py"),
                "command-ir": Path("tests/ab/command_ir/launch.py"),
                "frame-lagged-async-lighting": Path("tests/ab/frame_lagged_async_lighting/launch.py"),
                "hybrid-shadow-boundary": Path("tests/ab/hybrid_shadow_boundary/launch.py"),
                "transfer-queue": Path("tests/ab/transfer_queue/launch.py"),
            },
            {command: discovered.script for command, discovered in ab_launchers.items()},
        )

    def test_duplicate_launch_commands_are_rejected(self):
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

            with self.assertRaisesRegex(
                SystemExit,
                r"\A" + re.escape(f"missing category launcher: {Path('utilities') / 'launch.py'}") + r"\Z",
            ):
                launcher.discover_repo_launchers(root)

    def test_nested_leaf_requires_an_intermediate_router(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            for path in (
                root / "tests" / "launch.py",
                root / "tests" / "ab" / "async_shadow_m4" / "launch.py",
            ):
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("", encoding="utf-8")

            with self.assertRaisesRegex(
                SystemExit,
                r"\A" + re.escape(f"missing directory launcher: {Path('tests') / 'ab' / 'launch.py'}") + r"\Z",
            ):
                launcher.discover_repo_launchers(root)

    def test_discovered_launcher_forwards_its_arguments(self):
        repo_launchers = {
            "async-shadow-m4": launcher.RepoLauncher(
                "async-shadow-m4",
                Path("tests/ab/async_shadow_m4/launch.py"),
                (Path("tests/launch.py"), Path("tests/ab/launch.py")),
            )
        }
        with (
            mock.patch.object(launcher, "discover_repo_launchers", return_value=repo_launchers),
            mock.patch.object(launcher, "run_repo_script", return_value=0) as run_repo_script,
        ):
            self.assertEqual(0, launcher.main(["async-shadow-m4", "--dry-run"]))
        run_repo_script.assert_called_once_with(
            Path("tests/launch.py"),
            ["ab", "async-shadow-m4", "--dry-run"],
            echo=True,
        )

    def test_category_launcher_forwards_to_its_child_router(self):
        repo_launchers = {
            "ab": launcher.RepoLauncher(
                "ab",
                Path("tests/ab/launch.py"),
            )
        }
        with (
            mock.patch.object(launcher, "discover_directory_launchers", return_value=repo_launchers),
            mock.patch.object(launcher, "run_repo_script", return_value=0) as run_repo_script,
        ):
            self.assertEqual(0, launcher.run_directory_launcher(Path("tests"), ["ab", "async-shadow-m4", "--dry-run"]))
        run_repo_script.assert_called_once_with(
            Path("tests/ab/launch.py"),
            ["async-shadow-m4", "--dry-run"],
            echo=True,
        )

    def test_intermediate_router_forwards_to_its_leaf(self):
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
            self.assertEqual(
                0,
                launcher.run_directory_launcher(Path("tests") / "ab", ["async-shadow-m4", "--dry-run"]),
            )
        run_repo_script.assert_called_once_with(
            Path("tests/ab/async_shadow_m4/launch.py"),
            ["--dry-run"],
            echo=True,
        )

    def test_root_dispatch_preserves_application_argument_separator(self):
        repo_launchers = {
            "async-shadow-m4": launcher.RepoLauncher(
                "async-shadow-m4",
                Path("tests/ab/async_shadow_m4/launch.py"),
                (Path("tests/launch.py"), Path("tests/ab/launch.py")),
            )
        }
        with (
            mock.patch.object(launcher, "discover_repo_launchers", return_value=repo_launchers),
            mock.patch.object(launcher, "run_repo_script", return_value=0) as run_repo_script,
        ):
            self.assertEqual(0, launcher.main(["async-shadow-m4", "--", "--measure-seconds", "30"]))
        run_repo_script.assert_called_once_with(
            Path("tests/launch.py"),
            ["ab", "async-shadow-m4", "--", "--measure-seconds", "30"],
            echo=True,
        )


if __name__ == "__main__":
    unittest.main()
