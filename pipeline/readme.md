# Asset pipe

The pipe has three free exe files and one Python run-all. Build the
`nwb_pipeline` CMake target to make `dependency_computer`, `asset_builder`,
and `asset_gatherer` (with `.exe` on Windows). `NWB_BUILD_PIPELINE` turns
these targets and the asset build libs on or off.

Run the full pipe through the repo launcher:

```console
python -m launcher pipeline --config dbg --asset-root impl/assets project/assets --output-directory runtime/res
```

To ready the tools with no asset steps, use:

```console
python -m launcher build nwb_pipeline --config dbg -D NWB_BUILD_PIPELINE=ON
```

Both forms set up and build on their own. See [the launcher
guide](../launcher/README.md) for chip, form, plain build folders, and dry
previews.

The root finds `pipeline/launch.py` as a plain run folder. Build and asset
flags use one flag list. The pipe launcher sets up when needed with
`NWB_BUILD_PIPELINE=ON`, builds `nwb_pipeline`, and finds all three tools
from the picked CMake form. Setup, build, and pipe faults keep their bad exit
code at the root launcher.

`dependency_computer` finds full `.nwb` asset needs. With no set `--input`
or `--input-list`, it auto finds `project.nwb` in non-engine `--asset-root`
folders and grows the need graph from the project `start_scene` through scene
parts, auto-on feature parts, and reached files with `CookDependencyRegistry`.
With set `--input` or `--input-list` values, it keeps those seed inputs and
grows their full needs through `--asset-root` stores. That fix-up takes
`--repo-root` (default is the work folder) and repeat `--asset-root` values.
Its store list fixes data IDs with no read of idle asset papers, and store
rank uses asset-root order. Bad, wrong-kind, or two-way asset links fail
before the out list is made. Paired coded texture bytes stay with the texture
cooker and do not join as split inputs. Its `--output` is a UTF-8 file with
one fixed store path per line.

```console
python -m launcher run nwb_dependency_computer --config dbg --working-directory . -- --repo-root . --asset-root impl/assets project/assets --output dependencies.list
```

`asset_builder` turns picked `.nwb` files into plain run binary packs. It
takes files or folders as inputs. `--asset-root` gives the roots used to fix
links and shader takes. Roots under `impl` use the `engine/` base name, and
other roots use `project/`. When left out, the maker finds the near `assets`
parent of each input, or uses the input parent folder. Inputs must sit in one
of the given or found roots. The maker only reads picked inputs. It does not
grow their needs. Linked files needed by data checks must be in the inputs.

```console
python -m launcher run nwb_asset_builder --config dbg --working-directory . -- --input-list dependencies.list --repo-root . --asset-root project/assets --output-directory built --configuration dbg
```

The out folder holds `.nwba` files and `assets.list`. Each pack holds an
88-byte head then the exact run codec bytes. The head holds a 32-bit mark
(`0x4142574e`), 32-bit form (`1`), 64-byte base-path `NameHash`, 64-bit byte
size, and 64-bit FNV byte check. These fields use the engine plain
little-endian byte form. The pick-up tool checks the head, size, check, and
base ID before it packs the bytes.

Pack names join a short hex slot with byte and cook hashes. The maker reuses
byte-same packs and moves past full slots with changed stuff. It keeps packs
named by an old list. `assets.list` holds short file names for only the
current good build. It goes out after all packs are made, so failed builds
leave the old list whole, and dropped inputs do not come back through old
files. Old stuff files may stay in the build folder. Folder pick-up uses the
current list. The full build folder can move to another box or path with no
source files or compiler store.

`asset_gatherer` takes `.nwba` files, build out folders, or an `--input-list`.
Paths in a pick-up list fix against that list folder. Folders with
`assets.list` use that list. Other folders scan for `.nwba` files. Same base
ID and bytes join. The graphics data join hook joins shader store index rows
from free builds. Clashing rows or other changed bytes for the same ID fail.
Pick-up does no source read or build. It puts out the set `graphics` pack
form, with the default pack code through `IFilesystem`. Check and pack faults
before put-out leave the old pack in place. If a put-out fails, bring-back is
tried. A half bring-back can leave the live pack half done and keeps the
stage and copy folders for rescue.

```console
python -m launcher run nwb_asset_gatherer --config dbg --working-directory . -- --input built-a built-b --output-directory runtime/res
python -m launcher run nwb_asset_gatherer --config dbg --working-directory . -- --input-list built/assets.list --output-directory runtime/res
```

`pipeline/launch.py` shows in the root launcher as the `pipeline` call, with
the same form as the repo other run folders. It checks asset roots and opt
input swaps, sets up and builds `nwb_pipeline`, calls `dependency_computer`,
sends that result to `asset_builder`, then sends the maker list to
`asset_gatherer`. Find faults and failed steps stop the pipe. A failed
put-out uses the rescue rules above.

```console
python -m launcher pipeline --config dbg --asset-root impl/assets project/assets --output-directory runtime/res
```

Build and asset flags share one flag list. Use the root door for the flow and
help. Help ends with no build:

```console
python -m launcher pipeline --help
```

The launcher reuses the repo `--config`, `--arch`, `--domain`,
`--configure-preset`, `--build-dir`, `--jobs`, and other build flags. The
repo root defaults to the repo with the script. Asset flags `--repo-root`,
`--asset-root`, `--output-directory`, `--configuration`, and
`--asset-type graphics` stay open. `--configuration` swaps the asset form
name. If not, it tracks `--config`. No `--asset-type` picks `graphics`. Set
blank or bad values fail. `--skip-build` uses old tools with no build.
`--dry-run` prints the tool calls with no run. Asset roots must live and sit
in the repo root. The out folder is made as needed.
