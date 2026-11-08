# fbx_to_nwb

`fbx_to_nwb` brings an FBX scene into NWB mesh data. It can write a plain
mesh, a full asset bunch, a split model pack, or the bone and skin files for
a skinned mesh. It can also refresh old NWB mesh files into the plain stream
form.

The exe target is `nwb_fbx_to_nwb`. The tool takes the binary and text FBX
forms that the kept ufbx loader reads.

## Build and run

The folder launcher is found by the repo root. It builds the target as needed
and sends flags after `--`:

```sh
python -m launcher fbx-to-nwb --build-only --config dbg
python -m launcher fbx-to-nwb --working-directory . -- --help
python -m launcher fbx-to-nwb --working-directory . -- assets/models/crate.fbx
```

The build-only form readies the tool with no asset work. See [the launcher
guide](../../launcher/README.md) for setup and build flags. The last form
starts the ask-me flow. For CI and asset pipes, pass `--yes` so each unset
flag uses its default with no ask:

```sh
python -m launcher fbx-to-nwb --working-directory . -- \
    assets/models/crate.fbx \
    --output assets/models/crate.nwb \
    --asset-type bunch \
    --normal-mode imported \
    --yes
```

With no `--output`, the out path is the in path with its tail set to `.nwb`.
Old main out files never swap unless `--force` is set or an ask-to-swap
prompt is said yes to.

## Find and pick meshes

List the seen mesh copies in a scene before you send one out:

```sh
python -m launcher fbx-to-nwb --working-directory . -- assets/characters/hero.fbx --list-meshes
```

`--mesh` (or `-m`) picks `all` copies by default. It also takes `first`, a
zero-based copy index, a node name, or a mesh name. Name match ignores case.
An exact match wins. If not, a part match picks each match copy. Hid mesh
nodes stay out unless `--include-hidden` is set.

For example, this writes one still mesh file from the first copy:

```sh
python -m launcher fbx-to-nwb --working-directory . -- \
    assets/props/crate.fbx \
    --output assets/meshes/crate.nwb \
    --asset-type mesh \
    --mesh first \
    --yes
```

Picked copies join into one out mesh. Flat faces turn into tris, and bad tris
drop. The default `--triangle-area-length-squared-epsilon` is `1e-20`. Set it
to a larger real value at zero or more to drop smaller tris.

## Axes, points, and tangents

By default, the tool moves the scene into NWB space: `+X` right, `+Y` up,
`+Z` front, with one unit as one metre. `--preserve-space` keeps the source
axes and units. Node moves bake into the out mesh by default. Pass `--local`
to keep the picked mesh in its own space. `--scale` adds one more good same
scale after import.

`--normal-mode` picks one of these modes:

| Value | Result |
| --- | --- |
| `imported` (default) | Uses the brought-in per-corner normal list. |
| `smooth` | Remakes smooth normals shared by same out spots. |
| `regenerate` | Remakes one face normal per tri. |

NWB mesh files always hold a tangent list. Source tangents stay when they are
full and `imported` normals are set. If not, tangents remake from `uv0`.
Missing or bad UVs use a safe backup tangent set. The tool prints which
tangent path it took.

`uv0` comes in when present, or zero when not. Point colors come in by
default. Use `--ignore-colors` to force the `--default-color R,G,B,A` value
instead (default is `1,1,1,1`). `--flip-winding` swaps the second and third
point of each out tri.

## Out forms

`--asset-type` takes `bunch`, `mesh`, `model`, `skeleton`, and `skin`.

| Type | Files made | Use |
| --- | --- | --- |
| `bunch` (default) | One `.nwb` file with a mesh and model, plus bone and skin data when needed. | A full asset-bunch bring-in. |
| `mesh` | One mesh `.nwb` file. | A plain mesh pack. Skin and bone data do not go out. |
| `model` | One model `.nwb` file with links to pack files. | Use when the linked mesh, skin, and bone files are there. |
| `skeleton` | One bone `.nwb` file. | A skinned source only. |
| `skin` | One skin `.nwb` file with mesh and bone links. | A skinned source only. Linked files do not go out. |

`--asset-type` takes the five values in this table. The `asset_bunch` data
note is a source-word form. Use `bunch` for the CLI out type.

For model, skin, and split-pack outs, made file paths come from the out path.
For example, an out of `assets/characters/hero.nwb` uses
`project/characters/hero` as the default base. Use `--virtual-root engine`
to use `engine/...` instead. If the out path sits outside an `assets` folder,
its stem goes under that base.

`--separate-assets` works only with `--asset-type bunch`. It does not write
one asset bunch. It writes the model to the asked out file and its child
files in a folder named for the out stem:

```text
assets/characters/hero.nwb
assets/characters/hero/mesh.nwb
assets/characters/hero/skeleton.nwb  # skinned sources only
assets/characters/hero/skin.nwb      # skinned sources only
```

For `bunch`, `model`, `skeleton`, and `skin` outs, one source pick cannot mix
still and skinned meshes. `skeleton` and `skin` need a skinned pick.

## Refresh an NWB mesh file

A `.nwb` input auto starts refresh mode. `--refresh-nwb` can also force it.
Refresh reads each mesh note, checks its known mesh fields, makes plain and
same known point lists, then rewrites only changed lists and keeps the rest
of the file. Point list sameness uses the set number out rule: minus zero
and sizes below `1e-8` share the shown zero value. Refresh re-links their
point links as one, so a repeat refresh keeps the same plain bytes. Skinned
refresh keeps the first spot and skin pull pair and leaves the pull list text
as is.

To make a plain copy:

```sh
python -m launcher fbx-to-nwb --working-directory . -- \
    assets/meshes/crate.nwb \
    --output assets/meshes/crate_canonical.nwb \
    --yes
```

To swap the input in place, say yes to the swap in a clear way:

```sh
python -m launcher fbx-to-nwb --working-directory . -- \
    assets/meshes/crate.nwb \
    --yes --force
```

Refresh refuses a file with unknown mesh fields or bad lists. It never bakes
a new mesh from FBX data in refresh mode. Bone and skin notes stay byte-same
unless their mesh links change.
