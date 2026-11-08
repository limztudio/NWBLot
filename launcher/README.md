# Repo launcher

Run builds and apps from the repo root with `python -m launcher` (`python3`
on Linux). The launcher picks the matching setup, sets up when needed, and
builds what you asked for plus what it needs. All setup and build calls can
go through this one door. Use CTest to run the listed test sets after a
build. [The test guide](../tests/README.md) shows test pick and run.

Build Testbed with no start:

```powershell
python -m launcher testbed --build-only --config opt
```

Build one or more exe, lib, or group targets:

```powershell
python -m launcher build testbed --config opt
python -m launcher build nwb_graphics_backend nwb_global_tests --config dbg
python -m launcher build all --config opt
```

Build and run a project, or use the plain exe-target form:

```powershell
python -m launcher testbed --config opt
python -m launcher run testbed --config opt
```

Project and native tool short forms keep their own `launch.py` files. Their
`--build-only` flag stops after build. It does not find an exe, start the
app, or start a profile server. With `--with-profile`, a build-only project
call also readies the profile needs for a later run.

Common build flags:

- `--config dbg`, `opt`, or `fin` picks the build form.
- `--arch arm64` or `x64` picks the target chip. Windows defaults to the
  host chip.
- `--platform windows` or `linux` picks the build box. Default is the host
  box.
- `--domain engine`, `testbed`, `full`, or `namesym` picks a build set.
  `full` is the default. `namesym` turns on name-symbol build mode.
- `--configure-preset <name>` picks a set form from `CMakePresets.json`.
- `--jobs 8` sets side-by-side build jobs.
- `--configure always` refreshes setup. `--configure never` fails if setup
  is needed.
- `-D KEY=VALUE` gives a setup setting and starts setup.
- `--dry-run` prints the planned calls with no setup, build, or start.

The picked form sets `NWB_DEBUG` for `dbg`, `NWB_OPTIMIZE` for `opt`, or
`NWB_FINAL` for `fin` straight through CMake.

Setup names are just lower-case `dbg`, `opt`, and `fin`. For a one-form tree,
a missing or blank `CMAKE_BUILD_TYPE` defaults to `dbg`. All other set values
fail setup. This holds `Debug`, `Release`, `OFF`, `0`, and case forms. Many-form
sets show the same three names. Bought lib setup maps stay inside those lib
hooks. The compiler must be Clang-based. GNU-form Clang/AppleClang and
clang-cl front paths stay good, with the Windows/MSVC ABI and SDK needed for
Windows targets.

Windows hosts need Windows 10 form 1709 or newer, the stated floor for
[IsWow64Process2](https://learn.microsoft.com/en-us/windows/win32/api/wow64apiset/nf-wow64apiset-iswow64process2).
Host chip find needs that API. A missing API, a failed ask, or a bad host
chip type gives a fault. The chip is not guessed from the Python run or env.
Use `--arch` to pick a target outright. Other boxes default to `x64`.

A new plain build folder is set up from the picked set form on its own. Short
paths fix against the repo root:

```powershell
python -m launcher build testbed --config opt --build-dir "__cmake/build/custom project"
```

Use `--repo-root` to build another copy with the same project/set form. Put
in the tool needs listed in the root [readme](../readme.md) first.

The launcher finds the built exe through the picked build and form CMake
File API reply, unless `--executable` gives the path outright. Missing target
facts or files fail before start. Dry runs can show the repo name form.
`--executable-name` and `--profile-logserver-name` only change that show.
`python -m launcher profiles` lists the found project and tool calls.

A plain `--profile-logserver-executable` must take
`--readiness-token <token>` and give back that exact plain token from
`GET /ready` on its picked loop-back port. The launcher uses a fresh token
to turn down a wrong listener before it starts the app. This check names the
start listener. It does not guard later links.

Set flows like `pipeline`, `smoke`, and the A/B runners have their own flags.
Use their `--help` text. To build their CMake targets with no flow run, use
the plain `build` call. The Python `ui-skin` maker also owns a `launch.py`
door and shows as `python -m launcher ui-skin`. See [its
guide](../utilities/ui_skin/README.md).

## String pool and hide

Clang plain C/C++ text pool runs on each target. Clang-cl also gets `/GF`.
The default `NWB_OBFUSCATE_STRING_LITERALS=ON` build adds a tuned-bitcode
change and one decoder per exe or shared lib. Set narrow, UTF-16, and UTF-32
texts share coded rows when their bytes, char width, and align match. The
decoder fills those rows back before app still setup runs. Their store then
lasts for the image life. Source `constexpr StringView` checks, lent view
life, and direct point/length use stay whole.

The pipe needs Python 3.8 or newer, a Ninja or Makefiles maker, and the host
LLVM C API shared lib. Its big, small, and fix form must match both C and C++
Clang just so, and Python must load its host chip form. Setup finds likely
libs next to the compiler and in tool lib folders. It checks the needed API
outs and fails if no match lib is there. Give a clear lib when needed:

```powershell
python -m launcher build all --config opt -D "NWB_LLVM_C_LIBRARY=C:/Program Files/LLVM/bin/LLVM-C.dll"
```

To keep plain pool and turn off code form:

```powershell
python -m launcher build all --config opt -D NWB_OBFUSCATE_STRING_LITERALS=OFF
```

These are CMake kept settings, so a later build keeps the picked value. Pass
`-D NWB_OBFUSCATE_STRING_LITERALS=ON` to turn code form back on. On builds own
the compiler and link start calls and turn down rival starts. They keep the
picked `-O0`/`-O2`/`-O3` code-make level and run LLVM tune before code form,
with no second LLVM tune pipe after. Bad changed file modes, with LTO and
many-source file calls, fail in the open. Failed build keeps an old good
file.

Pool can cut dup text store, while code form adds row heads, align, write
pages, and one start pass over the rows. There is no per-use decode fork or
heap string make. Hide codes set compiler text globals. Named arrays,
point-key files, kept globals, tuned direct values, debug data, and
name-symbol data stay outside its reach. Decoded text reads clear in run
store. The file pipe takes COFF, ELF, and Mach-O forms. This does not grow
the repo backed app boxes.
