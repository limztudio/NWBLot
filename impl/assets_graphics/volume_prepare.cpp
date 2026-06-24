// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "material_validation.h"
#include "shader_cook_plan.h"
#include "shader_volume_writer.h"

#include <impl/assets_csg/cook.h>
#include <impl/assets_material/cook.h>
#include <impl/assets_shader/asset.h>
#include <impl/assets_shader/cook.h>
#include <impl/assets_volume/volume_prepare_registry.h>

#include <core/assets/cook_metadata.h>
#include <core/assets/paths.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_graphics_volume_prepare{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_GraphicsVolumeMetadataExtensionName("assets_graphics/volume_metadata");
inline constexpr Name s_IncludeAssetTypeName("include");

using MaterialBindEntryVector = ShaderCook::CookVector<MaterialBindEntry>;
using CsgShapeEntryVector = ShaderCook::CookVector<AssetsCsgCook::CsgShapeCookEntry>;

struct GraphicsVolumeMetadata{
    ShaderCook shaderCook;
    AssetsGraphicsCookDetail::IncludeMetadataMap includeMetadata;
    AssetsGraphicsCookDetail::ShaderEntryVector shaderEntries;
    MaterialBindEntryVector materialBindEntries;
    CsgShapeEntryVector csgShapeEntries;
    AssetsGraphicsCookDetail::PreparedShaderPlan preparedPlan;
    HashSet<
        AssetsGraphicsCookDetail::PreparedShaderKey,
        AssetsGraphicsCookDetail::PreparedShaderKeyHasher,
        EqualTo<AssetsGraphicsCookDetail::PreparedShaderKey>,
        ShaderCook::CookArena
    > seenShaderIdentityKeys;

    explicit GraphicsVolumeMetadata(ShaderCook::CookArena& arena)
        : shaderCook(arena)
        , includeMetadata(arena)
        , shaderEntries(arena)
        , materialBindEntries(arena)
        , csgShapeEntries(arena)
        , preparedPlan(arena)
        , seenShaderIdentityKeys(
            0,
            AssetsGraphicsCookDetail::PreparedShaderKeyHasher(),
            EqualTo<AssetsGraphicsCookDetail::PreparedShaderKey>(),
            arena
        )
    {}
};

static GraphicsVolumeMetadata& GraphicsMetadata(Core::Assets::ParsedAssetMetadata& metadata){
    return Core::Assets::RequireParsedMetadataExtension<GraphicsVolumeMetadata>(
        metadata,
        s_GraphicsVolumeMetadataExtensionName,
        metadata.arena
    );
}

static bool AppendUniqueShaderEntry(
    ShaderCook::ShaderEntry& shaderEntry,
    const Path& nwbFilePath,
    GraphicsVolumeMetadata& graphicsMetadata,
    AssetsGraphicsCookDetail::ShaderEntryVector& outShaderEntries
){
    if(shaderEntry.name.empty())
        return true;

    const AssetsGraphicsCookDetail::PreparedShaderKey shaderIdentityKey{
        ToName(shaderEntry.name),
        ToName(shaderEntry.archiveStage.view())
    };
    if(!graphicsMetadata.seenShaderIdentityKeys.insert(shaderIdentityKey).second){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: duplicate shader identity '{}' for stage '{}' from meta '{}'")
            , StringConvert(shaderEntry.name)
            , StringConvert(shaderEntry.archiveStage.c_str())
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }

    outShaderEntries.push_back(Move(shaderEntry));
    return true;
}

