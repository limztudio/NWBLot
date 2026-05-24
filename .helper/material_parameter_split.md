# NWBLot Material Parameter Split Specification

Updated: 2026-05-22

## Purpose

Material parameter layout must not be decided by an authored shader alone. The layout is a contract between a material family, its material assets, shader cook, and runtime binding code.

The design goal is to keep the typed, generated Slang accessor path while also splitting data by update frequency. Shaders should read fields such as `surface.base_color`, but the CPU should only upload the smallest block that actually changed.

This document is a design specification. It does not describe the current runtime ABI.

## Current Runtime Contract

The current material cook path resolves `asset.interface`, validates `asset.parameters` against the parsed `.bind` schema, serializes typed layout metadata plus packed typed block bytes, and generates Slang accessors that compile only with `NWB_MATERIAL_TYPED_BINDING=1`.

Mesh material shaders read the packed typed payload through the helper surface in `mesh_shader_authoring.slangi`, which owns `[[vk::binding(6, 0)]] g_NwbMaterialTypedWords`. The old shader-side hash lookup helpers and any material binding at slot `5` are removed and should not be reintroduced.

The remaining improvement area is update granularity: all typed block bytes are currently uploaded through one packed material payload even when some fields change rarely and others change every frame.

## Ownership Model

Add a material interface contract as the source of truth for material fields and parameter blocks.

The material interface owns:

- Block names.
- Field names.
- Field types.
- Default values.
- Update policy.
- Shader visibility.
- Packing and generated Slang type names.

The material asset owns:

- The selected material interface.
- Stage shader references.
- Shader variant defines.
- Concrete values for interface fields.
- Optional per-material update policy overrides only when the interface explicitly allows them.

The shader asset owns:

- Slang entry point metadata.
- Include roots.
- Shader feature defines.
- Authoring code that consumes the generated interface include.

The runtime renderer owns:

- Buffer allocation for each block class.
- Dirty tracking.
- Descriptor/binding set updates.
- Per-instance indices into material block buffers.

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

Material constant blocks are uploaded when the material is loaded or recooked. They are treated as immutable during normal runtime.

Use for:

- Base color.
- Roughness.
- Metallic.
- Texture transform defaults.
- Other parameters that define the stable material identity.

Recommended storage:

- Dense `StructuredBuffer<BlockData, Std430DataLayout>` arrays.
- One row per material.
- Per-material binding table stores the row index.

### Material Mutable

Material mutable blocks are per-material values that can change at runtime. They must be grouped by fields that normally change together.

Use for:

- Runtime tint.
- Fade alpha.
- Highlight intensity.
- Time-varying material controls shared by all instances using the material.

Recommended storage:

- One shader-visible `StructuredBuffer` backed by a CPU upload stream per mutable block type.
- Dirty tracking per block row, not per whole material.
- CPU update APIs write a complete block row.

### Frame Or Pass

Frame, camera, light, and pass values are not material parameters. Keep them in existing frame/view/pass buffers.

Use for:

- Camera data.
- View projection data.
- Lighting buffers.
- Time values shared by many materials.

## Metadata Shape

The proposed shape adds a material interface asset. The exact asset type name can change during implementation, but the contract should stay separate from one material instance.

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

asset.interface = "project/material_interfaces/bxdf_surface";
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

The declarations above define the interface schema. They do not directly declare shader globals, descriptor bindings, or runtime storage. Cook parses the `.bind` file, validates the attributes and field types, assigns block classes, then generates the real Slang include containing packed structs, `StructuredBuffer` declarations, binding tables, and accessor helpers.

Authored shaders may include the schema path as the stable authoring hook:

```slang
#include "project/material_interfaces/bxdf_surface.bind"
```

During shader cook, the include resolver maps that `.bind` include to the generated `.slangi` content for the parsed schema. Prefer a virtual include mapping or generated include path remap over editing the shader source on disk. The shader source remains stable, while the Slang compiler sees the generated material ABI.

The `.bind` file must be a dependency of the generated Slang include and every consuming shader. Changing a field, default, block class, attribute, or instance declaration must invalidate the generated include and the compiled shader bytecode.

## Authoring-Time Contract

Generated Slang types are cook products, but their schema is not discovered after shader authoring. The material interface asset is the authoring-time schema.

The authoring flow is:

1. Create or update a material interface asset.
2. Run the material-interface generation step, either explicitly from tools or implicitly as an early cook phase.
3. Write shader code against the generated include for that interface.
4. Compile the shader only after the generated include is available.

Shader authors therefore know field names from the interface asset, not from a material instance and not from shader reflection. If the generated include is missing or stale, shader cook must regenerate it before Slang compilation. Editor tooling should also regenerate interface includes on interface save or before shader language-service indexing.

The generated include path must be deterministic from the material interface identity. For example, an interface asset at `project/material_interfaces/bxdf_surface` can produce a generated include such as `nwb_generated/material_interfaces/project_bxdf_surface.slangi`. The exact path is implementation-defined, but it must be stable enough for shader source and editor tooling to reference.

