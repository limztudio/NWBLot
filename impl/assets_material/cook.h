// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "asset.h"
#include "bind.h"
#include "cook_types.h"

#include <core/alloc/scratch.h>
#include <core/metascript/parser.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MaterialCookEntry{
    using StageShaderMap = MaterialCookMap<Core::ShaderType::Enum, Core::Assets::AssetRef<Shader>>;
    using ParameterMap = MaterialBindParameterMap;

    // Readable source text (not Name); the cook builds paths and dedup keys from it.
    MaterialCookString virtualPath;
    MaterialCookString materialInterface;
    u64 typedLayoutHash = 0u;
    Material::TypedLayoutBlockVector typedLayoutBlocks;
    Material::TypedLayoutFieldVector typedLayoutFields;
    Material::TypedBlockByteVector typedBlockBytes;
    Material::ResourceReferenceVector resourceReferences;
    MaterialCookString shaderVariant;
    // Deferred lighting BXDF source; resolved and deduped by the cross-asset phase.
    MaterialCookString bxdfSource;
    // Surface hook; empty when explicit `shaders` are declared instead.
    MaterialCookString surfaceSource;
    u32 shadingModelId = 0u;
    // Shadow-transmittance id, deduped over the `surface` source set.
    u32 shadowTransmittanceModelId = 0u;
    StageShaderMap stageShaders;
    // Generated AVBOIT accumulate PS name; empty for opaque materials.
    MaterialCookString avboitAccumulatePixelShaderName;
    // Occupancy/extinction twins of the accumulate name.
    MaterialCookString avboitOccupancyPixelShaderName;
    MaterialCookString avboitExtinctionPixelShaderName;
    ParameterMap parameters;
    bool transparent = false;
    bool twoSided = false;
    // The dedicated refractive-caster classification flag (SEPARATE from `transparent`), parsed by
    // ParseMaterialRenderProperties from the bare `refractive` field and threaded through BuildMaterialAsset into
    // the cooked Material. The material decides only this boolean; the refraction VALUES are shader-side
    // (NwbMeshSurface). Authored metadata must provide the value explicitly.
    bool refractive = false;

    explicit MaterialCookEntry(MaterialCookArena& arena)
        : virtualPath(arena)
        , materialInterface(arena)
        , typedLayoutBlocks(arena)
        , typedLayoutFields(arena)
        , typedBlockBytes(arena)
        , resourceReferences(arena)
        , shaderVariant(arena)
        , bxdfSource(arena)
        , surfaceSource(arena)
        , stageShaders(0, Hasher<Core::ShaderType::Enum>(), EqualTo<Core::ShaderType::Enum>(), arena)
        , avboitAccumulatePixelShaderName(arena)
        , avboitOccupancyPixelShaderName(arena)
        , avboitExtinctionPixelShaderName(arena)
        , parameters(0, Hasher<ACompactString>(), EqualTo<ACompactString>(), arena)
    {}

    void reset(){
        virtualPath.clear();
        materialInterface.clear();
        typedLayoutHash = 0u;
        typedLayoutBlocks.clear();
        typedLayoutFields.clear();
        typedBlockBytes.clear();
        resourceReferences.clear();
        shaderVariant.clear();
        bxdfSource.clear();
        surfaceSource.clear();
        shadingModelId = 0u;
        shadowTransmittanceModelId = 0u;
        stageShaders.clear();
        avboitAccumulatePixelShaderName.clear();
        avboitOccupancyPixelShaderName.clear();
        avboitExtinctionPixelShaderName.clear();
        parameters.clear();
        transparent = false;
        twoSided = false;
        refractive = false;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Cook-generated PS for a `surface`-authored material; cooks like any other shader.
struct GeneratedMaterialPixelShader{
    MaterialCookString name;    // shader virtual name (identity), e.g. "generated/material_ps/<material path>"
    MaterialCookString source;  // absolute, forward-slash path of the generated .slang source in the cook cache

    explicit GeneratedMaterialPixelShader(MaterialCookArena& arena)
        : name(arena)
        , source(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool ParseMaterialCookMetadata(
    const Path& assetRoot,
    AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    MaterialCookEntry& outEntry,
    Core::Alloc::ScratchArena& scratchArena
);
[[nodiscard]] bool ValidateMaterialCookInterfaces(
    const MaterialCookVector<MaterialBindEntry>& materialBindEntries,
    MaterialCookVector<MaterialCookEntry>& materialEntries,
    Core::Alloc::ScratchArena& scratchArena
);
[[nodiscard]] bool BuildMaterialBindIncludeSource(
    MaterialCookArena& arena,
    const MaterialBindEntry& entry,
    MaterialCookString& outSource,
    Core::Alloc::ScratchArena& scratchArena
);
[[nodiscard]] bool EmitMaterialBindIncludes(
    MaterialCookArena& arena,
    const Path& cacheDirectory,
    AStringView configurationSafeName,
    const MaterialCookVector<MaterialBindEntry>& materialBindEntries,
    Path& outIncludeRoot,
    Core::Alloc::ScratchArena& scratchArena
);
[[nodiscard]] bool ResolveMaterialBindDependencyInterface(
    AStringView shaderName,
    const Path& materialBindIncludeRoot,
    const MaterialCookVector<Path>& dependencies,
    MaterialCookString& outInterfacePath,
    Name& outInterfaceName,
    bool& outDependsOnMaterialBind,
    Core::Alloc::ScratchArena& scratchArena
);
[[nodiscard]] bool BuildMaterialAsset(const MaterialCookEntry& materialEntry, Material& outMaterial);

// Assigns each material a deferred shading-model id from the unique set of `bxdf` sources AND a separate
// shadow-transmittance id from the unique set of `surface` sources (both sorted for deterministic ids;
// materials sharing a bxdf / a surface share the respective id). Must run before the material assets are built
// (so the ids are baked into each cooked material) and before EmitDeferredBxdfDispatchModule /
// EmitShadowTransmittanceDispatchModule.
[[nodiscard]] bool AssignMaterialShadingModelIds(
    MaterialCookVector<MaterialCookEntry>& materialEntries,
    Core::Alloc::ScratchArena& scratchArena
);
// Generates the deferred lighting BXDF dispatch module (deferred/generated/bxdf_dispatch.slangi) under the
// returned include root. The module includes each unique bxdf (macro-renamed per id) + a switch dispatch
// keyed by shading-model id; an unknown id resolves to a visible magenta (the engine ships no default BXDF).
// The engine's deferred lighting harness includes this module. Always writes the module (empty dispatch if
// no materials declare a bxdf). Run after AssignMaterialShadingModelIds + before PrepareShaderEntriesForCook.
[[nodiscard]] bool EmitDeferredBxdfDispatchModule(
    const Path& cacheDirectory,
    AStringView configurationSafeName,
    const MaterialCookVector<MaterialCookEntry>& materialEntries,
    Path& outIncludeRoot,
    Core::Alloc::ScratchArena& scratchArena
);

// Generates the shadow-transmittance dispatch module (shadow/generated/transmittance_dispatch.slangi) under the
// returned include root. The module includes each unique `.surface` (with its `.bind`, macro-isolated per id) +
// a switch dispatch keyed by shadowTransmittanceModelId that routes a hit to that material's surface hook. An unknown
// id returns a no-surface NwbMeshSurface: shadow consumes its neutral optical fields, while GI consumers use the fixed
// mid-grey base color. That fallback applies only to opaque, non-refractive explicit-stage materials; transparent or
// refractive explicit-stage materials are rejected during metadata parsing. The shadow trace includes this module.
// Always writes the module (empty dispatch if no materials declare a surface). Run after
// AssignMaterialShadingModelIds + before PrepareShaderEntriesForCook.
[[nodiscard]] bool EmitShadowTransmittanceDispatchModule(
    const Path& cacheDirectory,
    AStringView configurationSafeName,
    const MaterialCookVector<MaterialBindEntry>& materialBindEntries,
    const MaterialCookVector<MaterialCookEntry>& materialEntries,
    Path& outIncludeRoot,
    Core::Alloc::ScratchArena& scratchArena
);

// For each material that omits explicit `shaders`, generate its G-buffer pixel shader (engine pixel-shader
// authoring + the material's typed `.bind` + its resolved `surface` hook) under a `generated/` directory in the
// cook cache, set the material's stage shaders (pixel = the generated PS, mesh = `sharedMeshShaderName`), and
// append a (name, source) record so the caller can synthesize the shader entry. Errors if a material declares
// both `surface` and `shaders`, or neither, or omits the interface needed to generate. Explicit `shaders` are
// accepted only for opaque, non-refractive materials; transparent/refractive materials must use their project
// `surface` hook so AVBOIT and shadow optical passes use the same contract. `bxdfSource`/`surfaceSource` must
// already be resolved to absolute paths.
[[nodiscard]] bool EmitMaterialPixelShaders(
    MaterialCookArena& arena,
    const Path& cacheDirectory,
    AStringView configurationSafeName,
    AStringView sharedMeshShaderName,
    MaterialCookVector<MaterialCookEntry>& materialEntries,
    MaterialCookVector<GeneratedMaterialPixelShader>& outGenerated,
    Core::Alloc::ScratchArena& scratchArena
);

// The transparent-pass twin of EmitMaterialPixelShaders: for each TRANSPARENT material authored with a
// `surface`, generate its AVBOIT accumulate pixel shader (engine AVBOIT-accumulate authoring + the material's
// typed `.bind` + its resolved `surface` hook) under a `generated/` directory in the cook cache, and append a
// (name, source) record so the caller can synthesize the shader entry. Unlike the G-buffer PS this is NOT
// assigned as a material stage shader (the material's single pixel stage is the G-buffer PS); its generated name
// is stored on the cooked material for the transparent draw. Opaque materials are skipped; transparent/refractive
// explicit-stage materials are rejected
// during material metadata parsing because the current schema has no separate AVBOIT/shadow optical hook.
// `surfaceSource` must already be resolved to an absolute path.
[[nodiscard]] bool EmitMaterialAvboitAccumulatePixelShaders(
    MaterialCookArena& arena,
    const Path& cacheDirectory,
    AStringView configurationSafeName,
    MaterialCookVector<MaterialCookEntry>& materialEntries,
    MaterialCookVector<GeneratedMaterialPixelShader>& outGenerated,
    Core::Alloc::ScratchArena& scratchArena
);

// The occupancy/extinction twins of EmitMaterialAvboitAccumulatePixelShaders: for each TRANSPARENT material
// authored with a `surface`, generate its AVBOIT occupancy / extinction pixel shader (engine AVBOIT
// occupancy/extinction authoring + the material's typed `.bind` + its resolved `surface` hook), so all three
// AVBOIT passes read this material's SAME shader-decided surface.renderCoverage. Like the accumulate PS these are NOT
// material stage shaders; their generated names are stored on the cooked material for the transparent draw's
// occupancy/extinction pass. `surfaceSource` must already be resolved.
[[nodiscard]] bool EmitMaterialAvboitOccupancyPixelShaders(
    MaterialCookArena& arena,
    const Path& cacheDirectory,
    AStringView configurationSafeName,
    MaterialCookVector<MaterialCookEntry>& materialEntries,
    MaterialCookVector<GeneratedMaterialPixelShader>& outGenerated,
    Core::Alloc::ScratchArena& scratchArena
);
[[nodiscard]] bool EmitMaterialAvboitExtinctionPixelShaders(
    MaterialCookArena& arena,
    const Path& cacheDirectory,
    AStringView configurationSafeName,
    MaterialCookVector<MaterialCookEntry>& materialEntries,
    MaterialCookVector<GeneratedMaterialPixelShader>& outGenerated,
    Core::Alloc::ScratchArena& scratchArena
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