static Core::Assets::AssetMetadataParseResult::Enum ParseGraphicsDocumentMetadata(
    Core::Assets::AssetDocumentMetadataParseContext& context
){
    using namespace Core::Assets;

    if(context.assetType == Shader::AssetTypeName()){
        GraphicsVolumeMetadata& graphicsMetadata = GraphicsMetadata(context.parsedMetadata);
        ShaderCook::ShaderEntry shaderEntry(context.cookArena);
        if(!graphicsMetadata.shaderCook.parseShaderMeta(context.discoveredNwbFile.filePath, context.doc, shaderEntry, context.scratchArena))
            return AssetMetadataParseResult::Error;

        if(!Core::Assets::BuildDerivedAssetVirtualPath(
            context.discoveredNwbFile.assetRoot,
            context.discoveredNwbFile.virtualRoot.view(),
            Path(context.cookArena, shaderEntry.source),
            shaderEntry.name
        ))
            return AssetMetadataParseResult::Error;

        if(!AppendUniqueShaderEntry(shaderEntry, context.discoveredNwbFile.filePath, graphicsMetadata, graphicsMetadata.shaderEntries))
            return AssetMetadataParseResult::Error;
        return AssetMetadataParseResult::Parsed;
    }

    if(context.assetType == s_IncludeAssetTypeName){
        GraphicsVolumeMetadata& graphicsMetadata = GraphicsMetadata(context.parsedMetadata);
        ShaderCook::IncludeEntry includeEntry(context.cookArena);
        if(!graphicsMetadata.shaderCook.parseIncludeMeta(context.discoveredNwbFile.filePath, context.doc, includeEntry, context.scratchArena))
            return AssetMetadataParseResult::Error;

        if(!includeEntry.source.empty() && !includeEntry.defineValues.empty()){
            ErrorCode errorCode;
            const Path sourcePath(context.cookArena, includeEntry.source);
            const Path absSource = AbsolutePath(sourcePath, errorCode).lexically_normal();
            if(errorCode){
                NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: failed to resolve include metadata source '{}' from '{}': {}")
                    , StringConvert(includeEntry.source)
                    , PathToString<tchar>(context.discoveredNwbFile.filePath)
                    , StringConvert(errorCode.message())
                );
                return AssetMetadataParseResult::Error;
            }

            ScratchString key = PathToString(context.scratchArena, absSource);
            CanonicalizeTextInPlace(key);
            CookString cookKey(key, context.cookArena);
            if(!graphicsMetadata.includeMetadata.emplace(Move(cookKey), Move(includeEntry)).second){
                NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: duplicate include metadata for source '{}'")
                    , PathToString<tchar>(absSource)
                );
                return AssetMetadataParseResult::Error;
            }
        }

        return AssetMetadataParseResult::Parsed;
    }

    if(context.assetType == AssetsCsgCook::s_CsgShapeAssetTypeName){
        GraphicsVolumeMetadata& graphicsMetadata = GraphicsMetadata(context.parsedMetadata);
        AssetsCsgCook::CsgShapeCookEntry csgShapeEntry(context.cookArena);
        if(!AssetsCsgCook::ParseCsgShapeCookMetadata(
            context.cookArena,
            context.discoveredNwbFile.filePath,
            context.doc,
            csgShapeEntry,
            context.scratchArena
        ))
            return AssetMetadataParseResult::Error;

        graphicsMetadata.csgShapeEntries.push_back(Move(csgShapeEntry));
        return AssetMetadataParseResult::Parsed;
    }

    return AssetMetadataParseResult::Unsupported;
}

static bool ParseMaterialBindFiles(AssetsVolumeCookDetail::AssetVolumePrepareContext& context, GraphicsVolumeMetadata& graphicsMetadata){
    Core::Assets::DiscoveredBindFileVector bindFiles{ context.arena };
    if(!Core::Assets::DiscoverFilesWithExtension(
        context.resolvedPaths.assetRoots,
        MaterialBindNames::SourceExtensionText(),
        bindFiles,
        context.scratchArena
    ))
        return false;

    graphicsMetadata.materialBindEntries.reserve(bindFiles.size());
    for(const Core::Assets::DiscoveredNwbFile& discoveredBindFile : bindFiles){
        MaterialBindEntry bindEntry(context.arena);
        if(!ParseMaterialBindSource(discoveredBindFile.filePath, bindEntry, context.scratchArena))
            return false;

        if(!Core::Assets::BuildDerivedAssetVirtualPath(
            discoveredBindFile.assetRoot,
            discoveredBindFile.virtualRoot.view(),
            discoveredBindFile.filePath,
            bindEntry.virtualPath
        ))
            return false;

        graphicsMetadata.materialBindEntries.push_back(Move(bindEntry));
    }

    return true;
}

