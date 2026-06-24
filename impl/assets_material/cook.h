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
    // This material's surface hook. Parse stores the `project/`/`engine/`-rooted virtual path (`.surface`) from
    // the optional `surface` field; the cross-asset phase resolves it to an absolute path and (when `shaders` is
    // omitted) generates this material's G-buffer pixel shader by wrapping it with the engine PS authoring + the
    // material's `.bind`. Empty when the material declares explicit `shaders` instead.
    MaterialCookString surfaceSource;
    u32 shadingModelId = 0u;
    // This material's shadow-transmittance dispatch id, deduped over the `surface` SOURCE set (NOT the bxdf id:
    // the surface hook computes the per-hit transmittance the shadow trace returns). Materials sharing a
    // `.surface` share an id. Assigned by AssignMaterialShadingModelIds (alongside shadingModelId) and consumed
    // by EmitShadowTransmittanceDispatchModule, which routes this id to the material's transmittance hook.
    u32 shadowTransmittanceModelId = 0u;
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
        , surfaceSource(arena)
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
        surfaceSource.clear();
        shadingModelId = 0u;
        shadowTransmittanceModelId = 0u;
        stageShaders.clear();
        parameters.clear();
        transparent = false;
        twoSided = false;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// A pixel shader the cook generated for a material that declared `surface` instead of explicit `shaders`.
// The caller (graphics volume prepare) synthesizes a shader entry from these so it cooks like any other shader.
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
// a switch dispatch keyed by shadowTransmittanceModelId that routes a hit to that material's surface hook and
// returns its transmittance; an unknown id resolves to float3(1) (untinted/lit -- no engine default tint). The
// shadow trace (a later unit) includes this module. Always writes the module (empty dispatch if no materials
// declare a surface). Run after AssignMaterialShadingModelIds + before PrepareShaderEntriesForCook.
[[nodiscard]] bool EmitShadowTransmittanceDispatchModule(
    const Path& cacheDirectory,
    AStringView configurationSafeName,
    const MaterialCookVector<MaterialCookEntry>& materialEntries,
    Path& outIncludeRoot,
    Core::Alloc::ScratchArena& scratchArena
);

// For each material that omits explicit `shaders`, generate its G-buffer pixel shader (engine pixel-shader
// authoring + the material's typed `.bind` + its resolved `surface` hook) under a `generated/` directory in the
// cook cache, set the material's stage shaders (pixel = the generated PS, mesh = `sharedMeshShaderName`), and
// append a (name, source) record so the caller can synthesize the shader entry. Errors if a material declares
// both `surface` and `shaders`, or neither, or omits the interface needed to generate. Materials with explicit
// `shaders` are left untouched. `bxdfSource`/`surfaceSource` must already be resolved to absolute paths.
[[nodiscard]] bool EmitMaterialPixelShaders(
    MaterialCookArena& arena,
    const Path& cacheDirectory,
    AStringView configurationSafeName,
    AStringView sharedMeshShaderName,
    MaterialCookVector<MaterialCookEntry>& materialEntries,
    MaterialCookVector<GeneratedMaterialPixelShader>& outGenerated,
    Core::Alloc::ScratchArena& scratchArena
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

