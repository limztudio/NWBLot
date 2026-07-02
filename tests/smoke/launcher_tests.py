#!/usr/bin/env python3
import os
import argparse
from pathlib import Path
import sys
import tempfile
import unittest

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


if __name__ == "__main__":
    unittest.main()
