# Shader assets

Declare a shader by its physical stage and pair the `.nwb` file with a same-stem `.slang` source. For example, `filter.nwb` and `filter.slang` define one compute shader:

```cpp
compute_shader asset;

asset.entry_point = "main";
```

The declaration supplies the stage. The retired generic `shader` declaration and `asset.stage` field are rejected, even if the field repeats the concrete type's stage. Rebuild cooked assets after migrating metadata.

| Authored type | Runtime type | Physical stage |
| --- | --- | --- |
| `vertex_shader` | `VertexShader` | `Vertex` |
| `hull_shader` | `HullShader` | `Hull` |
| `domain_shader` | `DomainShader` | `Domain` |
| `geometry_shader` | `GeometryShader` | `Geometry` |
| `pixel_shader` | `PixelShader` | `Pixel` |
| `compute_shader` | `ComputeShader` | `Compute` |
| `amplification_shader` | `AmplificationShader` | `Amplification` |
| `mesh_shader` | `MeshShader` | `Mesh` |
| `ray_generation_shader` | `RayGenerationShader` | `RayGeneration` |
| `any_hit_shader` | `AnyHitShader` | `AnyHit` |
| `closest_hit_shader` | `ClosestHitShader` | `ClosestHit` |
| `miss_shader` | `MissShader` | `Miss` |
| `intersection_shader` | `IntersectionShader` | `Intersection` |
| `callable_shader` | `CallableShader` | `Callable` |

`entry_point` retains exact, case-sensitive spelling. Optional fields are `include_roots`, `defines`, integer `ray_query` (0 or 1), and `optimization_level` (`none`, `default`, `high`, or `maximal`). Mesh shaders additionally accept integer `emit_mesh_compute_shadow` (0 or 1, default 1); the fixed shared engine mesh requires 1. Identity, source path, physical stage, archive role, backend profiles, fixed auxiliary filenames, and payload versions remain derived or engine-owned. Reserved engine transport defines such as `NWB_BINDLESS_TLAS` belong in shader source and are rejected in authored define sets. Define names must be ASCII identifiers; values cannot contain semicolons, NUL/CR/LF, or leading/trailing whitespace because variant signatures must round-trip exactly.

`interface IShader` owns common entry-point and bytecode storage. Its final concrete types share the typed implementation and expose their physical stage as `static constexpr s_Stage`. Each type has a registered codec such as `ComputeShaderAssetCodec` or `PixelShaderAssetCodec`; serialization, payload layout, compiler invocation, variants, and dependency handling remain shared. Concrete types add no per-object stage storage or bytecode copies.

Metadata discovery parses each metascript document and passes it to `ShaderCook` for shader/include validation. `ShaderCook` owns metadata, define variants, dependencies, and checksums. Shader volume writing calls `SlangShaderCompiler::ComputeCompilerFingerprint` and the single `SlangShaderCompiler::CompileVariant` implementation directly. The separate `ShaderSourceDependencies` module owns source-line splicing, comment masking, `ExtractIncludeDirective`, and `ResolveIncludeFile` for conservative dependency planning. There is no compiler interface, factory, injection path, or production file-parsing convenience overload. All 14 physical stages remain supported through the compiler and archive mappings.

Shader-volume cooking computes a fingerprint of the configured compiler executable/version and shared flags, capabilities, stage mappings, and optimization options once per nonempty append. That fingerprint participates in each source checksum. The cache stores one `<16-hex-source-checksum>.spv` record under the cache/configuration directory, without a paired checksum file. A cache hit must satisfy the same complete SPIR-V, exact entry-point, and physical-stage validation as freshly compiled output; a malformed or wrong-stage record is rebuilt. Each compilation uses an isolated temporary directory and promotes its output atomically after required diagnostic and bytecode checks.

Dependency discovery ignores commented directives and resolves literal includes across escaped line endings. Unplanned macro includes, `#include_next`, and `#import` are rejected so cache keys cannot omit compiler inputs. The existing generated CSG evaluator macro is explicitly declared by its preparation caller, which separately tracks its generated source. Language-level Slang import dependency discovery is not implemented; current first-party shaders use includes.

Dependency checksums also record whether any compiler input has a UTF-8 BOM. Inputs without BOMs compile directly. BOM inputs use a stripped overlay that preserves nested relative includes and encodes each absolute root into a safe directory component. Temporary source overlays and diagnostics are cleaned on success and failure.

Use `Core::Assets::AssetRef<ComputeShader>` or the required concrete type for a known stage. `ShaderAssetLoader::Load<ComputeShader>(...)` derives the stage from that type. A heterogeneous material stage array uses `AssetRef<IShader>`, while known AVBOIT pixel bindings use `AssetRef<PixelShader>`. `LoadForStage(...)` handles genuinely dynamic physical-stage requests. `IShader` is abstract and is not registered as a generic loadable asset.

Archive stage names also describe roles. `mesh_compute` selects `ComputeShader` payloads; `mesh_object_vertex` selects a `VertexShader` payload. An explicit archive role selects the lookup key without changing the concrete type's physical stage. Materials continue to author the supported `mesh` and `ps` stage map.

Loading validates the current payload, bounded SPIR-V instruction stream, and exact entry point for the concrete physical stage before publishing the asset's state. A wrong-stage or malformed payload fails without replacing previously loaded entry-point or bytecode storage. Validation reads byte views directly, including unaligned payload offsets, without copying into a temporary word vector. Backend shader-module creation retains its own validation through the selected graphics API. Rendering accesses the admitted common data directly; it adds no per-frame virtual stage dispatch.

Build and cook through the [root launcher](../../launcher/README.md) and [pipeline workflow](../../pipeline/readme.md).