If NWBLot does not want authored shader source to include generated paths directly, the shader asset can declare the interface and cook can inject the generated include before compiling the entry point. In that mode the shader still depends on the interface schema; the include is just supplied by the cook pipeline instead of a handwritten `#include`.

A `.bind` include is a third authoring-friendly spelling of the same interface-first model. The authored shader can reference `#include "xxx.bind"`, but shader cook must resolve that request to generated Slang before invoking Slang compilation. The `.bind` text is parsed by NWBLot tooling and should not be forwarded as arbitrary global Slang declarations.

There are only three valid models:

- Interface-first typed model: shaders see named fields after interface generation. This is the proposed model.
- Fixed engine ABI model: shaders see only built-in structs with engine-defined fields.
- Dynamic lookup model: shaders keep using hashed or indexed lookup helpers and do not see typed fields.

Typed arbitrary fields require the interface-first model. Cook-time generation cannot create new names for shader code unless those names already came from an authored schema that shader compilation consumes.

## Generated Slang Shape

Shader cook generates one Slang include per material interface before compiling shaders that consume the interface. The generated file is a dependency of every consuming shader.

Generated include example:

```slang
#ifndef NWB_PROJECT_BXDF_MATERIAL_GENERATED_SLANGI
#define NWB_PROJECT_BXDF_MATERIAL_GENERATED_SLANGI

struct NwbProjectBxdfSurfaceMaterial{
    float4 base_color;
    float roughness;
    float metallic;
    float2 padding0;
};

struct NwbProjectBxdfRuntimeMaterial{
    float4 color_tint;
    float fade_alpha;
    float3 padding0;
};

static const uint NWB_PROJECT_BXDF_SURFACE_BASE_COLOR_BYTE_OFFSET = 0u;
static const uint NWB_PROJECT_BXDF_SURFACE_ROUGHNESS_BYTE_OFFSET = 16u;
static const uint NWB_PROJECT_BXDF_SURFACE_METALLIC_BYTE_OFFSET = 20u;
static const uint NWB_PROJECT_BXDF_RUNTIME_COLOR_TINT_BYTE_OFFSET = 32u;
static const uint NWB_PROJECT_BXDF_RUNTIME_FADE_ALPHA_BYTE_OFFSET = 48u;

NwbProjectBxdfSurfaceMaterial nwbProjectLoadBxdfSurfaceMaterial(const NwbMeshInstanceData instance){
    NwbProjectBxdfSurfaceMaterial result;
    result.base_color = nwbMaterialLoadFloat4(instance, NWB_PROJECT_BXDF_SURFACE_BASE_COLOR_BYTE_OFFSET);
    result.roughness = nwbMaterialLoadFloat(instance, NWB_PROJECT_BXDF_SURFACE_ROUGHNESS_BYTE_OFFSET);
    result.metallic = nwbMaterialLoadFloat(instance, NWB_PROJECT_BXDF_SURFACE_METALLIC_BYTE_OFFSET);
    return result;
}

NwbProjectBxdfRuntimeMaterial nwbProjectLoadBxdfRuntimeMaterial(const NwbMeshInstanceData instance){
    NwbProjectBxdfRuntimeMaterial result;
    result.color_tint = nwbMaterialLoadFloat4(instance, NWB_PROJECT_BXDF_RUNTIME_COLOR_TINT_BYTE_OFFSET);
    result.fade_alpha = nwbMaterialLoadFloat(instance, NWB_PROJECT_BXDF_RUNTIME_FADE_ALPHA_BYTE_OFFSET);
    return result;
}

#endif
```

Authored Slang then reads typed fields:

```slang
const NwbProjectBxdfSurfaceMaterial surface = nwbProjectLoadBxdfSurfaceMaterial(instance);
const NwbProjectBxdfRuntimeMaterial runtime = nwbProjectLoadBxdfRuntimeMaterial(instance);
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

- Resolve `asset.interface` through typed asset references.
- Resolve `.bind` schema references through the same material interface contract.
- Parse block-scoped `asset.parameters`.
- Fill missing values from interface defaults.
- Pack each block according to the generated layout.
- Build per-material binding rows that point to block rows.
- Keep alpha/transparent policy explicit instead of inferring render mode from unrelated dynamic blocks.

Runtime must:

- Allocate one GPU buffer stream per generated block class and material interface.
- Track dirty rows per mutable block.
- Upload complete dirty block rows.
- Preserve old rows until GPU work that references them has completed.
- Keep frame/view/pass values outside material parameter buffers.

## Migration Plan

1. Add material interface or `.bind` schema parsing and validation in cook.
2. Generate Slang includes and checksum dependencies before compiling consuming shaders.
3. Resolve authored `.bind` includes through the shader cook include resolver.
4. Add runtime binding tables for typed material block indices.
5. Move project BxDF materials to block-scoped parameters.
6. Remove shader-authored manual key constants after generated accessors are in use.

## Non-goals

- Do not bind `.nwb` names directly to arbitrary Slang globals.
- Do not treat `.bind` files as raw shader globals or final runtime ABI text.
- Do not let one shader silently redefine a material layout.
- Do not add per-asset shader compiler selectors.
- Do not move cook-time shader/language processing into runtime graphics modules.
- Do not make frame, camera, or light values material parameters.
