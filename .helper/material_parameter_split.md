# NWBLot Material Parameter Split Specification

Updated: 2026-05-31

## Purpose

Material parameter layout must not be decided by an authored shader alone. The layout is a contract between a material family, its material assets, shader cook, and runtime binding code.

The design goal is to keep the typed, generated Slang accessor path while also splitting data by update frequency. Shaders should read fields such as `surface.base_color`, but the CPU should only upload the smallest block that actually changed.

This document records the current material bind cook/runtime contract plus the remaining runtime split direction. When exact shader binding slots or binary layout matter, verify against `impl/assets/graphics/mesh/runtime.slangi`, `impl/assets_material/`, and `impl/ecs_render/`.

## Current Runtime Contract

The current material cook path resolves `asset.interface` to a parsed `.bind` material interface by virtual path, requires a non-empty explicit `asset.shader_variant`, validates block-scoped `asset.parameters` against the parsed `.bind` schema, serializes deterministic typed layout metadata plus packed typed block bytes, and generates Slang accessors that compile only with `NWB_MATERIAL_TYPED_BINDING=1`.

Mesh material shaders include `mesh/authoring.slangi` and then the generated `.bind` include for the selected interface. The common mesh runtime declares `[[vk::binding(13, 0)]] g_NwbMaterialTypedWords`; bindings 5 and 6 are meshlet descriptor/bounds data, not material typed data. The old shader-side hash lookup helpers and any legacy material binding at slot 5 are removed and should not be reintroduced.

The renderer currently splits packed material bytes into material-constant bytes and material-mutable defaults, applies `MaterialInstanceComponent` overrides only to mutable blocks, deduplicates constant ranges by material/layout and mutable ranges by byte content, and uploads one word-aligned typed-material buffer per material pass. The remaining improvement area is finer GPU update granularity, not the authoring or cook-time typed layout contract.

## Ownership Model

Add a material interface contract as the source of truth for material fields and parameter blocks.

The material interface owns:

- Block names.
- Field names.
- Field types.
- Default values.
- Block class (`material_constant` or `material_mutable`).
- Authored struct and instance names used by generated Slang helpers.
- Packing rules derived and validated by cook.

The material asset owns:

- The selected material interface.
- Stage shader references.
- Shader variant defines.
- Concrete values for interface fields.
- Explicit render policy such as `transparent` and `two_sided`.

The shader asset owns:

- Slang entry point metadata.
- Include roots.
- Shader feature defines.
- Authoring code that consumes the generated interface include.

The runtime renderer owns:

- CPU-side split storage for material-constant bytes and mutable-default bytes.
- `MaterialInstanceComponent` mutable overrides and revision-based mutable-byte caching.
- Descriptor/binding set updates for the shared typed material buffer.
- Per-instance byte offsets into material-constant and material-mutable ranges.

## Block Classes

Parameter blocks are split by update frequency and ownership.

### Static Variant

Static variant fields affect shader compilation. They remain shader defines through `asset.shader_variant`.

Use for:

- Feature switches.
- Shading model branches that should compile away.
- Texture coordinate policy that changes generated code.

Do not store static variant fields in runtime material buffers.

### Material Constant

Material constant blocks are cooked with the material and treated as immutable during normal runtime.

Use for:

- Base color.
- Roughness.
- Metallic.
- Texture transform defaults.
- Other parameters that define the stable material identity.

Recommended storage:

- Store constant bytes with material surface info after load.
- Deduplicate constant byte ranges by material name and typed layout hash during the per-pass typed-material upload.
- Pass the selected constant byte offset through draw push constants.
- A future finer-grained path may move this to persistent rows per material/interface.

### Material Mutable

Material mutable blocks are per-material values that can change at runtime. They must be grouped by fields that normally change together.

Use for:

- Runtime tint.
- Fade alpha.
- Highlight intensity.
- Time-varying material controls shared by all instances using the material.

Recommended storage:

