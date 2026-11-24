# Asset pipeline

The `pipeline/` tree contains four executables and one Python orchestrator. Build
the `nwb_pipeline` CMake target to produce `adhd_compiler`, `dependency_computer`,
`asset_builder`, and `asset_gatherer` (with `.exe` on Windows); `pipeline/launch.py`
runs the three asset-cooking stages (`dependency_computer`, `asset_builder`, and
`asset_gatherer`), while `adhd_compiler` compiles `.adhd` sources during the CMake build.
The launcher requires Python 3.10 or newer when `--input` is supplied.
`NWB_BUILD_PIPELINE` controls these targets and the asset build libraries.

Run the complete pipeline through the repository launcher:

```console
python -m launcher pipeline --config dbg --asset-root impl/assets project/assets --output-directory runtime/res
```

To prepare the tools without running asset stages, use:

```console
python -m launcher build nwb_pipeline --config dbg -D NWB_BUILD_PIPELINE=ON
```

Both commands configure and build automatically. See [the launcher guide](../launcher/README.md)
for architecture, configuration, custom build directories, and dry-run previews.

The root discovers `pipeline/launch.py` as a normal runnable directory. Build
and asset options use one argument list. The pipeline launcher configures when
needed with `NWB_BUILD_PIPELINE=ON`, builds `nwb_pipeline`, and resolves all three
tools from the selected CMake configuration. Configure, build, and pipeline
failures retain their nonzero exit status at the root launcher.

`dependency_computer` computes transitive `.nwb` asset dependencies. When no explicit
`--input` or `--input-list` is provided, it automatically discovers `project.nwb` in
non-engine `--asset-root` directories and expands the dependency graph from the project's
`start_scene` through scene subsystems, auto-activated feature subsystems, and reachable
assets via `CookDependencyRegistry`. When explicit `--input` or `--input-list` values are provided, it preserves those
seed inputs and expands their transitive dependencies across `--asset-root` providers.
That resolution accepts `--repo-root` (default: the working directory) and repeated
`--asset-root` values. Its provider catalog resolves metadata identities without reading
unrelated asset documents, and provider ownership follows asset-root order. Unresolved,
wrong-type, or ambiguous asset references fail before the output list is written.
Paired encoded texture payloads stay with the texture cooker and are not added as
separate inputs. Its `--output` is a UTF-8 file containing one input path per line. Explicit seed paths
are retained, and transitive dependencies add their resolved provider paths.

Entity metadata may provide a `name` alongside its required component references. A supplied name must be nonempty and unpadded; scene loading rejects duplicate resolved entity names. `ActiveSceneSubsystems::instantiateEntitiesWithResult()` returns the created entity IDs and a name lookup, and rolls back entities created by that call if instantiation fails. Call `destroyEntities()` when the owning code removes those instantiated entities.

```console
python -m launcher run nwb_dependency_computer --config dbg --working-directory . -- --repo-root . --asset-root impl/assets project/assets --output dependencies.list
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
python -m launcher run nwb_asset_builder --config dbg --working-directory . -- --input-list dependencies.list --repo-root . --asset-root impl/assets project/assets --output-directory built --configuration dbg
```

Standalone `.nwb` cooked assets are published directly under `__asset_cache/` as `<sha1(lower(absolute path))>.nwba` so projects share cached cooked assets. `assets.list` in the output directory references those shared `__asset_cache/*.nwba` paths alongside relative filenames for project-scoped `.nwba` files built in the output directory. When a standalone asset (or cached shader bytecode variant) is already cached and up to date, the cook notifies via `info(...)`.

Each `.nwba` file uses the unified version-2 binary format (`NWBA`): a 64-byte `BuiltAssetFileHeader` (`magic = 0x4142574e`, `version = 2`, `headerSize = 64`, flags, entry count, SHA-256 input fingerprint, payload section byte size, and 64-bit FNV payload-section checksum) followed by the source path string and `entryCount` validated asset entries. The gatherer validates the header, section checksum, per-entry payload FNV-64 checksums, and virtual identities before packaging payloads.

Project-scoped artifact names combine a compact hexadecimal slot with payload and cook hashes. The builder reuses byte-identical artifacts and advances past occupied slots with different contents, preserving artifacts referenced by an earlier manifest. `assets.list` references only the artifacts for the current successful build. It is published after all artifacts have been written, so failed builds leave the previous manifest intact and removed inputs do not return through stale files.

`asset_gatherer` accepts `.nwba` files, build output directories, or an `--input-list`. Relative paths in a gather input list resolve against that list's directory, while absolute paths resolve directly.
Directories with `assets.list` use that manifest; other directories are scanned
for `.nwba` files. Identical duplicate virtual identities and payloads are merged.
The graphics metadata merge callback combines shader archive index records from
independent builds; conflicting records or other different payloads for the same
identity fail. Gathering performs no source parsing or compilation. It publishes the existing `graphics` volume format, using
the default volume implementation through `IFilesystem`. Validation and packing failures before publication leave the
previous volume in place. If a publication fails, restoration is attempted; an incomplete rollback can leave the live
volume incomplete and retains the stage and backup directories for recovery.

```console
python -m launcher run nwb_asset_gatherer --config dbg --working-directory . -- --input built-a built-b --output-directory runtime/res
python -m launcher run nwb_asset_gatherer --config dbg --working-directory . -- --input-list built/assets.list --output-directory runtime/res
```

`pipeline/launch.py` is discovered by the root launcher as the `pipeline` command,
using the same convention as the repository's other runnable directories. It validates
asset roots and optional input overrides, configures and
builds `nwb_pipeline`, calls `dependency_computer`, passes that result to
`asset_builder`, then passes the builder manifest to `asset_gatherer`. Discovery
errors and failed stages stop the pipeline. A failed publication follows the recovery behavior described above.

