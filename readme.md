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
  - `slangc` must be available on `PATH`, discoverable through `VULKAN_SDK`, or provided with `NWB_SLANGC_EXECUTABLE`.
  - the Windows toolchain now builds with `clang`/`clang++`, `lld-link`, and Ninja. It still targets the Windows/MSVC ABI, so the Windows SDK and the Microsoft C++ runtime/standard library remain part of the environment.

- Linux status
  - full configure preset: `linux-clang-x64`
  - engine-only configure preset: `linux-clang-engine-x64`
  - testbed configure preset: `linux-clang-testbed-x64`
  - Linux uses the same CMake + Ninja + Clang flow and `dbg` / `opt` / `fin` build configurations as Windows.
  - `slangc` is required when `NWB_BUILD_RESOURCE_COOKER` is enabled.
  - `nwb_frame`, `nwb_loader`, `nwb_logserver`, `nwb_resource_cooker`, and `testbed` are configured through the CMake build options and platform dependencies.
  - fin skinned-cone benchmark verification: build `nwb_skinned_cone_benchmark` with `cmake --build --preset linux-clang-fin --target nwb_skinned_cone_benchmark`, then run `ctest --test-dir __cmake/build/linux-clang-x64 -C fin -R nwb_skinned_cone_culling_benchmark --output-on-failure`.
  - Project code should request a clean shutdown through `ProjectRuntimeContext::requestQuit`; the frame loop stops before submitting another graphics frame after that request.
