# Frozen reflection kernel references

`spatial_cs.slang` is the historical production shader frozen by commit `1a00f028e7a4300c70bdb936fc0f7cb0757fa47f` for native output comparisons. Its text exactly matches `impl/assets/graphics/reflection/spatial_cs.slang` immediately before that commit. The current production shader may change; this independent reference preserves the earlier behavior.

Commit `b64479971609b7476aa8262158fcf4b569fe60ef` unintentionally rewrote this reference during a helper-rule cleanup, adding the newer tile/cache implementation and changing accumulation from float to half. Those are behavioral changes to the test oracle, not formatting changes. The reference was restored from its immediate predecessor, `37426cca0aaaf90902bb0eb61242f650c5a2517f`; that version is also identical to the original reference introduced by `1a00f028e7a4300c70bdb936fc0f7cb0757fa47f`.

SHA256 of the restored historical text with LF line endings: `cad2d3a8af03ad004f8bac3e33dec09b6f05381f0e3af534eb0d89ca970a5f16`.

SHA256 of the restored file with the repository's required CRLF line endings: `454dbef87df8f48b7a1d26ef7a3e84df29f93a237032195963e9fed4c740baeb`.

Changes to a frozen reference require an explicit, independently justified change to the reference contract. Production optimization and style sweeps must retain its behavior. The paired native tests compare the current production pipeline against this file; they must not update both sides to the proposed implementation.

`classify_cs.slang` is the production classifier frozen from `e3f8c9a39` before the surface-parallel and group-feedback experiments. The native fixture compares both surfaces, counters, feedback, queue budgets and canonical per-tile candidate order. Workgroup reservations may appear in different orders, as permitted by atomic scheduling. Preserve the reference behavior when changing the production classifier. Its CRLF SHA-256 is `27cffd1a03e54fe8115bcab5e6a26512e71be43bb0a4ed7a551ac5c09c37fb3f`.

`../assets/refraction_hit/reference/surface_hit.slangi` freezes the shared geometric/material hit code from `e79c8cf90`. Native refraction tests use this header independently of the production admission helpers and compare retained hit fields exactly. The test-only material hook records reconstruction calls in both arms; it is absent from production. Its CRLF SHA-256 is `55fa5df747ee0416472dc0bde034d06a6b710c46a02d3c9fd76fbfe5f3f5d268`.

`screen_trace.slangi` freezes the production traversal before the mip-descent position-reuse experiment. The reference classifier resolves this adjacent header, and the direct screen-trace reference wrapper includes it explicitly. Neither reference arm uses the live production traversal. Shared view/surface and depth-reduction ABIs are unchanged. The frozen header CRLF SHA-256 is `0768e0bcf9ac19b79abb3c23fe18eab41257e2f6ddab03eaf6d5ab7c547f4d99`.
