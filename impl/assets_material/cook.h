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

// Deterministic shading-model id (unique `bxdf`) + transmittance id (unique `surface`); shared sources share ids.
// Before material build + dispatch emission.
[[nodiscard]] bool AssignMaterialShadingModelIds(
    MaterialCookVector<MaterialCookEntry>& materialEntries,
    Core::Alloc::ScratchArena& scratchArena
);
// BXDF dispatch module (per-bxdf include, macro-renamed per id; unknown id = magenta, no engine default).
// Always written; after AssignMaterialShadingModelIds, before PrepareShaderEntriesForCook.
[[nodiscard]] bool EmitDeferredBxdfDispatchModule(
    const Path& cacheDirectory,
    AStringView configurationSafeName,
    const MaterialCookVector<MaterialCookEntry>& materialEntries,
    Path& outIncludeRoot,
    Core::Alloc::ScratchArena& scratchArena
);

// Shadow-transmittance dispatch module (per-`.surface` include, macro-isolated per id, switch on shadowTransmittanceModelId).
// Unknown id: neutral optical fields for shadow, fixed mid-grey for GI. Always written; run after AssignMaterialShadingModelIds.
[[nodiscard]] bool EmitShadowTransmittanceDispatchModule(
    const Path& cacheDirectory,
    AStringView configurationSafeName,
    const MaterialCookVector<MaterialBindEntry>& materialBindEntries,
    const MaterialCookVector<MaterialCookEntry>& materialEntries,
    Path& outIncludeRoot,
    Core::Alloc::ScratchArena& scratchArena
);

// G-buffer PS per `surface` material (engine authoring + typed `.bind` + resolved hook); pixel = generated PS, mesh = shared.
// Explicit `shaders` only for opaque non-refractive; transparent/refractive must use `surface`. Sources must be absolute paths.
[[nodiscard]] bool EmitMaterialPixelShaders(
    MaterialCookArena& arena,
    const Path& cacheDirectory,
    AStringView configurationSafeName,
    AStringView sharedMeshShaderName,
    MaterialCookVector<MaterialCookEntry>& materialEntries,
    MaterialCookVector<GeneratedMaterialPixelShader>& outGenerated,
    Core::Alloc::ScratchArena& scratchArena
);

// Transparent-pass twin of EmitMaterialPixelShaders: generates each TRANSPARENT `surface` material's AVBOIT accumulate PS
// (not a material stage shader; name stored on the cooked material). Opaque skipped; explicit-stage transparent rejected
// (no separate AVBOIT hook in the schema). `surfaceSource` must be an absolute path.
[[nodiscard]] bool EmitMaterialAvboitAccumulatePixelShaders(
    MaterialCookArena& arena,
    const Path& cacheDirectory,
    AStringView configurationSafeName,
    MaterialCookVector<MaterialCookEntry>& materialEntries,
    MaterialCookVector<GeneratedMaterialPixelShader>& outGenerated,
    Core::Alloc::ScratchArena& scratchArena
);

// Occupancy/extinction twins of the accumulate PS: same surface.renderCoverage contract, names stored on the cooked
// material (not stage shaders). `surfaceSource` must already be resolved.
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