- Mutable default bytes are stored with the material surface info.
- Per-instance mutable overrides are represented by `MaterialInstanceComponent`, must target `material_mutable` blocks, and are cached by entity, material, interface, layout hash, and revision.
- The current renderer deduplicates mutable byte ranges by content before the per-pass typed-material upload.
- A future finer-grained path may move this to dirty tracking per mutable block row instead of rebuilding the pass upload.

### Frame Or Pass

Frame, camera, light, and pass values are not material parameters. Keep them in existing frame/view/pass buffers.

Use for:

- Camera data.
- View projection data.
- Lighting buffers.
- Time values shared by many materials.

## Metadata Shape

The current implemented interface schema is the `.bind` source form described below. A `.nwb` `material_interface` asset is a possible future spelling for the same contract; do not treat the following conceptual block as accepted current metadata.

```text
material_interface asset;

asset.blocks = {
    "surface": {
        "class": "material_constant",
        "slang_type": "NwbProjectBxdfSurfaceMaterial",
        "visibility": ["mesh", "ps"],
        "fields": {
            "base_color": { "type": "float4", "default": "float4(1.0, 1.0, 1.0, 1.0)" },
            "roughness": { "type": "float", "default": "float(0.5)" },
            "metallic": { "type": "float", "default": "float(0.0)" },
        },
    },
    "runtime": {
        "class": "material_mutable",
        "slang_type": "NwbProjectBxdfRuntimeMaterial",
        "visibility": ["mesh", "ps"],
        "fields": {
            "color_tint": { "type": "float4", "default": "float4(1.0, 1.0, 1.0, 1.0)" },
            "fade_alpha": { "type": "float", "default": "float(1.0)" },
        },
    },
};
```

A material asset then supplies values against named blocks instead of one flat generic map.

```text
material asset;

asset.interface = "project/shaders/bxdf_surface";
asset.shaders = {
    "ps": "project/shaders/bxdf_ps",
    "mesh": "project/shaders/bxdf_ms",
};
asset.shader_variant = [
    "NWB_PROJECT_BXDF=1",
    "NWB_PROJECT_TINT=1",
];
asset.parameters = {
    "surface": {
        "base_color": "float4(1.0, 0.22, 0.12, 1.0)",
        "roughness": "float(0.42)",
        "metallic": "float(0.0)",
    },
    "runtime": {
        "color_tint": "float4(1.0, 0.92, 0.85, 1.0)",
        "fade_alpha": "float(1.0)",
    },
};
```

Interface materials must use block-scoped `asset.parameters`; flat parameter keys are rejected for interface-bound materials.

## Bind Source Form

Material interfaces can be authored as `.bind` files if the format is treated as a metascript-backed schema source, not as raw Slang source. The goal is to reuse the existing asset/metascript parsing path instead of adding a separate material-binding language.

The source shape should stay close to Slang so shader authors can read it naturally:

```text
[material_constant]
struct NwbProjectBxdfSurfaceMaterial{
    [default("float4(1.0, 1.0, 1.0, 1.0)")]
    float4 base_color;

    [default("float(0.5)")]
    float roughness;

    [default("float(0.0)")]
    float metallic;
};

[material_mutable]
struct NwbProjectBxdfRuntimeMaterial{
    [default("float4(1.0, 1.0, 1.0, 1.0)")]
    float4 color_tint;

    [default("float(1.0)")]
    float fade_alpha;
};

NwbProjectBxdfSurfaceMaterial surface;
NwbProjectBxdfRuntimeMaterial runtime;
```

The declarations above define the interface schema. They do not directly declare shader globals, descriptor bindings, or runtime storage. Cook parses the `.bind` file, validates the attributes and field types, assigns block classes, then generates Slang-compatible include content containing layout constants, typed structs, and accessor helpers that use the common mesh material accessor surface.

Authored shaders may include the schema path as the stable authoring hook:

```slang
#include "project/shaders/bxdf_surface.bind"
```

During shader cook, the include resolver maps that `.bind` include to generated Slang-compatible content for the parsed schema while preserving the authored include spelling. Prefer a virtual include mapping or generated include path remap over editing the shader source on disk. The shader source remains stable, while the Slang compiler sees the generated material ABI.

