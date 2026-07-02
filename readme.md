- Targets are x64 only. ARM64 may be considered later; 32-bit targets are intentionally unsupported.

- Supported build configurations are `dbg`, `opt`, and `fin`.
  - `dbg`: no optimization and no inlining.
  - `opt`: optimized, only primitives are inlined, and frame pointers are available.
  - `fin`: optimized, inlined, frame pointers are omitted, and debugging is intentionally limited.

- CMake is the cross-platform build entry point for this repository.
- LLVM/Clang is now the required compiler toolchain on every platform.
- Checked-in MSBuild project metadata is no longer part of the repository workflow.

- Windows quick start
  - engine-only configure: `cmake --preset windows-clang-engine-x64`
  - engine-only build: `cmake --build --preset windows-clang-engine-dbg --target nwb_resource_cooker`
  - testbed configure: `cmake --preset windows-clang-testbed-x64`
  - testbed build: `cmake --build --preset windows-clang-testbed-dbg --target testbed`
  - full configure preset: `cmake --preset windows-clang-x64`
  - Visual Studio can still open the repository root as a CMake project, but the build now goes through Ninja + `clang`/`clang++` instead of MSBuild projects.

- Windows requirements
  - Ninja must be available, or discoverable through `NWB_NINJA` / `NWB_NINJA_ROOT`.
  - Clang/LLVM must be available, or discoverable through `NWB_LLVM_ROOT` / `LLVM_ROOT`.
  - `VULKAN_SDK` must be set.
  - `slangc` must be available on `PATH`, discoverable through `VULKAN_SDK`, or provided with `NWB_SLANGC_EXECUTABLE`.
  - the Windows toolchain now builds with `clang`/`clang++`, `lld-link`, and Ninja. It still targets the Windows/MSVC ABI, so the Windows SDK and the Microsoft C++ runtime/standard library remain part of the environment.

- Linux status
  - Local verification should use the repo-bundled CMake and CTest binaries under `__cmake/tool-venv/bin/` when system `cmake` / `ctest` are not on `PATH`.
  - full configure preset: `linux-clang-x64`
  - engine-only configure preset: `linux-clang-engine-x64`
  - testbed configure preset: `linux-clang-testbed-x64`
  - Linux uses the same CMake + Ninja + Clang flow and `dbg` / `opt` / `fin` build configurations as Windows.
  - `slangc` is required when `NWB_BUILD_RESOURCE_COOKER` is enabled.
  - `nwb_frame`, `nwb_loader`, `nwb_logserver`, `nwb_resource_cooker`, and `testbed` are configured through the CMake build options and platform dependencies.
  - Full Linux configure: `cmake --preset linux-clang-x64`.
  - Debug test verification: `cmake --build --preset linux-clang-dbg`, then `ctest --test-dir __cmake/build/linux-clang-x64 -C dbg --output-on-failure`.
  - Transparent multi capture verification: configure with `cmake --preset linux-clang-x64`, build the executable/assets with `cmake --build --preset linux-clang-dbg --target nwb_transparent_multi_smoke`, then run `ctest --test-dir __cmake/build/linux-clang-x64 -C dbg -R "^nwb_transparent_multi_capture_smoke$" --output-on-failure`.
  - `nwb_transparent_multi_capture_smoke` is a CTest entry, not a Ninja build target. The latest capture is written to `__cmake/build/linux-clang-x64/Testing/smoke/dbg/transparent_multi_capture_latest.png`.
  - Window-capture smoke tests require a usable X11 display server. In headless Linux environments without `DISPLAY` or `Xvfb`, `nwb_testbed_window_capture_smoke` is expected to skip with `XOpenDisplay failed`.
  - fin skinning-culling benchmark verification: configure with `cmake --preset linux-clang-x64`, build `nwb_skinning_culling_benchmark` with `cmake --build --preset linux-clang-fin --target nwb_skinning_culling_benchmark`, then run `ctest --test-dir __cmake/build/linux-clang-x64 -C fin -R nwb_skinning_culling_benchmark --output-on-failure`.
  - The skinning-culling benchmark CTest entry is `fin`-only and is configured only when `tests/smoke/assets/characters/body.nwb` is present.
  - The benchmark CTest is a smoke/regression check. It records GPU timing metrics in the log and allows a small tolerance for near-equal no-culling and culling render times.
  - Project code should request a clean shutdown through `ProjectRuntimeContext::requestQuit`; do not call platform-specific quit APIs such as `PostQuitMessage` from project or smoke-test code.
  - When `requestQuit` is raised during project update, the frame loop exits without submitting another graphics frame.

- Launcher
  - Repo-level launcher: `python launcher.py run testbed --config dbg`
  - Generic executable target: `python launcher.py run nwb_resource_cooker -- --help`
  - Smoke profile: `python launcher.py smoke transparent-multi --backend hw`
  - Legacy smoke entry point remains available at `python tests/smoke/launcher.py --scene transparent-multi --backend hw`.
