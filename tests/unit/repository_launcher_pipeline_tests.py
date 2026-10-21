"""Repository launcher dry-run domains, executable metadata, and pipeline launch behavior."""


import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

import launcher  # noqa: E402


LIT_RESOLVE_EXECUTABLE_PATH = "resolve_executable_path"
LIT_CMAKE_COMMAND = "cmake_command"
LIT_BUILD_ENVIRONMENT = "build_environment"
LIT_ENSURE_FILE_API_QUERY = "ensure_file_api_query"
LIT_RETURN_VALUE = "return_value"
LIT_SIDE_EFFECT = "side_effect"
LIT_CMAKE_FILE_API_METADATA_IS_REQUIRED = "CMake File API metadata is required"
LIT_RELEASE = "release"
LIT_WINDOWS = "windows"
LIT_ARM64 = "arm64"
LIT_CMAKE = "__cmake"
LIT_CMAKE_EXECUTABLE = "cmake"
LIT_BUILD = "build"
LIT_FULL = "full"
LIT_WINDOWS_CLANG_ARM64 = "windows-clang-arm64"
LIT_DBG = "dbg"
LIT_TESTBED = "testbed"
LIT_NWB_ASSET_BUILDER = "nwb_asset_builder"
LIT_CMAKECACHE_TXT = "CMakeCache.txt"
LIT_UTF_8 = "utf-8"
LIT_RUN = "run"
LIT_POPEN = "Popen"
LIT_HOST_PLATFORM_NAME = "host_platform_name"
LIT_PIPELINE = "pipeline"
LIT_LAUNCH_PY = "launch.py"
LIT_DRY_RUN = "--dry-run"
LIT_CONFIG = "--config"
LIT_ASSET_ROOT = "--asset-root"
LIT_OUTPUT_DIRECTORY = "--output-directory"
LIT_RUNTIME_RESOURCES = "runtime resources"
LIT_OPT = "opt"
LIT_REPO_ROOT = "--repo-root"
LIT_CACHE_DIRECTORY = "--cache-directory"
LIT_CUSTOM_ARTIFACTS = "custom artifacts"
LIT_DEPENDENCY_COMPUTER_EXE = "dependency_computer.exe"
LIT_ASSET_BUILDER_EXE = "asset_builder.exe"
LIT_ASSET_GATHERER_EXE = "asset_gatherer.exe"
LIT_BUILTINS_PRINT = "builtins.print"
LIT_MAIN = "__main__"
LIT_BUILD_DIR = "--build-dir"
LIT_CONFIGURE_PRESET = "--configure-preset"
LIT_ARCH = "--arch"
LIT_PLATFORM = "--platform"
LIT_DOMAIN = "--domain"
LIT_COLD_CUSTOM_BUILD = "cold custom build"
LIT_HELP = "--help"
LIT_FORBIDDEN_LAUNCH = "A build operation entered a launch path"
LIT_ENGINE = "engine"
LIT_WINDOWS_CLANG_ENGINE_ARM64 = "windows-clang-engine-arm64"
LIT_WINDOWS_CLANG_TESTBED_ARM64 = "windows-clang-testbed-arm64"
LIT_EXEC_OUTPUT_ROOT = "__exec"
LIT_METADATA_INDEX = "index-0001.json"
LIT_METADATA_CODEMODEL = "codemodel.json"
LIT_METADATA_TARGET = "target.json"
LIT_METADATA_LIBRARY = "STATIC_LIBRARY"
LIT_ACTUAL_EXECUTABLE = "actual-target.exe"
LIT_PREVIEW_EXECUTABLE = "preview-target"
LIT_EXPLICIT_EXECUTABLE = "explicit-target.exe"


class LauncherDryRunDomainTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.custom = self.root / LIT_COLD_CUSTOM_BUILD
        self.arguments = [LIT_RUN, LIT_TESTBED, LIT_REPO_ROOT, str(self.root), LIT_PLATFORM, LIT_WINDOWS, LIT_ARCH, LIT_ARM64, LIT_CONFIG, LIT_OPT, LIT_BUILD_DIR, str(self.custom), LIT_DRY_RUN,]
        output_patch = mock.patch(LIT_BUILTINS_PRINT)
        self.output = output_patch.start()
        self.addCleanup(output_patch.stop)
        for owner, operation, options in ((launcher, LIT_CMAKE_COMMAND, {LIT_RETURN_VALUE: (LIT_CMAKE_EXECUTABLE,)}), (launcher, LIT_HOST_PLATFORM_NAME, {LIT_RETURN_VALUE: LIT_WINDOWS}), (launcher, "host_arch_name", {LIT_RETURN_VALUE: LIT_ARM64}),
            (launcher, LIT_ENSURE_FILE_API_QUERY, {"wraps": launcher.ensure_file_api_query}), (subprocess, LIT_RUN, {LIT_SIDE_EFFECT: AssertionError(LIT_FORBIDDEN_LAUNCH)}), (subprocess, LIT_POPEN, {LIT_SIDE_EFFECT: AssertionError(LIT_FORBIDDEN_LAUNCH)}),):
            patcher = mock.patch.object(owner, operation, **options)
            result = patcher.start()
            self.addCleanup(patcher.stop)
            if LIT_RETURN_VALUE not in options:
                self.addCleanup(result.assert_not_called)

    def assert_preview_uses_domain(self, expected_domain, extra_arguments):
        self.output.reset_mock()
        args = launcher.make_parser({}).parse_args([*self.arguments, *extra_arguments])
        args.application_args = []
        before = tuple((path, path.read_bytes() if path.is_file() else None) for path in sorted(self.root.rglob("*")))
        settings = launcher.resolve_launch_settings(args, LIT_FULL)
        self.assertEqual(expected_domain, settings.domain)
        self.assertEqual(expected_domain, launcher.refresh_launch_settings(settings, args.domain).domain)

        self.assertEqual(0, launcher.run_target_command(args))

        output = self.root / LIT_EXEC_OUTPUT_ROOT / LIT_WINDOWS / LIT_ARM64
        if expected_domain != LIT_ENGINE:
            output /= expected_domain
        executable = output / LIT_OPT / "testbed.exe"
        printed = [str(call.args[0]) for call in self.output.call_args_list]
        self.assertIn("+ " + launcher.format_command([str(executable)]), printed)
        self.assertIn("  cwd: " + str(output), printed)
        after = tuple((path, path.read_bytes() if path.is_file() else None) for path in sorted(self.root.rglob("*")))
        self.assertEqual(before, after)

    def test_cold_custom_directory_preview_uses_selected_preset_instead_of_directory_name(self):
        cases = ((LIT_FULL, []), (LIT_FULL, [LIT_CONFIGURE_PRESET, LIT_WINDOWS_CLANG_ARM64]), (LIT_ENGINE, [LIT_CONFIGURE_PRESET, LIT_WINDOWS_CLANG_ENGINE_ARM64]), (LIT_TESTBED, [LIT_CONFIGURE_PRESET, LIT_WINDOWS_CLANG_TESTBED_ARM64]),)
        for expected_domain, arguments in cases:
            with self.subTest(domain=expected_domain, arguments=arguments):
                self.assert_preview_uses_domain(expected_domain, arguments)
                self.assertFalse(self.custom.exists())

    def test_cache_and_explicit_domain_override_the_custom_directory_preset_fallback(self):
        self.custom.mkdir()
        (self.custom / LIT_CMAKECACHE_TXT).write_text("NWB_OUTPUT_DOMAIN:STRING=testbed\n", encoding=LIT_UTF_8)
        cases = ((LIT_TESTBED, [LIT_CONFIGURE_PRESET, LIT_WINDOWS_CLANG_ARM64]), (LIT_ENGINE, [LIT_CONFIGURE_PRESET, LIT_WINDOWS_CLANG_ARM64, LIT_DOMAIN, LIT_ENGINE]),)
        for expected_domain, arguments in cases:
            with self.subTest(domain=expected_domain):
                self.assert_preview_uses_domain(expected_domain, arguments)


class LauncherExecutableMetadataTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.settings = launcher.LaunchSettings(root=self.root, platform_name=LIT_WINDOWS, arch=LIT_ARM64, domain=LIT_FULL, config=LIT_OPT, configure_preset=LIT_WINDOWS_CLANG_ARM64, build_dir=self.root / LIT_CMAKE / LIT_BUILD, cmake=(LIT_CMAKE_EXECUTABLE,),)
        self.reply = self.settings.build_dir / launcher.FILE_API_DIR_CMAKE / launcher.FILE_API_DIR_API / launcher.FILE_API_DIR_V1 / launcher.FILE_API_DIR_REPLY
        self.reply.mkdir(parents=True)
        self.actual = self.settings.build_dir / LIT_ACTUAL_EXECUTABLE

    def write_target_metadata(self, target_type, artifacts):
        documents = { LIT_METADATA_INDEX: { launcher.FILE_API_OBJECTS_KEY: [{ launcher.FILE_API_KIND_KEY: launcher.FILE_API_KIND_CODEMODEL, launcher.FILE_API_VERSION_KEY: {launcher.FILE_API_VERSION_MAJOR: launcher.FILE_API_CODEMODEL_VERSION},
                    launcher.FILE_API_JSON_FILE_KEY: LIT_METADATA_CODEMODEL, }], }, LIT_METADATA_CODEMODEL: { launcher.FILE_API_CONFIGURATIONS_KEY: [{ launcher.FILE_API_NAME_KEY: self.settings.config, launcher.FILE_API_TARGETS_KEY: [{
                        launcher.FILE_API_NAME_KEY: LIT_TESTBED, launcher.FILE_API_JSON_FILE_KEY: LIT_METADATA_TARGET, }], }], }, LIT_METADATA_TARGET: { launcher.FILE_API_NAME_KEY: LIT_TESTBED, launcher.FILE_API_TYPE_KEY: target_type,
                launcher.FILE_API_ARTIFACTS_KEY: [{launcher.FILE_API_PATH_KEY: str(path)} for path in artifacts], }, }
        for name, document in documents.items():
            (self.reply / name).write_text(json.dumps(document), encoding=LIT_UTF_8)

    def test_missing_metadata_rejects_guess_but_keeps_explicit_and_preview_paths(self):
        predicted = launcher.resolve_executable_path(self.settings, LIT_TESTBED, None, LIT_PREVIEW_EXECUTABLE, True)
        predicted.parent.mkdir(parents=True)
        predicted.touch()
        with self.assertRaisesRegex(SystemExit, LIT_CMAKE_FILE_API_METADATA_IS_REQUIRED):
            launcher.resolve_executable_path(self.settings, LIT_TESTBED, None, LIT_PREVIEW_EXECUTABLE, False)
        explicit = Path(LIT_EXPLICIT_EXECUTABLE)
        self.assertEqual(self.root / explicit, launcher.resolve_executable_path(self.settings, LIT_TESTBED, explicit, None, False))
        self.assertEqual(predicted, launcher.resolve_executable_path(self.settings, LIT_TESTBED, None, LIT_PREVIEW_EXECUTABLE, True))

        self.write_target_metadata(launcher.FILE_API_TARGET_EXECUTABLE, [self.actual])
        self.assertEqual(self.actual, launcher.resolve_executable_path(self.settings, LIT_TESTBED, None, LIT_PREVIEW_EXECUTABLE, False))

    def test_unmatched_single_configuration_cannot_supply_the_selected_executable(self):
        codemodel_path = self.reply / LIT_METADATA_CODEMODEL
        for other_config in (LIT_DBG, LIT_RELEASE, launcher.EMPTY_STRING):
            with self.subTest(configuration=other_config):
                self.write_target_metadata(launcher.FILE_API_TARGET_EXECUTABLE, [self.actual])
                codemodel = json.loads(codemodel_path.read_text(encoding=LIT_UTF_8))
                codemodel[launcher.FILE_API_CONFIGURATIONS_KEY][0][launcher.FILE_API_NAME_KEY] = other_config
                codemodel_path.write_text(json.dumps(codemodel), encoding=LIT_UTF_8)
                with self.assertRaisesRegex(SystemExit, LIT_CMAKE_FILE_API_METADATA_IS_REQUIRED):
                    launcher.resolve_executable_path(self.settings, LIT_TESTBED, None, LIT_PREVIEW_EXECUTABLE, False)

        self.write_target_metadata(launcher.FILE_API_TARGET_EXECUTABLE, [self.actual])
        self.assertEqual(self.actual, launcher.resolve_executable_path(self.settings, LIT_TESTBED, None, None, False))

    def test_executable_metadata_without_artifact_or_with_library_type_is_rejected(self):
        self.write_target_metadata(launcher.FILE_API_TARGET_EXECUTABLE, [])
        with self.assertRaisesRegex(SystemExit, "has no executable artifact"):
            launcher.resolve_executable_path(self.settings, LIT_TESTBED, None, None, False)
        self.write_target_metadata(LIT_METADATA_LIBRARY, [self.actual])
        with self.assertRaisesRegex(SystemExit, "CMake target is not executable"):
            launcher.resolve_executable_path(self.settings, LIT_TESTBED, None, None, False)
        self.write_target_metadata(launcher.FILE_API_TARGET_EXECUTABLE, [Path(LIT_ACTUAL_EXECUTABLE)])
        self.assertEqual(self.actual, launcher.resolve_executable_path(self.settings, LIT_TESTBED, None, None, False))


