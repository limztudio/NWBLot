// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

    Name virtualPath = NAME_NONE;
    Name materialInterface = NAME_NONE;
    u64 typedLayoutHash = 0u;
    Material::TypedLayoutBlockVector typedLayoutBlocks;
    Material::TypedLayoutFieldVector typedLayoutFields;
    Material::TypedBlockByteVector typedBlockBytes;
    MaterialCookString shaderVariant;
    // This material's deferred lighting BXDF source. Parse stores the `project/`- or `engine/`-rooted virtual
    // path (with the dedicated `.bxdf` extension) from the required `bxdf` field; the cross-asset phase resolves
    // it to an absolute, canonical (forward-slash) path against all asset roots, then dedups by it, assigns
    // shadingModelId, and generates the deferred lighting dispatch module.
    MaterialCookString bxdfSource;
    u32 shadingModelId = 0u;
    StageShaderMap stageShaders;
    ParameterMap parameters;
    bool transparent = false;
    bool twoSided = false;

    explicit MaterialCookEntry(MaterialCookArena& arena)
        : typedLayoutBlocks(arena)
        , typedLayoutFields(arena)
        , typedBlockBytes(arena)
        , shaderVariant(arena)
        , bxdfSource(arena)
        , stageShaders(0, Hasher<Core::ShaderType::Enum>(), EqualTo<Core::ShaderType::Enum>(), arena)
        , parameters(0, Hasher<ACompactString>(), EqualTo<ACompactString>(), arena)
    {}

    void reset(){
        virtualPath = NAME_NONE;
        materialInterface = NAME_NONE;
        typedLayoutHash = 0u;
        typedLayoutBlocks.clear();
        typedLayoutFields.clear();
        typedBlockBytes.clear();
        shaderVariant.clear();
        bxdfSource.clear();
        shadingModelId = 0u;
        stageShaders.clear();
        parameters.clear();
        transparent = false;
        twoSided = false;
    }
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
    Core::Alloc::ScratchArena& scratchArena
);
[[nodiscard]] bool BuildMaterialAsset(const MaterialCookEntry& materialEntry, Material& outMaterial);

// Assigns each material a deferred shading-model id from the unique set of `bxdf` sources (sorted for
// deterministic ids; materials sharing a bxdf share an id). Must run before the material assets are built
// (so the id is baked into each cooked material) and before EmitDeferredBxdfDispatchModule.
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

