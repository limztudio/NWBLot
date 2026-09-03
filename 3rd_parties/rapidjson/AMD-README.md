# RapidJSON (NWB)

Official source: [Tencent/rapidjson](https://github.com/Tencent/rapidjson) tag `v1.1.0`.

Vendored as `include/rapidjson/` plus `license.txt`. NWB CMake exposes `nwb::rapidjson`.

NWB-local patches on top of v1.1.0 (re-apply after a re-fetch):

- `rapidjson.h`: treat MSVC ARM64 (`_M_ARM64`) as little-endian, not only `_M_ARM`.
- `document.h`: `GenericStringRef` assignment uses destructor + placement new because `s`/`length` are const.
- `document.h`: `static_cast<int>` on mixed enum bitwise flags for MSVC.
- `internal/biginteger.h` and `internal/diyfp.h`: ARM64EC `_umul128` uses `softintrin` instead of the x64 intrinsic.
