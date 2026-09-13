# Frozen caustic resolve reference

`caustic_resolve_cs.slang` is a byte-for-byte copy of `impl/assets/graphics/caustic/caustic_resolve_cs.slang` before the Step 17 cached-center/right-coordinate proposal. SHA256: `0dd13b85a913ce01449c7bce0b3bc21d3605e6897d137c4181fd2580bf612574`.

It is the complete production entry source, including prepare, wavelet and upsample branches; no test shader or copied CPU wavelet algorithm replaces it. The native loader cooks this source with the production `ShaderCook`, the actual production `.nwb` metadata, the same production includes and entry `main`. The active source is cooked separately. The only selected runtime stage in these tests is WAVELET (1), at steps 1, 2 and 4. Reference includes must remain identical between the compared source snapshots; freeze their identities together with the source and cooker.

This reference is an independent frozen baseline for exact RGBA16 output parity. It is not evidence of CPU, native or GPU test execution. Do not update it to a candidate merely to make a comparison pass.
