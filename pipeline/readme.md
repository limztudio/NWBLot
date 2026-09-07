# Asset pipeline

The pipeline has three independent executables and one Python orchestrator. Build
the `nwb_pipeline` CMake target to produce `dependeny_computer`, `asset_builder`,
and `asset_gatherer` (with `.exe` on Windows). `NWB_BUILD_PIPELINE` controls these
targets and the asset build libraries.

`dependeny_computer` currently returns exactly its inputs, preserving order and
duplicates. Root asset selection and dependency expansion are not implemented.
It accepts multiple `--input` values, repeated `--input` options, or an
`--input-list` file. Its `--output` is a UTF-8 file containing one input per line.

```console
dependeny_computer --input project/assets/samplers/a.nwb project/assets/samplers/b.nwb --output dependencies.list
```

`asset_builder` compiles selected `.nwb` assets into standalone runtime binary
artifacts. It accepts files or directories as inputs. `--asset-root` supplies
the roots used to resolve references and shader includes; roots under `impl`
use the `engine/` virtual namespace, and other roots use `project/`. When omitted,
the builder finds the nearest `assets` ancestor of each input, or uses the input's
parent directory. Inputs must belong to one of the supplied or inferred roots.
The builder only parses selected inputs; it does not expand their dependencies.
Related assets required by metadata validation must be included in the inputs.

```console
asset_builder --input-list dependencies.list --repo-root . --asset-root project/assets --output-directory built --configuration dbg
```

The output directory contains `.nwba` files and `assets.list`. Each artifact
contains an 88-byte header followed by the exact runtime codec payload. The
header stores a 32-bit magic (`0x4142574e`), 32-bit version (`1`), 64-byte virtual
path `NameHash`, 64-bit payload size, and 64-bit FNV payload checksum. These fields
use the engine's native little-endian binary encoding. The gatherer validates the
header, size, checksum, and virtual identity before packaging the payload.

Artifact names include their virtual path hash and payload hash. `assets.list`
contains relative filenames for only the current successful build. It is
published after all artifacts have been written, so failed builds leave the
previous manifest intact and removed inputs do not return through stale files.
Older content files may remain in the build directory; directory gathering uses
the current manifest. The entire build directory can be copied to another machine
or path without its source files or compiler cache.

`asset_gatherer` accepts `.nwba` files, build output directories, or an
`--input-list`. Paths in a gather input list are relative to that list's directory.
Directories with `assets.list` use that manifest; other directories are scanned
for `.nwba` files. Identical duplicate virtual identities and payloads are merged.
The graphics metadata merge callback combines shader archive index records from
independent builds; conflicting records or other different payloads for the same
identity fail. Gathering performs no source parsing or compilation. It publishes the existing `graphics` volume format, using
the default volume implementation through `IFilesystem`. The staged publication
preserves the previous volume if validation or packing fails.

```console
asset_gatherer --input built-a built-b --output-directory runtime/res
asset_gatherer --input-list built/assets.list --output-directory runtime/res
```

`cooker.py` replaces the removed `resource_cooker` executable. It discovers `.nwb`
files, calls `dependeny_computer`, passes that result to `asset_builder`, then
passes the builder manifest to `asset_gatherer`. A failed stage stops the pipeline. Source discovery errors also stop cooking
before the tools run, preserving the published volume.
Existing `--repo-root`, `--asset-root`, `--output-directory`, `--cache-directory`,
`--configuration`, and `--asset-type graphics` options remain available.

```console
python pipeline/cooker.py --tool-directory __exec/windows/arm64/full/dbg --repo-root . --asset-root impl/assets CoolStuff/Testbed/assets --output-directory runtime/res --configuration dbg
```

Use `--dependency-computer`, `--asset-builder`, and `--asset-gatherer` to specify
individual executable paths. Without explicit paths or `--tool-directory`, tools
are discovered beside the script or on `PATH`. `--input` limits the selected
sources. `--build-directory` chooses where to retain intermediate artifacts;
otherwise they live in an output-specific directory under the asset cache.

The reusable C++ entry points are `Core::Assets::BuildAssets(AssetBuildOptions)`
in `core/assets/volume/build.h` and `Core::Assets::GatherAssets(AssetGatherOptions)`
in `core/assets/volume/gather.h`. Asset codecs, metadata parsers, and build
registrations remain in their owning asset modules. The retired cooker interface,
registry, combined volume cooker, and executable are removed.

Library callers gathering independently built graphics assets set
`AssetGatherOptions::mergePayloads` to `Impl::MergeGatheredGraphicsAsset`, declared
in `impl/assets_graphics/gather.h` and linked through `nwb_assets_graphics_gather`.
The generic gather API also accepts a project callback for other metadata merge
contracts. A callback receives the duplicate virtual identity, an existing payload
to update, and the incoming payload bytes. Returning `false` rejects the conflict
and preserves the published volume. Callbacks must leave the existing payload
unchanged when they fail.
