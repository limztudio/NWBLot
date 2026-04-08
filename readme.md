- target always be x64 (in the future I should consider ARM64 but I would never consider 32bit)

- config now would be dbg, opt and fin
  - dbg: not optimized. no inline.
  - opt: optimized. only primitives are inlined. frame pointer available.
  - fin: optimized. inlined. frame pointer is omitted. hardly debuggable.

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
  - the current Windows build expects `volk.lib`, `shaderc_combined.lib`, and `shaderc_combinedd.lib` from the Vulkan SDK.
  - the Windows toolchain now builds with `clang`/`clang++`, `lld-link`, and Ninja. It still targets the Windows/MSVC ABI, so the Windows SDK and the Microsoft C++ runtime/standard library remain part of the environment.

- Linux status
  - engine preset: `linux-clang-engine-x64`
  - migration is still in progress.
  - `nwb_logserver` is available on Linux in console mode.
  - Windows-only targets are intentionally disabled on non-Windows for now: `nwb_frame`, `nwb_loader`, `testbed`.