class PipelineLauncherTests(unittest.TestCase):
    def setUp(self):
        specification = importlib.util.spec_from_file_location("nwb_test_pipeline_launcher", ROOT / LIT_PIPELINE / LIT_LAUNCH_PY)
        self.assertIsNotNone(specification)
        self.assertIsNotNone(specification.loader)
        self.pipeline = importlib.util.module_from_spec(specification)
        specification.loader.exec_module(self.pipeline)
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.settings = launcher.LaunchSettings(root=self.root, platform_name=LIT_WINDOWS, arch=LIT_ARM64, domain=LIT_FULL, config=LIT_OPT, configure_preset=LIT_WINDOWS_CLANG_ARM64, build_dir=self.root / LIT_CMAKE / LIT_BUILD / LIT_WINDOWS_CLANG_ARM64,
            cmake=(LIT_CMAKE_EXECUTABLE,),)
        self.environment = {"NWB_TEST_ENVIRONMENT": LIT_PIPELINE}
        self.asset_roots = [self.root / "first assets", self.root / "second assets"]
        for index, asset_root in enumerate(self.asset_roots):
            asset_root.mkdir()
            (asset_root / f"asset-{index}.nwb").write_text("", encoding=LIT_UTF_8)
        self.cache = self.root / "asset cache"
        self.output = self.root / LIT_RUNTIME_RESOURCES
        self.arguments = [LIT_REPO_ROOT, str(self.root), LIT_PLATFORM, LIT_WINDOWS, LIT_ARCH, LIT_ARM64, LIT_CONFIG, LIT_OPT, LIT_ASSET_ROOT, *(str(path) for path in self.asset_roots),
            LIT_OUTPUT_DIRECTORY, str(self.output), LIT_CACHE_DIRECTORY, str(self.cache),]
        self.tool_paths = { "nwb_dependency_computer": self.root / LIT_CUSTOM_ARTIFACTS / LIT_DEPENDENCY_COMPUTER_EXE, LIT_NWB_ASSET_BUILDER: self.root / LIT_CUSTOM_ARTIFACTS / LIT_ASSET_BUILDER_EXE,
            "nwb_asset_gatherer": self.root / LIT_CUSTOM_ARTIFACTS / LIT_ASSET_GATHERER_EXE, }
        for path in self.tool_paths.values():
            path.parent.mkdir(exist_ok=True)
            path.touch()
        self.settings_resolver = self.patch(launcher, "resolve_launch_settings", return_value=self.settings)
        self.refresh = self.patch(launcher, "refresh_launch_settings", return_value=self.settings)
        self.patch(launcher, LIT_BUILD_ENVIRONMENT, return_value=self.environment)
        self.patch(launcher, LIT_HOST_PLATFORM_NAME, return_value=LIT_WINDOWS)
        self.process = self.patch(subprocess, LIT_RUN, return_value=mock.Mock(returncode=0))
        self.stage = self.patch(self.pipeline, "run_stage")

    def patch(self, owner, name, **kwargs):
        patcher = mock.patch.object(owner, name, **kwargs)
        result = patcher.start()
        self.addCleanup(patcher.stop)
        return result

    def prepare_pipeline_build(self):
        configure = self.patch(launcher, "maybe_configure")
        build = self.patch(launcher, "build_target")
        resolve = self.patch(launcher, LIT_RESOLVE_EXECUTABLE_PATH, side_effect=lambda settings, target, override, base_name, dry_run: override or self.tool_paths[target],)
        return configure, build, resolve


    def test_build_failure_propagates_without_resolving_or_launching_tools(self):
        _, build, resolve = self.prepare_pipeline_build()
        build.side_effect = SystemExit(21)

        with self.assertRaises(SystemExit) as failure:
            self.pipeline.main(self.arguments)

        self.assertEqual(21, failure.exception.code)
        resolve.assert_not_called()
        self.stage.assert_not_called()
        self.process.assert_not_called()
        self.assertFalse(self.cache.exists())


    def test_pipeline_dry_run_has_no_subprocess_or_output_side_effects(self):
        with mock.patch(LIT_BUILTINS_PRINT), mock.patch.object(subprocess, LIT_POPEN) as popen:
            self.assertEqual(0, self.pipeline.main([*self.arguments, LIT_DRY_RUN]))

        self.process.assert_not_called()
        self.stage.assert_not_called()
        popen.assert_not_called()
        self.assertFalse(self.settings.build_dir.exists())
        self.assertFalse(self.cache.exists())
        self.assertFalse(self.output.exists())

    def test_help_skips_configuration_build_and_target_resolution(self):
        configure, build, resolve = self.prepare_pipeline_build()
        with mock.patch(LIT_BUILTINS_PRINT), self.assertRaises(SystemExit) as result:
            self.pipeline.main([LIT_HELP])

        self.assertEqual(0, result.exception.code)
        self.settings_resolver.assert_not_called()
        configure.assert_not_called()
        build.assert_not_called()
        resolve.assert_not_called()
        self.stage.assert_not_called()
        self.process.assert_not_called()

    def test_stage_failure_reports_failure_and_prevents_later_stages(self):
        self.prepare_pipeline_build()
        for failed_index, executable in enumerate(self.tool_paths.values()):
            with self.subTest(stage=executable.name):
                self.stage.reset_mock()
                self.stage.side_effect = [None] * failed_index + [subprocess.CalledProcessError(23, [str(executable)])]
                with mock.patch(LIT_BUILTINS_PRINT):
                    self.assertEqual(1, self.pipeline.main(self.arguments))
                self.assertEqual(failed_index + 1, self.stage.call_count)


if __name__ == LIT_MAIN:
    unittest.main()
