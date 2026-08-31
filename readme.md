# NWBLot

NWBLot is a C++ Vulkan engine with an asset cooker, runtime loader, ECS renderer, developer tools, automated tests, and a runnable Testbed. CMake, Ninja, and LLVM/Clang are the supported build stack.

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
- LLVM/Clang, including the LLVM linker and archive tools
- Python 3 for the launcher and test-enabled builds
- `slangc` for the resource cooker, which is enabled by default
- A Vulkan loader and a compatible Vulkan driver for rendering

Windows builds also need Visual Studio 2022 Build Tools or Visual Studio 2022 with the C++ workload and a Windows SDK. Install the ARM64 C++ tools when building the ARM64 presets. CMake, Ninja, and LLVM may come from Visual Studio or standalone installations.

The Vulkan SDK is optional. The repository vendors Vulkan headers and Volk; the SDK is a convenient source for `slangc`, validation layers, and Vulkan diagnostics.

Linux builds require X11, zlib, libcurl, and oneTBB development packages. Wayland support is enabled when the Wayland client, scanner, protocols, and xkbcommon development files are available.

See [Build and Verification](https://github.com/limztudio/NWBLot/wiki/Build-and-Verification) for installation details, tool discovery, every preset, output locations, and focused test commands.

## Quick start

### Windows ARM64

```powershell
cmake --preset windows-clang-arm64
cmake --build --preset windows-clang-arm64-dbg
ctest --preset windows-clang-arm64-dbg
```

### Windows x64

```powershell
cmake --preset windows-clang-x64
cmake --build --preset windows-clang-dbg
ctest --preset windows-clang-dbg
```

### Linux x64

```bash
cmake --preset linux-clang-x64
cmake --build --preset linux-clang-dbg
ctest --preset linux-clang-dbg
```

Append `--target <target>` to a build command for a focused build. For example:

```powershell
cmake --build --preset windows-clang-arm64-dbg --target testbed
```

## Run the Testbed and tools

The repository launcher configures when needed, builds the selected target, and starts it from the correct runtime directory. On Windows it selects the native host architecture unless `--arch` is supplied.

```powershell
python launcher.py testbed --config dbg
python launcher.py run nwb_resource_cooker -- --help
python launcher.py smoke --profiles
python launcher.py profiles
```

Use `--with-profile` to start the log server with a launched application. Use `--run-seconds <N>` for a bounded profiling run.

## Build configurations and outputs

| Configuration | Clang optimization | Frame pointer |
| --- | --- | --- |
| `dbg` | `-O0` | Kept |
| `opt` | `-O2` | Kept |
| `fin` | `-O3` | Omitted |

Configure trees are written below `__cmake/build/<configure-preset>/`. Runtime artifacts use these roots:

- Engine-only: `__exec/<platform>/<arch>/<config>/`
- Full: `__exec/<platform>/<arch>/full/<config>/`
- Testbed-only: `__exec/<platform>/<arch>/testbed/<config>/`
- Name-symbol: `__exec/<platform>/<arch>/namesym/<config>/`

Building `testbed` also cooks its required assets into the matching runtime `res` directory.

## Rendering portability

The Vulkan backend validates required device capabilities at startup. `VK_EXT_descriptor_buffer` is required by the renderer. Windows ARM64 uses the compute-emulation mesh path by default; a qualified adapter can opt into native mesh shaders before graphics instance creation.

Texture cooking and runtime format selection account for device format support, including BC and ASTC-capable GPUs. See [Renderer Feature Paths](https://github.com/limztudio/NWBLot/wiki/Renderer-Feature-Paths) and [Texture Conversion](https://github.com/limztudio/NWBLot/wiki/Texture-Conversion) for the current contracts.

## Source and dependency registration

`CMakePresets.json` defines supported build variants. Each target's nearest `CMakeLists.txt` owns its source list through `target_sources`; add new C/C++ files there and reconfigure CMake.

Third-party packages are vendored as flat top-level directories under `3rd_parties/`. Each package records its source and version in `nwb_update.txt`. See [Third-Party Packages](https://github.com/limztudio/NWBLot/wiki/Third-Party-Packages) before updating a dependency.

Read [`.helper/standard.md`](.helper/standard.md) before changing project code. Use the project wrappers defined under `global/`; in particular, `BitCast` from `global/bit.h` is the project wrapper for `std::bit_cast`.

## Documentation

Start with the [NWBLot Wiki](https://github.com/limztudio/NWBLot/wiki), then use [Architecture](https://github.com/limztudio/NWBLot/wiki/Architecture), [Asset Flow](https://github.com/limztudio/NWBLot/wiki/Asset-Flow), [Runtime and ECS](https://github.com/limztudio/NWBLot/wiki/Runtime-and-ECS), and [Build and Verification](https://github.com/limztudio/NWBLot/wiki/Build-and-Verification) for the corresponding subsystem.
