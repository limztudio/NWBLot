# NWBLot

NWBLot is a C++ engine with a Vulkan graphics backend, an asset cooker, runtime loader, ECS renderer, developer tools, automated tests, and a runnable Testbed. CMake, Ninja, and LLVM/Clang are the supported build stack.

## Supported targets

- Windows x64
- Windows ARM64
- Linux x64

All targets are 64-bit. The checked-in CMake presets build `dbg`, `opt`, and `fin` configurations.

## Requirements

Common tools:

- Git with Git LFS
- CMake 3.25 or newer
- Ninja
- LLVM/Clang, including the LLVM linker and archive tools, with a C++23-or-newer standard library providing `std::expected`
- Python 3.8 or newer for the launcher and string-literal compiler pipeline
- The compiler host's LLVM C API shared library matching the C and C++ Clang version exactly
- `slangc` for the asset pipeline, which is enabled by default
- A Vulkan loader and a compatible Vulkan driver for rendering

Windows hosts require Windows 10 version 1709 or newer. The launcher requires [IsWow64Process2](https://learn.microsoft.com/en-us/windows/win32/api/wow64apiset/nf-wow64apiset-iswow64process2) for native architecture discovery; frame creation uses current Per-Monitor v2 DPI APIs, including [SetProcessDpiAwarenessContext](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setprocessdpiawarenesscontext).

Windows builds also need Visual Studio 2022 Build Tools or Visual Studio 2022 with the C++ workload and a Windows SDK. Install the ARM64 C++ tools when building the ARM64 presets. CMake, Ninja, and LLVM may come from Visual Studio or standalone installations. Compilation requires Clang; the Microsoft tools provide the target ABI, runtime, headers, and libraries. Compiler frontend support retains GNU-style Clang/AppleClang and clang-cl; native `cl.exe` compilation is unsupported.

Configuration selects a supported C++23-or-newer frontend mode and compiles a real `std::expected` probe. The compiler and target standard library must provide `__cpp_lib_expected >= 202202L`; older language/library combinations fail configuration. First-party headers also enforce the Clang compiler requirement and query its supported attributes/builtins directly; CMake owns compiler/frontend selection. The current filesystem `Path` API always supports `char8_t` input, and `genericU8String()` returns `std::u8string`. Current result contracts use global `Expected<T, E>`, `Unexpected<E>` and `MakeUnexpected(error)` from `global/expected.h`. See [produced values and expected failures](docs/expected_results.md) for admission, ownership and mutation rules.

The Vulkan SDK is optional. The repository vendors Vulkan headers and Volk; the SDK is a convenient source for `slangc`, validation layers, and Vulkan diagnostics.

Linux builds require X11, zlib, libcurl, and oneTBB development packages. Wayland support is enabled when the Wayland client, scanner, protocols, and xkbcommon development files are available.

See [Build and Verification](https://github.com/limztudio/NWBLot/wiki/Build-and-Verification) for installation details, tool discovery, every preset, output locations, and focused test commands.

## Quick start

Use the repository launcher for configuration and builds. It selects the matching preset and configures automatically when needed.

### Windows ARM64

```powershell
python -m launcher build all --arch arm64 --config dbg
```

### Windows x64

```powershell
python -m launcher build all --arch x64 --config dbg
```

### Linux x64

```bash
python3 -m launcher build all --arch x64 --config dbg
```

Build a project without starting it, or select one or more targets for a focused build:

```powershell
python -m launcher testbed --build-only --config dbg
python -m launcher build nwb_global_tests nwb_ui_tests --config dbg
```

See [the launcher guide](launcher/README.md) for custom build directories, configurations, and command previews.

## Run the Testbed and tools

The repository launcher configures when needed, builds the selected target, and starts it from the correct runtime directory. On Windows it selects the native host architecture unless `--arch` is supplied.

```powershell
python -m launcher testbed --config dbg
python -m launcher pipeline --help
python -m launcher ui-skin --help
python -m launcher pipeline --config dbg --asset-root impl/assets CoolStuff/Testbed/assets --output-directory runtime/res
python -m launcher smoke --profiles
python -m launcher profiles
```

The root launcher discovers the `pipeline` command from `pipeline/launch.py`, using the same `launch.py` entry-point convention as projects and utilities. The pipeline launcher accepts build and asset options together, builds the three tools, and runs `dependency_computer`, `asset_builder`, and `asset_gatherer` in order. Use `--skip-build` with existing tools or `--dry-run` to preview the workflow; `--help` lists all options without building. The tools build with `NWB_BUILD_PIPELINE=ON` (the default). See [the pipeline guide](pipeline/readme.md) for direct stage commands and [the filesystem guide](docs/filesystem.md) for project filesystem customization.

The `ui-skin` utility owns `utilities/ui_skin/launch.py` and generates artwork and atlas metadata through the same root entry point. See [its guide](utilities/ui_skin/README.md) for regeneration and texture conversion.

Use `--with-profile` to start the log server with a launched application. Use `--run-seconds <N>` for a bounded profiling run.

## Build configurations and outputs

| Configuration | Configuration macro | Clang optimization | Frame pointer |
| --- | --- | --- | --- |
| `dbg` | `NWB_DEBUG` | `-O0` | Kept |
| `opt` | `NWB_OPTIMIZE` | `-O2` | Kept |
| `fin` | `NWB_FINAL` | `-O3` | Omitted |

CMake defines the corresponding configuration macro directly. Use `defined(NWB_DEBUG)`, `defined(NWB_OPTIMIZE)`, or `defined(NWB_FINAL)` for configuration-specific code. First-party headers do not infer NWB modes from `DEBUG`, `_DEBUG`, `NDEBUG`, or `_NDEBUG`; external compiler/runtime uses of those spellings remain separate.

Use the exact lowercase configuration names `dbg`, `opt`, and `fin`. A single-config build tree defaults an omitted or empty `CMAKE_BUILD_TYPE` to `dbg` and rejects every other nonempty name, including `Debug`, `Release`, `OFF`, `0`, and case variants. Multi-config presets expose the same canonical names. Imported third-party libraries may map those names to their vendor `Debug`/`Release` artifacts; the project configuration names remain unchanged.

Configure trees are written below `__cmake/build/<configure-preset>/`. Runtime artifacts use these roots:

- Engine-only: `__exec/<platform>/<arch>/<config>/`
- Full: `__exec/<platform>/<arch>/full/<config>/`
- Testbed-only: `__exec/<platform>/<arch>/testbed/<config>/`
- Name-symbol: `__exec/<platform>/<arch>/namesym/<config>/`

Building `testbed` also cooks its required assets into the matching runtime `res` directory.

Shader metadata declares its physical type, such as `compute_shader asset;` or `pixel_shader asset;`, alongside its Slang source. Runtime code uses concrete shader references and typed loaders; the shared `IShader` interface retains entry-point text and bytecode. See [the shader asset guide](impl/assets_shader/README.md) for all stage types, authored fields, and archive roles.

GPU payload packing preserves live numerical values and uses actual compiled sizes and shared CPU/shader contracts. See [GPU payload packing](docs/gpu_payload_packing.md) for the current compact layouts, recooking requirements, and runtime/performance qualification.

String pooling applies to C/C++ literals on every target through Clang's normal literal pooling; clang-cl also receives `/GF`. `NWB_OBFUSCATE_STRING_LITERALS=ON` is the default. The compiler pipeline pools and encodes eligible literals that survive optimization, then one decoder per executable or shared library restores their bytes before application static initialization. `constexpr StringView` declarations retain compile-time evaluation and direct pointer/length access, with no per-access decode checks or string allocations.

Obfuscation requires a Ninja or Makefiles generator, Python 3.8 or newer, and a matching LLVM C API shared library that Python can load on the compiler host. Configuration discovers and validates the library, or accepts its path through `-D NWB_LLVM_C_LIBRARY=<path>`; missing tools, mismatched versions, and competing compiler/linker launchers fail configuration. Set `-D NWB_OBFUSCATE_STRING_LITERALS=OFF` to retain ordinary pooling without encoding. Encoded records add startup work, headers, alignment, and writable memory; pooling savings depend on the workload. Decoded text remains readable in memory, and named arrays, identity-sensitive objects, optimized immediates, debug data, and name-symbol data are outside the transformation's coverage. See [the launcher guide](launcher/README.md#string-literal-pooling-and-obfuscation) for configuration examples and limitations.

## Rendering portability

Graphics providers are selected at compile time. The `NWB_GRAPHICS_BACKEND` CMake cache setting defaults to `Vulkan`, which is the only implemented provider. `Metal` is reserved for a future provider and currently fails configuration. CMake publishes matching `NWB_GRAPHICS_BACKEND_VULKAN=1` and `NWB_GRAPHICS_BACKEND_METAL=0` definitions.

Graphics consumers and tests use neutral contracts from `core/graphics/rhi/` and the narrow headers under `core/graphics/backend_selection/`. `backend_selection.h` selects the full context; `backend_selection/backend.h` selects public device and resource definitions. Native Vulkan headers stay behind the provider and selection boundary. Graphics tests live under `tests/` and exercise public contracts after normal backend selection; provider-specific tests, private provider fixtures, and production test hooks are prohibited. Selection preserves concrete calls and resource layouts without adding runtime dispatch, per-resource storage, or allocations. See [Architecture](https://github.com/limztudio/NWBLot/wiki/Architecture) for ownership and include boundaries.

The Vulkan backend validates required device capabilities at startup. `VK_EXT_descriptor_buffer` is required by the renderer. Windows ARM64 disables native mesh shaders by default; eligible engine meshes use persistent indexed raster, while geometry paths without a compatible indexed archive stage use compute emulation. A qualified adapter can opt into native mesh shaders before graphics instance creation.

Texture cooking and runtime format selection account for device format support, including BC and ASTC-capable GPUs. See [Renderer Feature Paths](https://github.com/limztudio/NWBLot/wiki/Renderer-Feature-Paths) and [Texture Conversion](https://github.com/limztudio/NWBLot/wiki/Texture-Conversion) for the current contracts.

## Macro and namespace ownership

NWBLot-owned utility, configuration, diagnostic, namespace, and shader feature macro APIs use `NWB_`, including the shared utilities in `global/`. Application-owned macro APIs use an application prefix, such as `TESTBED_`. External/compiler interoperability spellings and existing `g_*` shader resource aliases retain their current contracts. Utility types and functions retain their existing scopes.

Root `engine_namespace.h` owns the engine namespace wrappers. Engine domains reach them through their own umbrella headers. Testbed owns `CoolStuff/Testbed/namespace.h` for `namespace Testbed`; its classes and helpers belong there, while the required loader entry adapter functions remain in `NWB`. Projects include the engine APIs they use and receive engine namespace definitions transitively. See [Architecture](https://github.com/limztudio/NWBLot/wiki/Architecture#namespace-ownership) and [Project API](https://github.com/limztudio/NWBLot/wiki/Project-API#namespace-ownership) for the ownership boundaries.

Project-owned abstract interfaces retain the lowercase `interface` annotation supplied by `global/compile.h`, which expands portably to `struct` when an external SDK has not defined it. Their virtual methods retain the declared ownership, lifetime, and exception contracts.

## Source and dependency registration

`CMakePresets.json` defines supported build variants. Each target's nearest `CMakeLists.txt` owns its source list through `target_sources`; add new C/C++ files there and use `python -m launcher build <target> --configure always` to refresh the build.

Third-party packages are vendored as flat top-level directories under `3rd_parties/`. Each package records its source and version in `nwb_update.txt`. See [Third-Party Packages](https://github.com/limztudio/NWBLot/wiki/Third-Party-Packages) before updating a dependency.

## Documentation

Start with the [NWBLot Wiki](https://github.com/limztudio/NWBLot/wiki), then use [Architecture](https://github.com/limztudio/NWBLot/wiki/Architecture), [Asset Flow](https://github.com/limztudio/NWBLot/wiki/Asset-Flow), [Runtime and ECS](https://github.com/limztudio/NWBLot/wiki/Runtime-and-ECS), and [Build and Verification](https://github.com/limztudio/NWBLot/wiki/Build-and-Verification) for the corresponding subsystem.

The [project optimization audit](docs/project_optimization_audit.md) records CPU and scratch-memory measurements, rejected candidates, and runtime qualification for retained changes.
