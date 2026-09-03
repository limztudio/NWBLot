# NWBLot Third-Party Package Policy

Updated: 2026-09-03

Rules for everything that lives under `3rd_parties/`. These are hard requirements: when a vendored package
violates them, fix the package (re-layout it, rewrite its `CMakeLists.txt`, add the missing metadata) rather
than working around it elsewhere.

## 1. Flatten everything — one package, one top-level directory

- Every third-party package is a single directory directly under `3rd_parties/` (e.g. `3rd_parties/zstd`,
  `3rd_parties/rdf`, `3rd_parties/rmv`). No package nests another package inside itself.
- If a package ships its own bundled dependency (a vendored sub-library, an `external/`, `imported/`,
  `third_party/`, `3rdparty/`, or `single_include/` copy), that dependency is **lifted out** and vendored as
  its own independent top-level package. The consuming package then depends on the shared top-level copy.
  - Example: rdf used to bundle zstd under `rdf/imported/zstd` and the library itself under `rdf/rdf`. Both
    were flattened: zstd became `3rd_parties/zstd`, and the rdf library moved up to `3rd_parties/rdf`.
- Do not keep two copies of the same dependency. When two packages each bundle the same library, vendor it
  once at top level and point both at it.
  - Example: the engine's `nlohmann_json` and a second `json/single_include/nlohmann/json.hpp` (both v3.9.1)
    were merged into the single `3rd_parties/nlohmann_json` (`nwb::nlohmann_json`); consumers include
    `<nlohmann/json.hpp>`.
- When the duplicated copies are at **different versions, keep the latest one** as the single shared package
  and delete the older copies. If a consumer was written against an older copy and does not build against the
  newer one, fix that consumer (update its includes / adjust its code) to fit the retained latest version —
  do not re-introduce an older duplicate to avoid the work.
- Rationale: a flat layout makes the real dependency set visible, removes duplicate/divergent copies, and
  lets a single update of a shared library reach every consumer.

### 1.1 Build wiring for a flattened package
- Register the package with `add_subdirectory(<name>)` in `3rd_parties/CMakeLists.txt`. Add a dependency
  before the packages that consume it (e.g. `zstd` before `rdf`).
- Editing or fully rewriting a vendored `CMakeLists.txt` is allowed and expected — drop upstream
  `FetchContent`/download steps (the build is offline/vendored), strip `-Werror`-style options that fight the
  engine toolchain, and fix include paths to the flattened layout.
- Suppress vendor warnings with `nwb_disable_vendor_warnings(<target>)`.
- C/C++ static libs linked into engine targets must match the engine's per-config CRT and
  `_ITERATOR_DEBUG_LEVEL`: add the target to the `nwb_match_engine_runtime` list in `3rd_parties/CMakeLists.txt`
  (otherwise lld-link rejects the mix via `/failifmismatch`).
- Prefer building vendored libraries `STATIC` so nothing has to deploy a vendored `.dll` next to an executable
  (e.g. amdrdf is forced static via `RDF_BUILD_STATIC=1`).

## 2. Watch the official upstream — never a nested snapshot

Flattening (section 1) does not change **who we watch**. After a nested copy is lifted to `3rd_parties/<name>/`,
that package tracks the library's own official repository, release archive, or SDK. It must not keep watching
the parent project's `third_party/`, `3rdparty/`, `external/`, or `imported/` snapshot.

- `source:` in `nwb_update.txt` is the official origin of **this** package, not the repo we first noticed it in.
  - Example: TinyEXR was first lifted from Basis Universal `encoder/3rdparty`. It now tracks
    https://github.com/syoyo/tinyexr, not BinomialLLC/basis_universal.
  - Example: QOI tracks https://github.com/phoboslab/qoi. tinydds tracks https://github.com/DeanoC/tiny_dds.
    RapidJSON tracks https://github.com/Tencent/rapidjson. zstd tracks https://github.com/facebook/zstd.
- If the official copy and a nested snapshot differ, take the official copy. Re-apply only NWB-local patches
  the engine still needs (toolchain, ARM64, documented correctness). Record those patches in `notes:`.
- If a nested file has no independent official repo — it is owned by the parent project — the parent project's
  official repo is the source. Example: `dev_driver` `rgdevents.h` is an RGD header, so GPUOpen-Tools/radeon_gpu_detective
  is official for that package.
- Binary SDKs (Nsight Aftermath, COMGR runtime archives) use the vendor's official SDK or release, not a copy
  bundled inside a different tool.

## 3. Every package carries an `nwb_update.txt`

- Each top-level package directory contains a file named `nwb_update.txt` recording when it was last updated
  and where it came from, so the next update is a mechanical re-fetch.
- Required fields (one `key: value` per line):
  - `package:` — the package name (matches the directory name).
  - `version:` — released version, git tag, or commit hash that was vendored.
  - `updated_utc:` — the vendoring date/time in UTC, ISO-8601 (`YYYY-MM-DDTHH:MM:SSZ`); a bare date
    (`YYYY-MM-DD`) is acceptable when only the day is known.
  - `source:` — the official upstream URL the package was fetched from (release archive, repo URL, SDK page).
    Do not point this at another project's nested copy.
- Optional fields:
  - `notes:` — anything done while vendoring that a re-fetch must reproduce: files pruned, sub-dependencies
    lifted out, CMake rewritten, headers patched, runtime libs deployed separately, etc.
- Keep `nwb_update.txt` in plain ASCII. Update it whenever the package is re-vendored or modified.

### 3.1 Format example
```
package: zstd
version: 1.5.7
updated_utc: 2026-06-23T16:02:45Z
source: https://github.com/facebook/zstd/releases/tag/v1.5.7
notes: Zstandard. Built STATIC from upstream single-file amalgamation. Only inc/ + src/zstd.c are used. Originally flattened out of rdf/imported; subsequent updates come from facebook/zstd.
```