The `.bind` file must be a dependency of the generated Slang include and every consuming shader. Changing a field, default, block class, attribute, or instance declaration must invalidate the generated include and the compiled shader bytecode.

## Authoring-Time Contract

Generated Slang types are cook products, but their schema is not discovered after shader authoring. The `.bind` material interface source is the authoring-time schema.

The authoring flow is:

1. Create or update a `.bind` material interface source.
2. Run the material-bind generation step as an early cook phase.
3. Include `mesh/authoring.slangi`, then include the stable `.bind` virtual path for that interface.
4. Compile the shader only after the generated include content is available.

Shader authors therefore know field names from the interface source, not from a material instance and not from shader reflection. If the generated include is missing or stale, shader cook must regenerate it before Slang compilation. Editor tooling should also regenerate interface includes on interface save or before shader language-service indexing.

The generated include path must be deterministic from the material interface identity. Current cook output uses the configured cache root plus `material_bind_includes/<interface>.bind`, for example `cache/<config>/material_bind_includes/project/shaders/bxdf_surface.bind`. The exact physical path can vary by cache/configuration, but the virtual include identity must stay stable enough for shader dependency tracking.

Authored shader source should include the stable `.bind` virtual path directly after `mesh/authoring.slangi`. The cook pipeline supplies the generated content behind that include path instead of mutating the shader source on disk.

The authored shader can reference `#include "xxx.bind"`, but shader cook must resolve that request to generated Slang-compatible content before invoking Slang compilation. The `.bind` text is parsed by NWBLot tooling and should not be forwarded as arbitrary global Slang declarations.

There are only three valid models:

- Interface-first typed model: shaders see named fields after interface generation. This is the implemented model.
- Fixed engine ABI model: shaders see only built-in structs with engine-defined fields.
- Dynamic lookup model: shaders keep using hashed or indexed lookup helpers and do not see typed fields.

Typed arbitrary fields require the interface-first model. Cook-time generation cannot create new names for shader code unless those names already came from an authored schema that shader compilation consumes.

## Generated Slang Shape

Shader cook generates one Slang include per material interface before compiling shaders that consume the interface. The generated file is a dependency of every consuming shader.

Generated include example:

```slang
#ifndef NWB_GENERATED_MATERIAL_BIND_PROJECT_SHADERS_BXDF_SURFACE_BIND
#define NWB_GENERATED_MATERIAL_BIND_PROJECT_SHADERS_BXDF_SURFACE_BIND

#ifndef NWB_MATERIAL_TYPED_BINDING
#error "generated material bind includes require mesh/authoring.slangi"
#endif

#if NWB_MATERIAL_TYPED_BINDING != 1
#error "generated material bind accessors require NWB_MATERIAL_TYPED_BINDING=1"
#endif

struct NwbProjectBxdfSurfaceMaterial{
    float4 base_color;
    float roughness;
    float metallic;
};

struct NwbProjectBxdfRuntimeMaterial{
    float4 color_tint;
    float fade_alpha;
};

static const uint NWB_MATERIAL_BIND_STORAGE_CONSTANT = 1u;
static const uint NWB_MATERIAL_BIND_STORAGE_MUTABLE = 2u;
static const uint NWB_MATERIAL_BIND_SURFACE_STORAGE = 1u;
static const uint NWB_MATERIAL_BIND_RUNTIME_STORAGE = 2u;
static const uint NWB_MATERIAL_BIND_SURFACE_BASE_COLOR_BYTE_OFFSET = 0u;
static const uint NWB_MATERIAL_BIND_SURFACE_ROUGHNESS_BYTE_OFFSET = 16u;
static const uint NWB_MATERIAL_BIND_SURFACE_METALLIC_BYTE_OFFSET = 20u;
static const uint NWB_MATERIAL_BIND_RUNTIME_COLOR_TINT_BYTE_OFFSET = 0u;
static const uint NWB_MATERIAL_BIND_RUNTIME_FADE_ALPHA_BYTE_OFFSET = 16u;

NwbProjectBxdfSurfaceMaterial nwbMaterialBindLoadSurface(const NwbMeshInstanceData instance){
    NwbProjectBxdfSurfaceMaterial result;
    result.base_color = nwbMaterialLoadConstantFloat4(instance, NWB_MATERIAL_BIND_SURFACE_BASE_COLOR_BYTE_OFFSET);
    result.roughness = nwbMaterialLoadConstantFloat(instance, NWB_MATERIAL_BIND_SURFACE_ROUGHNESS_BYTE_OFFSET);
    result.metallic = nwbMaterialLoadConstantFloat(instance, NWB_MATERIAL_BIND_SURFACE_METALLIC_BYTE_OFFSET);
    return result;
}

NwbProjectBxdfRuntimeMaterial nwbMaterialBindLoadRuntime(const NwbMeshInstanceData instance){
    NwbProjectBxdfRuntimeMaterial result;
    result.color_tint = nwbMaterialLoadMutableFloat4(instance, NWB_MATERIAL_BIND_RUNTIME_COLOR_TINT_BYTE_OFFSET);
    result.fade_alpha = nwbMaterialLoadMutableFloat(instance, NWB_MATERIAL_BIND_RUNTIME_FADE_ALPHA_BYTE_OFFSET);
    return result;
}

#endif
```

