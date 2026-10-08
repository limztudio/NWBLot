# Repository launcher

Run project builds and applications from the repository root with `python -m launcher` (`python3` on Linux).
The launcher selects the matching configuration preset, configures when necessary, and builds the requested targets and dependencies.
All configuration and compilation commands can use this entry point. Use CTest to execute the registered suites after building;
[the test guide](../tests/README.md) describes test selection and execution.

Build Testbed without starting it:

```powershell
python -m launcher testbed --build-only --config opt
```

Build one or more executable, library, or aggregate targets:

```powershell
python -m launcher build testbed --config opt
python -m launcher build nwb_graphics_backend nwb_global_tests --config dbg
python -m launcher build all --config opt
```

Build and run a project, or use the generic executable-target command:

```powershell
python -m launcher testbed --config opt
python -m launcher run testbed --config opt
```

Project and native utility shortcuts retain their own `launch.py` files. Their `--build-only` option stops after building;
it does not resolve an executable, start the application, or start a profiling server.
With `--with-profile`, a build-only project invocation also prepares the profiling dependencies for a later run.

Common build controls:

- `--config dbg`, `opt`, or `fin` selects the build configuration.
- `--arch arm64` or `x64` selects the target architecture; Windows defaults to the native host architecture.
- `--platform windows` or `linux` selects the build platform; the default is the host platform.
- `--domain engine`, `testbed`, `full`, or `namesym` selects a build variant; `full` is the default. The `namesym` variant enables the name-symbol build mode.
- `--configure-preset <name>` selects an explicit preset from `CMakePresets.json`.
- `--jobs 8` controls parallel build work.
- `--configure always` refreshes configuration; `--configure never` fails if configuration is required.
- `-D KEY=VALUE` supplies a configuration setting and triggers configuration.
- `--dry-run` prints the planned commands without configuring, building, or launching anything.

The selected configuration defines `NWB_DEBUG` for `dbg`, `NWB_OPTIMIZE` for `opt`, or `NWB_FINAL` for `fin` directly through CMake.

Configuration names are exactly lowercase `dbg`, `opt`, and `fin`. For a single-config tree, omitted or empty `CMAKE_BUILD_TYPE` defaults to `dbg`; all other nonempty values fail configuration, including `Debug`, `Release`, `OFF`, `0`, and case variants. Multi-config presets expose the same three names. Vendor imported-library configuration mappings remain internal dependency integration. The compiler must be Clang-based; GNU-style Clang/AppleClang and clang-cl frontend paths remain supported, with the Windows/MSVC ABI and SDK required for Windows targets.

Windows hosts require Windows 10 version 1709 or newer, the documented minimum for
[IsWow64Process2](https://learn.microsoft.com/en-us/windows/win32/api/wow64apiset/nf-wow64apiset-iswow64process2). Native host detection requires that API. An unavailable API, a failed query, or an
unsupported native machine type produces an error; architecture is not guessed from the Python process or environment.
Use `--arch` to select a target explicitly. Other platforms retain their native machine-name normalization.

A new custom build directory is configured from the selected preset automatically. Relative paths are resolved against the repository root:

```powershell
python -m launcher build testbed --config opt --build-dir "__cmake/build/custom project"
```

Use `--repo-root` to build another checkout with the same project/preset structure. Installation of the toolchain dependencies
listed in the root README is still required; the launcher invokes those tools internally.

Native applications start from their runtime output domain directory unless `--working-directory` is supplied.
For utility examples with repository-relative input or output paths, use `--working-directory .` before the application
argument separator so those paths resolve against the repository root.

Pass application arguments after `--` when running:

```powershell
python -m launcher testbed --config opt -- --gpudbg
```

`--gpudbg` enables graphics validation in `dbg` and `opt` applications. `fin` applications omit this option and reject
explicit requests during command-line parsing. Python capture harnesses also expose `--gpu-validation` / `--no-gpu-validation`;
use `--no-gpu-validation` with a `fin` executable. The root smoke launcher can pass the native request after `--`, as shown
above.

Build-only commands reject application arguments and contradictory `--skip-build` requests before making changes.
`--skip-build` remains available for launching existing binaries. Real launches require an executable artifact from the
selected build and configuration's CMake File API reply, unless `--executable` supplies the path explicitly. Missing target
metadata or artifacts fail before launch. Dry runs can preview the repository naming convention; `--executable-name` and
`--profile-logserver-name` only change that preview. `python -m launcher profiles` lists the discovered project and utility commands.

Specialized workflows such as `pipeline`, `smoke`, and the A/B runners have their own options; use their `--help` output.
To build their CMake targets without running the workflow, use the generic `build` command. The Python `ui-skin` generator
also owns a `launch.py` entry point and is available as `python -m launcher ui-skin`; see [its guide](../utilities/ui_skin/README.md).

## String-literal pooling and obfuscation

Clang's ordinary C/C++ literal pooling applies on every target; clang-cl also receives `/GF`. The default
`NWB_OBFUSCATE_STRING_LITERALS=ON` build adds an optimized-bitcode transformation and one decoder per executable or shared
library. Eligible narrow, UTF-16, and UTF-32 literals share encoded records when their bytes, character width, and alignment
match. The decoder restores those records before application static initialization; their storage then lasts for the image's
lifetime. Source `constexpr StringView` evaluation, borrowed view lifetime, and direct pointer/length access remain intact.

The pipeline requires Python 3.8 or newer, a Ninja or Makefiles generator, and the compiler host's LLVM C API shared library.
Its major, minor, and patch version must match both C and C++ Clang exactly, and Python must be able to load its host
architecture. Configuration discovers candidate libraries beside the compiler and in toolchain library directories, validates
the required API exports, and fails if no matching library is available. Supply an explicit library when needed:

```powershell
python -m launcher build all --config opt -D "NWB_LLVM_C_LIBRARY=C:/Program Files/LLVM/bin/LLVM-C.dll"
```

To keep ordinary pooling and disable encoding:

```powershell
python -m launcher build all --config opt -D NWB_OBFUSCATE_STRING_LITERALS=OFF
```

These are CMake cache settings, so a later build keeps the chosen value; pass `-D NWB_OBFUSCATE_STRING_LITERALS=ON` to
reenable encoding. Enabled builds own the compiler and linker launchers and reject competing launchers. They preserve the
selected `-O0`/`-O2`/`-O3` code-generation level and run LLVM optimization before encoding, without a second LLVM
optimization pipeline afterward. Unsupported transformed object modes, including LTO and multi-source object commands,
fail explicitly. Failed compilation preserves an existing successful object.

Pooling can reduce duplicated literal storage, while encoding adds record headers, alignment, writable pages, and one
startup pass over the records. There is no per-access decode branch or heap string allocation. Obfuscation encodes eligible
compiler literal globals; named arrays, address-significant objects, explicitly retained globals, optimized immediates, debug
data, and name-symbol data remain outside its coverage. Decoded text is readable in process memory. The object pipeline
handles COFF, ELF, and Mach-O formats, which does not expand the repository's supported application platforms.
