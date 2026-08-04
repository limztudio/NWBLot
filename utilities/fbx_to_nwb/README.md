# fbx_to_nwb

`fbx_to_nwb` imports an FBX scene into NWB mesh metadata. It can write a
standalone mesh, a self-contained asset bunch, a separately packaged model, or
the skeleton and skin assets for a skinned mesh. It also refreshes existing NWB
mesh assets into the canonical stream layout.

The executable target is `nwb_fbx_to_nwb`. The tool accepts the binary and
ASCII FBX variants supported by the vendored ufbx loader.

## Build and run

The directory launcher is discovered by the repository root, builds the target
as needed, and forwards arguments after `--`:

```sh
python launcher.py fbx-to-nwb -- --help
python launcher.py fbx-to-nwb -- assets/models/crate.fbx
```

The second command starts the interactive workflow. For CI and asset pipelines,
pass `--yes` so that every unspecified option uses its default instead of
prompting:

```sh
python launcher.py fbx-to-nwb -- \
    assets/models/crate.fbx \
    --output assets/models/crate.nwb \
    --asset-type bunch \
    --normal-mode imported \
    --yes
```

Without `--output`, the output path is the input path with its extension
replaced by `.nwb`. Existing primary output files are never replaced unless
`--force` is supplied or an interactive overwrite prompt is accepted.

## Finding and selecting meshes

List the visible mesh instances in a scene before exporting one:

```sh
python launcher.py fbx-to-nwb -- assets/characters/hero.fbx --list-meshes
```

`--mesh` (or `-m`) selects `all` instances by default. It also accepts `first`,
a zero-based instance index, a node name, or a mesh name. Name matching is
case-insensitive; an exact match wins, otherwise a partial match selects every
matching instance. Hidden mesh nodes are excluded unless `--include-hidden` is
present.

For example, this writes a single static mesh asset from the first instance:

```sh
python launcher.py fbx-to-nwb -- \
    assets/props/crate.fbx \
    --output assets/meshes/crate.nwb \
    --asset-type mesh \
    --mesh first \
    --yes
```

Selected instances are merged into one exported mesh. Polygon faces are
triangulated and degenerate triangles are discarded. The default
`--triangle-area-length-squared-epsilon` is `1e-20`; set it to a larger
non-negative finite value to discard smaller triangles.

## Coordinate, vertex, and tangent handling

By default, the importer converts the scene to NWB space: `+X` right, `+Y` up,
`+Z` forward, with one unit equal to one metre. `--preserve-space` retains the
source axes and units. Node transforms are baked into the output mesh by
default; pass `--local` to keep the selected mesh in its local space. `--scale`
applies an additional positive uniform scale after import.

`--normal-mode` selects one of these modes:

| Value | Result |
| --- | --- |
| `imported` (default) | Uses the imported per-corner normal stream. |
| `smooth` | Rebuilds smooth normals shared by identical output positions. |
| `regenerate` | Rebuilds a face normal for every triangle. |

NWB mesh assets always contain a tangent stream. Source tangents are retained
when they are complete and `imported` normals are selected; otherwise tangents
are regenerated from `uv0`. Missing or degenerate UVs use a safe fallback
tangent frame. The tool reports which tangent path it used.

`uv0` is imported when available and otherwise defaults to zero. Vertex colors
are imported by default; use `--ignore-colors` to force the
`--default-color R,G,B,A` value instead (the default is `1,1,1,1`).
`--flip-winding` swaps the second and third vertex of every output triangle.

## Output layouts

`--asset-type` accepts `bunch`, `mesh`, `model`, `skeleton`, and `skin`.

| Type | Files written | Intended use |
| --- | --- | --- |
| `bunch` (default) | One `.nwb` file containing a mesh and model, plus skeleton and skin data when needed. | A self-contained asset-bunch import. |
| `mesh` | One mesh `.nwb` file. | A standalone mesh payload. Skin and skeleton data are not emitted. |
| `model` | One model `.nwb` file with references to package assets. | Use when the referenced mesh, skin, and skeleton assets already exist. |
| `skeleton` | One skeleton `.nwb` file. | A skinned source only. |
| `skin` | One skin `.nwb` file with mesh and skeleton references. | A skinned source only; referenced assets are not written. |

For model, skin, and separate-package output, generated asset paths
are derived from the output path. For example, an output of
`assets/characters/hero.nwb` uses `project/characters/hero` as the default
virtual base. Use `--virtual-root engine` to use `engine/...` instead. If the
output path is outside an `assets` directory, its stem is used beneath that
virtual root.

`--separate-assets` is valid only with `--asset-type bunch`. Instead of writing
one asset bunch, it writes the model to the requested output and its child
assets in a directory named after the output stem:

```text
assets/characters/hero.nwb
assets/characters/hero/mesh.nwb
assets/characters/hero/skeleton.nwb  # skinned sources only
assets/characters/hero/skin.nwb      # skinned sources only
```

For `bunch`, `model`, `skeleton`, and `skin` output, a source selection cannot
mix static and skinned meshes. `skeleton` and `skin` specifically require a
skinned selection.

## Refreshing an NWB mesh asset

Passing a `.nwb` input automatically enters refresh mode; `--refresh-nwb` can
also force it. Refresh reads every mesh declaration, validates its supported
mesh fields, canonicalizes and deduplicates its streams (and associated skin
influences), then rewrites only changed lists while preserving the rest of the
document.

To make a canonical copy:

```sh
python launcher.py fbx-to-nwb -- \
    assets/meshes/crate.nwb \
    --output assets/meshes/crate_canonical.nwb \
    --yes
```

To replace the input in place, explicitly acknowledge the overwrite:

```sh
python launcher.py fbx-to-nwb -- \
    assets/meshes/crate.nwb \
    --yes --force
```

`--list-meshes` is only available for FBX input. Refresh rejects unsupported
mesh fields rather than silently dropping them.

## Command-line reference

| Option | Description |
| --- | --- |
| `input` | FBX input path, or an NWB mesh path for refresh mode. |
| `-o, --output PATH` | Primary output `.nwb` path. |
| `--asset-type TYPE` | `bunch`, `mesh`, `model`, `skeleton`, or `skin`. |
| `--virtual-root ROOT` | Virtual root for generated package references; defaults to `project`. |
| `-m, --mesh SELECTOR` | `all`, `first`, an index, a node name, or a mesh name. |
| `--normal-mode MODE` | `imported`, `smooth`, or `regenerate`. |
| `--scale VALUE` | Positive finite uniform scale, applied after import. |
| `--triangle-area-length-squared-epsilon VALUE` | Non-negative threshold for discarding degenerate triangles. |
| `--default-color R,G,B,A` | Fallback color, four finite values. |
| `--preserve-space` | Do not convert axes or source units to NWB space. |
| `--include-hidden` | Include hidden FBX mesh nodes. |
| `--local` | Do not bake node transforms into the output mesh. |
| `--ignore-colors` | Use the default color even if the source has vertex colors. |
| `--flip-winding` | Reverse every output triangle's winding. |
| `--separate-assets` | Write a bunch as a model plus child `.nwb` files. |
| `--refresh-nwb` | Treat the input as an NWB mesh asset to canonicalize. |
| `--list-meshes` | Print importable FBX mesh instances and exit. |
| `--force` | Allow replacement of an existing primary output file. |
| `-y, --yes` | Use defaults for omitted import options and disable prompts. |
| `-h, --help` | Show the executable's generated help text. |