Authored Slang then reads typed fields:

```slang
const NwbProjectBxdfSurfaceMaterial surface = nwbMaterialBindLoadSurface(instance);
const NwbProjectBxdfRuntimeMaterial runtime = nwbMaterialBindLoadRuntime(instance);
const float4 materialColor = surface.base_color * runtime.color_tint;
```

No shader-side hash lookup is needed for fields declared by the interface.

## Cook Rules

Shader cook must:

- Parse `.bind` or material-interface schema sources before Slang compilation.
- Generate deterministic Slang includes for material interfaces.
- Resolve authored `.bind` includes to generated Slang content without mutating shader source files.
- Add generated includes to shader dependency metadata and checksum invalidation.
- Validate generated field names as legal Slang identifiers.
- Validate field types against the supported material value type table.
- Preserve exact shader entry-point and variant text.
- Reject undeclared material fields.
- Reject missing required fields that have no default.
- Reject block names that collide after canonicalization.

Material cook must:

- Resolve `asset.interface` to the parsed `.bind` material interface entry by stable virtual path.
- Keep the material-interface identity as `Name` metadata because the `.bind` schema is a cook-time interface source, not a runtime asset payload.
- Parse block-scoped `asset.parameters`.
- Fill missing values from interface defaults.
- Pack each block according to the generated layout.
- Build per-material binding rows that point to block rows.
- Keep `transparent` and `two_sided` policy explicit instead of inferring render mode from unrelated dynamic blocks.

Runtime currently must:

- Split material typed bytes by block class into material-constant bytes and mutable-default bytes when material surface info is created.
- Reject material instance overrides that target material-constant storage or a different material interface.
- Gather one word-aligned typed-material upload per material pass, deduplicating constant ranges by material/layout and mutable ranges by byte content.
- Pass the material-constant byte offset through draw push constants and the material-mutable byte offset through the packed instance slot exposed to Slang as `NwbMeshInstanceData.materialMutableByteOffset` (`InstanceGpuData::translation.w` on the CPU side).
- Keep frame/view/pass values outside material parameter buffers.

## Migration Plan

Completed:

- `.bind` schema parsing and validation in cook.
- Deterministic generated Slang includes and dependency checksum invalidation.
- Authored `.bind` includes resolved through the shader cook include resolver.
- Block-scoped material parameters and generated typed accessors.
- Runtime constant/mutable byte split, per-instance mutable overrides, and typed byte offsets consumed by mesh shaders.

Remaining:

- Finer-grained GPU update streams or dirty-row tracking when the renderer needs to avoid rebuilding the current per-pass typed-material upload.

## Non-goals

- Do not bind `.nwb` names directly to arbitrary Slang globals.
- Do not treat `.bind` files as raw shader globals or final runtime ABI text.
- Do not let one shader silently redefine a material layout.
- Do not add per-asset shader compiler selectors.
- Do not move cook-time shader/language processing into runtime graphics modules.
- Do not make frame, camera, or light values material parameters.
