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
- `--domain engine`, `testbed`, or `full` selects a build variant.
- `--configure-preset <name>` selects an explicit preset from `CMakePresets.json`.
- `--jobs 8` controls parallel build work.
- `--configure always` refreshes configuration; `--configure never` fails if configuration is required.
- `-D KEY=VALUE` supplies a configuration setting and triggers configuration.
- `--dry-run` prints the planned commands without configuring, building, or launching anything.

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

Build-only commands reject application arguments and contradictory `--skip-build` requests before making changes.
`--skip-build` remains available for launching existing binaries, and `python -m launcher profiles` lists the discovered project and utility commands.

Specialized workflows such as `pipeline`, `smoke`, and the A/B runners have their own options; use their `--help` output.
To build their CMake targets without running the workflow, use the generic `build` command. The Python `ui-skin` generator
also owns a `launch.py` entry point and is available as `python -m launcher ui-skin`; see [its guide](../utilities/ui_skin/README.md).
