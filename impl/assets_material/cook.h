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
    using StageShaderMap = MaterialCookMap<Core::ShaderType::Enum, Core::Assets::AssetRef<IShader>>;
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
    StageShaderMap stageShaders;
    // Generated AVBOIT accumulate PS name; empty for opaque materials.
    MaterialCookString avboitAccumulatePixelShaderName;
    // Occupancy/extinction twins of the accumulate name.
    MaterialCookString avboitOccupancyPixelShaderName;
    MaterialCookString avboitExtinctionPixelShaderName;
    ParameterMap parameters;
    u32 shadingModelId = 0u;
    // Surface dispatch id, deduped over the `surface` source set.
    u32 surfaceDispatchId = 0u;
    bool transparent = false;
    bool twoSided = false;
    // Explicit caster classification, independent of transparency; NwbMeshSurface supplies the optical parameters.
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
        surfaceDispatchId = 0u;
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


[[nodiscard]] Expected<MaterialCookEntry> ParseMaterialCookMetadata(
    const Path& assetRoot,
    AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
);
[[nodiscard]] bool ValidateMaterialCookInterfaces(
    const MaterialCookVector<MaterialBindEntry>& materialBindEntries,
    MaterialCookVector<MaterialCookEntry>& materialEntries,
    Core::Alloc::ScratchArena& scratchArena
);
[[nodiscard]] Expected<MaterialCookString> BuildMaterialBindIncludeSource(
    MaterialCookArena& arena,
    const MaterialBindEntry& entry,
    Core::Alloc::ScratchArena& scratchArena
);
[[nodiscard]] Expected<Path> EmitMaterialBindIncludes(
    MaterialCookArena& arena,
    const Path& cacheDirectory,
    AStringView configurationSafeName,
    const MaterialCookVector<MaterialBindEntry>& materialBindEntries,
    Core::Alloc::ScratchArena& scratchArena
);
struct MaterialBindDependency{
    MaterialCookString interfacePath;
    Name interfaceName = s_NameNone;
    bool dependsOnMaterialBind = false;

    explicit MaterialBindDependency(MaterialCookArena& arena)
        : interfacePath(arena)
    {}
};

[[nodiscard]] Expected<MaterialBindDependency> ResolveMaterialBindDependencyInterface(
    MaterialCookArena& arena,
    AStringView shaderName,
    const Path& materialBindIncludeRoot,
    const MaterialCookVector<Path>& dependencies,
    Core::Alloc::ScratchArena& scratchArena
);
[[nodiscard]] Expected<Material> BuildMaterialAsset(const MaterialCookEntry& materialEntry, Core::Assets::AssetArena& arena);

// Deterministic shading-model id (unique `bxdf`) + surface dispatch id (unique `surface`); shared sources share ids.
// Before material build + dispatch emission.
[[nodiscard]] bool AssignMaterialShadingModelIds(
    MaterialCookVector<MaterialCookEntry>& materialEntries,
    Core::Alloc::ScratchArena& scratchArena
);
// BXDF dispatch module (per-bxdf include, macro-renamed per id; unknown id = magenta, no engine default).
// Always written; after AssignMaterialShadingModelIds, before PrepareShaderEntriesForCook.
[[nodiscard]] Expected<Path> EmitDeferredBxdfDispatchModule(
    const Path& cacheDirectory,
    AStringView configurationSafeName,
    const MaterialCookVector<MaterialCookEntry>& materialEntries,
    Core::Alloc::ScratchArena& scratchArena
);

// Surface dispatch module (per-`.surface` include, macro-isolated per id, switch on surfaceDispatchId).
// Unknown id: neutral optical fields for shadow, fixed mid-grey for GI. Always written; run after AssignMaterialShadingModelIds.
[[nodiscard]] Expected<Path> EmitShadowSurfaceDispatchModule(
    const Path& cacheDirectory,
    AStringView configurationSafeName,
    const MaterialCookVector<MaterialBindEntry>& materialBindEntries,
    const MaterialCookVector<MaterialCookEntry>& materialEntries,
    Core::Alloc::ScratchArena& scratchArena
);

// G-buffer PS per `surface` material (engine authoring + typed `.bind` + resolved hook); pixel = generated PS, mesh = shared.
// Explicit `shaders` only for opaque non-refractive; transparent/refractive must use `surface`. Sources must be absolute paths.
[[nodiscard]] Expected<MaterialCookVector<GeneratedMaterialPixelShader>> EmitMaterialPixelShaders(
    MaterialCookArena& arena,
    const Path& cacheDirectory,
    AStringView configurationSafeName,
    AStringView sharedMeshShaderName,
    MaterialCookVector<MaterialCookEntry>& materialEntries,
    Core::Alloc::ScratchArena& scratchArena
);

// Transparent-pass twin of EmitMaterialPixelShaders: generates each TRANSPARENT `surface` material's AVBOIT accumulate PS
// (not a material stage shader; name stored on the cooked material). Opaque skipped; explicit-stage transparent rejected
// (no separate AVBOIT hook in the schema). `surfaceSource` must be an absolute path.
[[nodiscard]] Expected<MaterialCookVector<GeneratedMaterialPixelShader>> EmitMaterialAvboitAccumulatePixelShaders(
    MaterialCookArena& arena,
    const Path& cacheDirectory,
    AStringView configurationSafeName,
    MaterialCookVector<MaterialCookEntry>& materialEntries,
    Core::Alloc::ScratchArena& scratchArena
);

// Occupancy/extinction twins of the accumulate PS: same surface.renderCoverage contract, names stored on the cooked
// material (not stage shaders). `surfaceSource` must already be resolved.
[[nodiscard]] Expected<MaterialCookVector<GeneratedMaterialPixelShader>> EmitMaterialAvboitOccupancyPixelShaders(
    MaterialCookArena& arena,
    const Path& cacheDirectory,
    AStringView configurationSafeName,
    MaterialCookVector<MaterialCookEntry>& materialEntries,
    Core::Alloc::ScratchArena& scratchArena
);
[[nodiscard]] Expected<MaterialCookVector<GeneratedMaterialPixelShader>> EmitMaterialAvboitExtinctionPixelShaders(
    MaterialCookArena& arena,
    const Path& cacheDirectory,
    AStringView configurationSafeName,
    MaterialCookVector<MaterialCookEntry>& materialEntries,
    Core::Alloc::ScratchArena& scratchArena
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

