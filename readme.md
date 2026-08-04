- Targets are x64 only; 32-bit targets are intentionally unsupported.

- Supported build configurations are `dbg`, `opt`, and `fin`.
  - `dbg`: no optimization and no inlining.
  - `opt`: optimized, only primitives are inlined, and frame pointers are available.
  - `fin`: optimized, inlined, frame pointers are omitted, and debugging is intentionally limited.

- CMake is the cross-platform build entry point for this repository.
- LLVM/Clang is the required compiler toolchain on every platform.

- Windows quick start
  - engine-only configure: `cmake --preset windows-clang-engine-x64`
  - engine-only build: `cmake --build --preset windows-clang-engine-dbg --target nwb_resource_cooker`
  - testbed configure: `cmake --preset windows-clang-testbed-x64`
  - testbed build: `cmake --build --preset windows-clang-testbed-dbg --target testbed`
  - full configure/test preset: `cmake --preset windows-clang-x64`
  - Visual Studio can open the repository root as a CMake project; builds use Ninja + `clang`/`clang++`.

- Windows requirements
  - Ninja must be available, or discoverable through `NWB_NINJA` / `NWB_NINJA_ROOT`.
  - Clang/LLVM must be available, or discoverable through `NWB_LLVM_ROOT` / `LLVM_ROOT`.
  - `VULKAN_SDK` must be set.
  - `slangc` must be available on `PATH`, discoverable through `VULKAN_SDK`, or provided with `NWB_SLANGC_EXECUTABLE`.
  - The Windows toolchain uses `clang`/`clang++`, `lld-link`, and Ninja. It targets the Windows/MSVC ABI, so the Windows SDK and the Microsoft C++ runtime/standard library remain part of the environment.

- Linux status
  - Local verification should use the repo-bundled CMake and CTest binaries under `__cmake/tool-venv/bin/` when system `cmake` / `ctest` are not on `PATH`.
  - full configure/test preset: `linux-clang-x64`
  - engine-only configure preset: `linux-clang-engine-x64`
  - testbed configure preset: `linux-clang-testbed-x64`
  - Linux uses the same CMake + Ninja + Clang flow and `dbg` / `opt` / `fin` build configurations as Windows.
  - `slangc` is required when `NWB_BUILD_RESOURCE_COOKER` is enabled.
  - `nwb_frame`, `nwb_loader`, `nwb_logserver`, `nwb_resource_cooker`, and `testbed` are configured through the CMake build options and platform dependencies.
  - Full Linux configure (includes internal validation targets): `cmake --preset linux-clang-x64`.
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
  - Testbed directory launcher: `python launcher.py testbed --config dbg`
  - Generic executable target: `python launcher.py run nwb_resource_cooker -- --help`
  - Launch with profiling/logserver: `python launcher.py run testbed --with-profile`
  - Smoke profile through root dispatch: `python launcher.py smoke transparent-multi --backend hw`
  - Smoke profile with profiling/logserver: `python launcher.py smoke transparent-multi --backend hw --with-profile`
  - Smoke-domain launcher script: `python tests/smoke/launch.py --scene transparent-multi --backend hw`
  - Root commands stay flat, but dispatch through every router in the directory hierarchy: `launcher.py` →
    `CoolStuff/launch.py`, `tests/launch.py`, or `utilities/launch.py` → any intermediate group launcher such as
    `tests/ab/launch.py` → the workflow's terminal `launch.py`. Every directory that groups child launchers must
    provide a router; only terminal launchers become root commands, using their directory name with `_` changed to
    `-`.
  - Source-only and CTest-owned directories under `tests/` are not launcher commands. Add a terminal `launch.py`
    only for an explicit, independently runnable workflow.
  - `python launcher.py profiles` lists all currently discovered commands, so adding a utility or test launcher does
    not require editing the root launcher.