```console
python -m launcher pipeline --config dbg --asset-root impl/assets project/assets --output-directory runtime/res
```

Build and asset options share one argument list. Use the root entry point for
the workflow and help; help returns without building:

```console
python -m launcher pipeline --help
```

The launcher reuses the repository's `--config`, `--arch`, `--domain`,
`--configure-preset`, `--build-dir`, `--jobs`, and other build options. The repository
root defaults to the repository containing the script. Asset arguments
`--repo-root`, `--asset-root`, `--output-directory`,
`--configuration`, and `--asset-type graphics` remain available. `--configuration`
overrides the asset configuration label; otherwise it follows `--config`. Omitting `--asset-type` selects `graphics`;
explicitly empty or unsupported values are rejected by the builder, including internal build requests.

By default, executable paths require executable artifacts from the selected CMake build and configuration's File API reply.
Missing metadata fails instead of guessing a tool path; dry runs retain naming previews.
Use `--tool-directory`, or `--dependency-computer`, `--asset-builder`, and
`--asset-gatherer`, to supply already built tools. `--skip-build` skips both CMake
configuration and compilation; CMake asset commands use it to avoid starting a
nested build. For example:

```console
python -m launcher pipeline --skip-build --tool-directory __exec/windows/arm64/full/dbg --asset-root impl/assets project/assets --output-directory runtime/res
```

`--dry-run` prints planned build and pipeline commands without writing manifests,
artifacts, or volumes, or running tools. `--input` overrides automatic `project.nwb`
root discovery with an explicit seed list while still expanding transitive dependencies.
`--build-directory` chooses where to write `assets.list` and project-scoped `.nwba` artifacts; otherwise
they use an ephemeral workspace cleaned up after gathering while standalone cooked `.nwba` assets remain shared in `__asset_cache/`.

The standalone asset cache keys cooked entries by their lowercase canonical absolute path (`__asset_cache/<sha1(lower(absolute path))>.nwba`), authored inputs, the
running producer executable's SHA-256, and all registered paired sidecars present
beside the metadata. Rebuilding the producer or changing a sidecar therefore
invalidates entries made by older inputs. When the executable cannot be
read, the builder reports the problem and cooks without reading or writing that
cache. Shader compilation also fingerprints its configured compiler and options.

The C++ dependency entry point is `NWB::Pipeline::DependencyComputer::ComputeAssetDependencies`
(and `WriteAssetDependencies`), declared with `AssetDependencyOptions` in
`pipeline/dependency_computer/dependencies.adhd`; link
`nwb_pipeline_dependency_computer` to call it. The build entry point is
`NWB::Pipeline::AssetBuilder::BuildAssets`, declared in `pipeline/asset_builder/build.adhd`,
with `AssetBuildOptions` in the same namespace; link `nwb_pipeline_asset_builder`
to call it. The gather entry point is `NWB::Pipeline::AssetGatherer::GatherAssets`,
declared with `AssetGatherOptions` in `pipeline/asset_gatherer/gather.adhd`; link
`nwb_pipeline_asset_gatherer` to call it. Each tool is authored in `.adhd` and owns its orchestration API and
implementation alongside the shared pipeline CLI/workspace layer (`nwb_pipeline_tools_common`
with `pipeline/command_line.adhd` and `pipeline/root_discovery.adhd`, `pipeline/cook_workspace.{h,adhd}`,
`pipeline/volume_workspace.{h,adhd}`, `pipeline/dependency_computer/dependency_workspace.{h,adhd}`,
and `utilities/cli/` for `nwb_tool_cli` / `nwb_target_console_entry`). Shared artifact, cache, manifest, and volume-writing support remains
in `core/assets/volume`; asset codecs, metadata parsers, and build registrations
remain in their owning asset modules.

Library callers gathering independently built graphics assets configure
`NWB::Pipeline::AssetGatherer::AssetGatherOptions::mergePayloads` (`NWB::Pipeline::GatherMergeCallback`):
C++ callers may assign `NWB::Core::Assets::QueryAutoCollectedAssetGatherMerge(scratchArena)`
(`core/assets/gather_merge_registry.h`), while `.adhd` callers use
`NWB::Pipeline::RegisteredGatherMerge(workspace)` (`pipeline/volume_workspace.adhd`).
The graphics asset implementation self-registers its merge hook; the gather tool consumes the core registration contract.
The generic gather API also accepts a project callback for other metadata merge
contracts. An `AssetGatherMergeFunction` callback receives the duplicate virtual identity,
the incoming payload bytes, the operation scratch arena, and an existing payload
to update (`(virtualPath, incomingPayload, scratchArena, existingPayload)`). Returning `false` rejects the conflict
and preserves the published volume. Callbacks must leave the existing payload
unchanged when they fail.

## Library result contracts

Fallible path, metadata, dependency-selection and volume-write producers return values through global `Expected<T, E>`. `DependencyProviderCatalog::selectInputs` and `resolve` return the selection and provider index after admission, while document reading is owned by `NWB::Pipeline::CookDependencyAccess::read` (`pipeline/dependency_computer/dependency_workspace.{h,adhd}`, exposed through `DependencyProviderCatalog::access`). `WriteAssetVolume` returns `AssetVolumeWriteResult`, and `ReadBuiltAsset` returns entry identity and payload range data while retaining its caller-owned byte buffer. Build/gather orchestration, registered metadata accumulation and conflict callbacks keep their actual mutation or callback contracts. Check results before consuming them, preserve supplied arenas, and propagate failure before manifest or volume publication. See [produced values and expected failures](../docs/expected_results.md).
