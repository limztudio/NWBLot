# UI skin maker

This tool owns `launch.py`. The repo launcher finds it as `ui-skin`. Run it
from the repo root to remake the default art and atlas data:

```powershell
python -m launcher ui-skin --help
python -m launcher ui-skin
```

The default out folder is `impl/assets/ui/skins/default`. Use `--directory`
to pick another out folder:

```powershell
python -m launcher ui-skin -- --directory "path/to/skin"
```

This Python script writes `source.png` and `atlas.nwb`. The atlas uses
current plain `ui_skin` data and `asset.toolkit_contract = "widgets";` to
check the full stock widget region set. Authors keep no schema or version
numbers. To turn the made art into the run-time texture with auto converter
setup and build, use the native tool launcher:

```powershell
python -m launcher tex-conv --config opt --working-directory . -- impl/assets/ui/skins/default/source.png --output impl/assets/ui/skins/default/texture --force
```

The converter `--force` flag swaps the old texture pair. The made
`source.png` stays out of the repo and is not needed at run time. The
[default skin guide](../../impl/assets/ui/skins/default/README.md) states the
asset rules, named parts, and swap skins.

If a converter is built, the maker can call it while it runs with
`--tex-conv`:

```powershell
python -m launcher tex-conv --build-only --arch arm64 --config opt
python -m launcher ui-skin -- --tex-conv __exec/windows/arm64/full/opt/tex_conv.exe
```

The path above uses Windows ARM64, the full set, and the opt build. Pick the
matching path for another platform, chip, or set. The maker flags show with
`python -m launcher ui-skin --help`. Native build flags belong to `tex-conv`.
The Python maker takes `--directory` and `--tex-conv`. The maker stages its
outs and brings back old files if publish fails. It prints the rescue folder
if roll-back cannot end. Publish of many files is not safe from kill or other
readers/writers. On POSIX, old owner, group, and mode bits move to new files.
Publish fails if they cannot be set. ACLs and extra attrs are not kept by
this maker.