static bool PrepareGraphicsVolumeAssets(AssetsVolumeCookDetail::AssetVolumePrepareContext& context){
    GraphicsVolumeMetadata& graphicsMetadata = GraphicsMetadata(context.parsedMetadata);
    if(!ParseMaterialBindFiles(context, graphicsMetadata))
        return false;
    if(!AssetsCsgCook::AssignCsgShapeCookIds(graphicsMetadata.csgShapeEntries))
        return false;

    auto& materialEntries = context.parsedMetadata.entryRegistry.entries<MaterialCookEntry>(Material::AssetTypeName());
    if(graphicsMetadata.shaderEntries.empty() && !materialEntries.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: material assets require at least one shader entry"));
        return false;
    }

    if(!ValidateMaterialCookInterfaces(graphicsMetadata.materialBindEntries, materialEntries, context.scratchArena))
        return false;

    Path materialBindIncludeRoot(context.arena);
    if(!EmitMaterialBindIncludes(
        context.arena,
        context.resolvedPaths.cacheDirectory,
        context.configurationSafeName,
        graphicsMetadata.materialBindEntries,
        materialBindIncludeRoot,
        context.scratchArena
    ))
        return false;

    // Resolve each CSG shape's `eval` virtual path (engine/...; .slangi) to its absolute hand-written source
    // against the asset roots -- only known here, in the cross-asset phase. The resolved path becomes the verbatim
    // #include the generated CSG module emits for each shape's evaluator, mirroring how a material's `.surface`
    // virtual path resolves to its absolute source before the per-material pixel shader #includes it. Runs before
    // EmitCsgShapeModuleIncludes (which writes the generated module that #includes these resolved sources) and is
    // covered by the dependency checksum's repo alias. ResolveVirtualAssetPath preserves the asset-root case +
    // canonicalizes the appended components (matching the on-disk lowercase eval file names).
    for(auto& csgShapeEntry : graphicsMetadata.csgShapeEntries){
        if(csgShapeEntry.evalInclude.empty())
            continue;

        Path resolvedEvalSource(context.arena);
        if(!Core::Assets::ResolveVirtualAssetPath(
            context.resolvedPaths.assetRoots,
            AStringView(csgShapeEntry.evalInclude),
            resolvedEvalSource,
            context.scratchArena
        )){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: CSG shape '{}' eval '{}' does not resolve against any asset root")
                , StringConvert(csgShapeEntry.shapeName.c_str())
                , StringConvert(csgShapeEntry.evalInclude.c_str())
            );
            return false;
        }

        auto resolvedEvalText = PathToString(context.scratchArena, resolvedEvalSource);
        for(auto& ch : resolvedEvalText){
            if(ch == '\\')
                ch = '/';
        }
        csgShapeEntry.evalInclude.assign(AStringView(resolvedEvalText));
    }

    Path csgShapeIncludeRoot(context.arena);
    if(!AssetsCsgCook::EmitCsgShapeModuleIncludes(
        context.resolvedPaths.cacheDirectory,
        context.configurationSafeName,
        graphicsMetadata.csgShapeEntries,
        csgShapeIncludeRoot,
        context.scratchArena
    ))
        return false;

    // Resolve each material's `bxdf` + `surface` virtual paths (project/...; .bxdf / .surface) to absolute
    // sources against the asset roots -- only known here, in the cross-asset phase. Each resolved path becomes a
    // verbatim #include in a generated shader (the BXDF dispatch module / the per-material pixel shader) covered
    // by the dependency checksum's repo alias; this mirrors how a material's `interface` resolves to a discovered
    // .bind. ResolveVirtualAssetPath preserves the asset-root case + canonicalizes the appended components
    // (matching the on-disk lowercase shader file names).
    const auto resolveMaterialVirtualSource = [&context](auto& virtualSource, const AStringView label, const AStringView materialName) -> bool {
        if(virtualSource.empty())
            return true;

        Path resolvedSource(context.arena);
        if(!Core::Assets::ResolveVirtualAssetPath(
            context.resolvedPaths.assetRoots,
            AStringView(virtualSource),
            resolvedSource,
            context.scratchArena
        )){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: material '{}' {} '{}' does not resolve against any asset root")
                , StringConvert(materialName)
                , StringConvert(label)
                , StringConvert(virtualSource.c_str())
            );
            return false;
        }

        auto resolvedText = PathToString(context.scratchArena, resolvedSource);
        for(auto& ch : resolvedText){
            if(ch == '\\')
                ch = '/';
        }
        virtualSource.assign(AStringView(resolvedText));
        return true;
    };
    for(auto& materialEntry : materialEntries){
        const AStringView materialName(materialEntry.virtualPath.c_str());
        if(!resolveMaterialVirtualSource(materialEntry.bxdfSource, AStringView("bxdf"), materialName))
            return false;
        if(!resolveMaterialVirtualSource(materialEntry.surfaceSource, AStringView("surface"), materialName))
            return false;
    }

    // Assign each material its deferred shading-model id from the unique set of `bxdf` declarations, then
    // generate the deferred lighting BXDF dispatch module the engine harness includes. Both run before
    // shader preparation (so the harness picks up the module + the dependency checksum covers each BXDF) and
    // before the materials are built (so the id is baked into each cooked material).
    if(!AssignMaterialShadingModelIds(materialEntries, context.scratchArena))
        return false;

    Path deferredBxdfIncludeRoot(context.arena);
    if(!EmitDeferredBxdfDispatchModule(
        context.resolvedPaths.cacheDirectory,
        context.configurationSafeName,
        materialEntries,
        deferredBxdfIncludeRoot,
        context.scratchArena
    ))
        return false;

    // Generate the shadow-transmittance dispatch module the shadow trace includes (routes each material's
    // shadowTransmittanceModelId to its surface hook). Runs alongside the BXDF dispatch -- before shader
    // preparation (so its include root is registered + each `.surface` it #includes is covered by the
    // dependency checksum) and after the surface ids are assigned. The trace shaders #include it in a later
    // unit; emitting it now keeps the cook + the rasterizer unchanged.
    Path shadowTransmittanceIncludeRoot(context.arena);
    if(!EmitShadowTransmittanceDispatchModule(
        context.resolvedPaths.cacheDirectory,
        context.configurationSafeName,
        materialEntries,
        shadowTransmittanceIncludeRoot,
        context.scratchArena
    ))
        return false;

    // Generate each material's G-buffer pixel shader from its `surface` hook (when it omits explicit `shaders`),
    // assigning its stage shaders (pixel = generated PS, mesh = the shared engine mesh shader). Then synthesize a
    // shader entry per generated PS so it is prepared + cooked like any authored shader. Runs before shader prep
    // (so the entries are cooked) and before ValidateMaterials (so each material's stage shaders are populated).
    auto& materialCookArena = materialEntries.get_allocator().arena();
    MaterialCookVector<GeneratedMaterialPixelShader> generatedPixelShaders(materialCookArena);
    if(!EmitMaterialPixelShaders(
        materialCookArena,
        context.resolvedPaths.cacheDirectory,
        context.configurationSafeName,
        AStringView("engine/graphics/mesh/shared_ms"),
        materialEntries,
        generatedPixelShaders,
        context.scratchArena
    ))
        return false;

    // The transparent-pass twin: generate each TRANSPARENT material's AVBOIT accumulate pixel shader from the
    // SAME `surface` hook (its color now comes from the material's surface + BXDF, not the old fixed
    // vertex-color + hard-coded lambert). The renderer binds it for the transparent draw, keyed by the material.
    // Runs alongside the G-buffer PS generation (before shader prep + before ValidateMaterials).
    MaterialCookVector<GeneratedMaterialPixelShader> generatedAvboitAccumulatePixelShaders(materialCookArena);
    if(!EmitMaterialAvboitAccumulatePixelShaders(
        materialCookArena,
        context.resolvedPaths.cacheDirectory,
        context.configurationSafeName,
        materialEntries,
        generatedAvboitAccumulatePixelShaders,
        context.scratchArena
    ))
        return false;

    auto& shaderCookArena = graphicsMetadata.shaderEntries.get_allocator().arena();
    const auto appendGeneratedPixelShaderEntry = [&](const GeneratedMaterialPixelShader& generatedPixelShader) -> bool{
        ShaderCook::ShaderEntry pixelShaderEntry(shaderCookArena);
        pixelShaderEntry.name.assign(AStringView(generatedPixelShader.name));
        pixelShaderEntry.source.assign(AStringView(generatedPixelShader.source));
        if(!pixelShaderEntry.stage.assign(AStringView("ps")) ||
           !pixelShaderEntry.archiveStage.assign(AStringView("ps")) ||
           !pixelShaderEntry.targetProfile.assign(AStringView("spirv_1_5"))){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: failed to allocate generated pixel shader entry"));
            return false;
        }
        pixelShaderEntry.includeRoots.push_back(ShaderCook::CookString(AStringView("engine/graphics"), shaderCookArena));
        pixelShaderEntry.emitMeshComputeShadow = false;

        const AssetsGraphicsCookDetail::PreparedShaderKey shaderIdentityKey{
            ToName(pixelShaderEntry.name),
            ToName(pixelShaderEntry.archiveStage.view())
        };
        if(!graphicsMetadata.seenShaderIdentityKeys.insert(shaderIdentityKey).second){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: duplicate generated pixel shader identity '{}'")
                , StringConvert(pixelShaderEntry.name)
            );
            return false;
        }
        graphicsMetadata.shaderEntries.push_back(Move(pixelShaderEntry));
        return true;
    };
    for(const GeneratedMaterialPixelShader& generatedPixelShader : generatedPixelShaders){
        if(!appendGeneratedPixelShaderEntry(generatedPixelShader))
            return false;
    }
    for(const GeneratedMaterialPixelShader& generatedPixelShader : generatedAvboitAccumulatePixelShaders){
        if(!appendGeneratedPixelShaderEntry(generatedPixelShader))
            return false;
    }

    if(!AssetsGraphicsCookDetail::PrepareShaderEntriesForCook(
        context.arena,
        graphicsMetadata.shaderCook,
        context.resolvedPaths,
        materialBindIncludeRoot,
        csgShapeIncludeRoot,
        deferredBxdfIncludeRoot,
        shadowTransmittanceIncludeRoot,
        graphicsMetadata.includeMetadata,
        graphicsMetadata.shaderEntries,
        materialEntries,
        graphicsMetadata.preparedPlan,
        context.scratchArena
    ))
        return false;

    if(!AssetsGraphicsCookDetail::ValidateMaterials(
        graphicsMetadata.shaderCook,
        graphicsMetadata.preparedPlan.preparedEntries,
        materialEntries,
        context.scratchArena
    ))
        return false;
    if(!Core::Assets::AddPlannedFileCount(graphicsMetadata.preparedPlan.plannedFileCount, context.plannedFileCount))
        return false;

    context.externalWriters.emplace_back([&context](
        Core::Filesystem::VolumeSession& volumeSession,
        Core::Assets::CookEntryPathHashSet& seenVirtualPathHashes,
        Core::Assets::ScratchArena& writeScratchArena
    ){
        return AssetsGraphicsCookDetail::AppendPreparedShadersToVolume(
            context.arena,
            GraphicsMetadata(context.parsedMetadata).shaderCook,
            context.resolvedPaths.cacheDirectory,
            context.configurationSafeName,
            GraphicsMetadata(context.parsedMetadata).preparedPlan.preparedEntries,
            volumeSession,
            seenVirtualPathHashes,
            writeScratchArena
        );
    });
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AssetsVolumeCookDetail::AssetVolumePrepareAutoRegistrar s_PrepareGraphicsVolumeAssetsRegistrar(&PrepareGraphicsVolumeAssets);
Core::Assets::AssetMetadataParserAutoRegistrar s_GraphicsVolumeMetadataParserRegistrar(
    &ParseGraphicsDocumentMetadata,
    nullptr
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

