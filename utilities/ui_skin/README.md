# UI skin generator

The utility owns `launch.py` and is discovered as `ui-skin` by the repository launcher.
Run it from the repository root to regenerate the default artwork and atlas metadata:

```powershell
python -m launcher ui-skin --help
python -m launcher ui-skin
```

The default output directory is `impl/assets/ui/skins/default`. Use `--directory` to select another output directory:

```powershell
python -m launcher ui-skin -- --directory "path/to/skin"
```

This Python generator writes `source.png` and `atlas.nwb`. To convert the generated artwork into the runtime texture
with automatic converter configuration and compilation, use the native utility launcher:

```powershell
python -m launcher tex-conv --config opt --working-directory . -- impl/assets/ui/skins/default/source.png --output impl/assets/ui/skins/default/texture --force
```

The converter's `--force` option replaces the existing texture pair. The generated intermediate `source.png` is omitted
from the repository and is not required at runtime. The [default skin guide](../../impl/assets/ui/skins/default/README.md)
describes the asset contract, named regions, and replacement skins.

If a converter is already built, the generator can invoke it during generation with `--tex-conv`:

```powershell
python -m launcher tex-conv --build-only --arch arm64 --config opt
python -m launcher ui-skin -- --tex-conv __exec/windows/arm64/full/opt/tex_conv.exe
```

The path above uses Windows ARM64, the full domain, and the optimized configuration; select the matching path for another
platform, architecture, or domain. The generator's options are shown by `python -m launcher ui-skin --help`.
Native build options belong to `tex-conv`; the Python generator accepts `--directory` and `--tex-conv`.
