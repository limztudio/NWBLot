// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "material_validation.h"
#include "shader_cook_plan.h"
#include "shader_volume_writer.h"

#include <impl/assets_csg/cook.h>
#include <impl/assets_material/cook.h>
#include <impl/assets_material/shader_stage_names.h>
#include <impl/assets_shader/shader_types.h>
#include <impl/assets_shader/cook.h>
#include <core/assets/volume/volume_prepare_registry.h>
#include <core/assets/cook_metadata.h>
#include <core/assets/paths.h>
#include <core/common/log.h>
#include <core/graphics/shader_stage_names.h>


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
    HashSet<AssetsGraphicsCookDetail::ShaderStageKey, ShaderCook::CookArena, AssetsGraphicsCookDetail::ShaderStageKeyHasher, EqualTo<AssetsGraphicsCookDetail::ShaderStageKey>> seenShaderIdentityKeys;

    explicit GraphicsVolumeMetadata(ShaderCook::CookArena& arena)
        : shaderCook(arena)
        , includeMetadata(arena)
        , shaderEntries(arena)
        , materialBindEntries(arena)
        , csgShapeEntries(arena)
        , preparedPlan(arena)
        , seenShaderIdentityKeys(
            0,
            AssetsGraphicsCookDetail::ShaderStageKeyHasher(),
            EqualTo<AssetsGraphicsCookDetail::ShaderStageKey>(),
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

    const AssetsGraphicsCookDetail::ShaderStageKey shaderIdentityKey{
        ToName(shaderEntry.name),
        ToName(shaderEntry.archiveStage.view())
    };
    if(!graphicsMetadata.seenShaderIdentityKeys.insert(shaderIdentityKey).second){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: duplicate shader identity '{}' for stage '{}' from meta '{}'")
            , StringConvert(shaderEntry.name)
            , StringConvert(shaderEntry.archiveStage.view())
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

    if(ShaderAssetTypes::ShaderTypeFromAssetType(context.assetType) != Core::ShaderType::Invalid){
        GraphicsVolumeMetadata& graphicsMetadata = GraphicsMetadata(context.parsedMetadata);
        auto shaderEntryResult = graphicsMetadata.shaderCook.parseShaderMeta(context.discoveredNwbFile.filePath, context.doc, context.scratchArena);
        if(!shaderEntryResult)
            return AssetMetadataParseResult::Error;
        ShaderCook::ShaderEntry& shaderEntry = *shaderEntryResult;

        auto shaderName = Core::Assets::BuildDerivedAssetVirtualPath(
            context.cookArena,
            context.discoveredNwbFile.assetRoot,
            context.discoveredNwbFile.virtualRoot.view(),
            Path(context.cookArena, shaderEntry.source)
        );
        if(!shaderName)
            return AssetMetadataParseResult::Error;
        shaderEntry.name = Move(*shaderName);

        if(!AppendUniqueShaderEntry(shaderEntry, context.discoveredNwbFile.filePath, graphicsMetadata, graphicsMetadata.shaderEntries))
            return AssetMetadataParseResult::Error;
        return AssetMetadataParseResult::Parsed;
    }

    if(context.assetType == s_IncludeAssetTypeName){
        GraphicsVolumeMetadata& graphicsMetadata = GraphicsMetadata(context.parsedMetadata);
        auto includeEntryResult = graphicsMetadata.shaderCook.parseIncludeMeta(context.discoveredNwbFile.filePath, context.doc, context.scratchArena);
        if(!includeEntryResult)
            return AssetMetadataParseResult::Error;
        ShaderCook::IncludeEntry& includeEntry = *includeEntryResult;

        if(!includeEntry.source.empty() && !includeEntry.defineValues.empty()){
            const Path sourcePath(context.cookArena, includeEntry.source);
            auto absSourceResult = AbsolutePath(sourcePath);
            if(!absSourceResult){
                NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: failed to resolve include metadata source '{}' from '{}': {}")
                    , StringConvert(includeEntry.source)
                    , PathToString<tchar>(context.discoveredNwbFile.filePath)
                    , StringConvert(absSourceResult.error().message())
                );
                return AssetMetadataParseResult::Error;
            }
            const Path absSource = absSourceResult->lexicallyNormal();

            ScratchString key = PathToString(context.scratchArena, absSource);
            CanonicalizeTextInPlace(key);
            CookString cookKey(key, context.cookArena);
            if(!graphicsMetadata.includeMetadata.emplace(Move(cookKey), Move(includeEntry)).second){
                NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: duplicate include metadata for source '{}'")
                    , PathToString<tchar>(absSource)
                );
                return AssetMetadataParseResult::Error;
            }
        }

        return AssetMetadataParseResult::Parsed;
    }

    if(context.assetType == AssetsCsgCook::s_CsgShapeAssetTypeName){
        GraphicsVolumeMetadata& graphicsMetadata = GraphicsMetadata(context.parsedMetadata);
        auto csgShapeEntryResult = AssetsCsgCook::ParseCsgShapeCookMetadata(
            context.cookArena, context.discoveredNwbFile.filePath, context.doc, context.scratchArena
        );
        if(!csgShapeEntryResult)
            return AssetMetadataParseResult::Error;

        graphicsMetadata.csgShapeEntries.push_back(Move(*csgShapeEntryResult));
        return AssetMetadataParseResult::Parsed;
    }

    return AssetMetadataParseResult::Unsupported;
}

static bool ParseMaterialBindFiles(Core::Assets::AssetsVolumeCookDetail::AssetVolumePrepareContext& context, GraphicsVolumeMetadata& graphicsMetadata){
    const auto bindFiles = Core::Assets::DiscoverFilesWithExtension(
        context.arena,
        context.resolvedPaths.assetRoots,
        MaterialBindNames::SourceExtensionText(),
        context.scratchArena
    );
    if(!bindFiles)
        return false;

    graphicsMetadata.materialBindEntries.reserve(bindFiles->size());
    for(const Core::Assets::DiscoveredNwbFile& discoveredBindFile : *bindFiles){
        auto bindResult = ParseMaterialBindSource(discoveredBindFile.filePath, context.arena, context.scratchArena);
        if(!bindResult)
            return false;
        MaterialBindEntry& bindEntry = *bindResult;

        auto virtualPath = Core::Assets::BuildDerivedAssetVirtualPath(
            context.arena,
            discoveredBindFile.assetRoot,
            discoveredBindFile.virtualRoot.view(),
            discoveredBindFile.filePath
        );
        if(!virtualPath)
            return false;
        bindEntry.virtualPath = Move(*virtualPath);

        graphicsMetadata.materialBindEntries.push_back(Move(bindEntry));
    }

    return true;
}

static bool PrepareGraphicsVolumeAssets(Core::Assets::AssetsVolumeCookDetail::AssetVolumePrepareContext& context){
    const GraphicsVolumeMetadata* selectedGraphics = Core::Assets::FindParsedMetadataExtension<GraphicsVolumeMetadata>(
        context.parsedMetadata,
        s_GraphicsVolumeMetadataExtensionName
    );
    const Core::Assets::ICookEntryBucket* materialBucket = context.parsedMetadata.entryRegistry.find(Material::AssetTypeName());
    // Only selected graphics metadata or materials require generated modules and the shader index.
    if(!selectedGraphics && (!materialBucket || materialBucket->size() == 0u))
        return true;

    GraphicsVolumeMetadata& graphicsMetadata = GraphicsMetadata(context.parsedMetadata);
    if(!ParseMaterialBindFiles(context, graphicsMetadata))
        return false;
    if(!AssetsCsgCook::AssignCsgShapeCookIds(graphicsMetadata.csgShapeEntries))
        return false;

    auto& materialEntries = context.parsedMetadata.entryRegistry.entries<MaterialCookEntry>(Material::AssetTypeName());
    if(graphicsMetadata.shaderEntries.empty() && !materialEntries.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: material assets require at least one shader entry"));
        return false;
    }

    if(!ValidateMaterialCookInterfaces(graphicsMetadata.materialBindEntries, materialEntries, context.scratchArena))
        return false;

    auto materialBindIncludeRootResult = EmitMaterialBindIncludes(
        context.arena,
        context.resolvedPaths.cacheDirectory,
        context.configurationSafeName,
        graphicsMetadata.materialBindEntries,
        context.scratchArena
    );
    if(!materialBindIncludeRootResult)
        return false;
    const Path& materialBindIncludeRoot = *materialBindIncludeRootResult;

    // Resolve each CSG `eval` virtual path to its absolute source (cross-asset phase; verbatim #include in the
    // generated module, checksum-covered). Runs before EmitCsgShapeModuleIncludes.
    for(auto& csgShapeEntry : graphicsMetadata.csgShapeEntries){
        if(csgShapeEntry.evalInclude.empty())
            continue;

        auto resolvedEvalSourceResult = Core::Assets::ResolveVirtualAssetPath(
            context.arena,
            context.resolvedPaths.assetRoots,
            AStringView(csgShapeEntry.evalInclude),
            context.scratchArena
        );
        if(!resolvedEvalSourceResult){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: CSG shape '{}' eval '{}' does not resolve against any asset root")
                , StringConvert(csgShapeEntry.shapeName.resolvedText())
                , StringConvert(AStringView(csgShapeEntry.evalInclude))
            );
            return false;
        }

        auto resolvedEvalText = PathToString(context.scratchArena, *resolvedEvalSourceResult);
        for(auto& ch : resolvedEvalText){
            if(ch == '\\')
                ch = '/';
        }
        csgShapeEntry.evalInclude.assign(AStringView(resolvedEvalText));
    }

    auto csgShapeIncludeRootResult = AssetsCsgCook::EmitCsgShapeModuleIncludes(
        context.resolvedPaths.cacheDirectory,
        context.configurationSafeName,
        graphicsMetadata.csgShapeEntries,
        context.scratchArena
    );
    if(!csgShapeIncludeRootResult)
        return false;
    Path& csgShapeIncludeRoot = *csgShapeIncludeRootResult;

    // Resolve each material `bxdf`/`surface` virtual path to its absolute source (verbatim #include, checksum-covered,
    // mirroring `interface` -> .bind resolution).
    static constexpr AStringView s_BxdfSourceLabel = "bxdf";
    static constexpr AStringView s_SurfaceSourceLabel = "surface";
    const auto resolveMaterialVirtualSource = [&context](auto& virtualSource, const AStringView label, const AStringView materialName) -> bool {
        if(virtualSource.empty())
            return true;

        const auto resolvedSource = Core::Assets::ResolveVirtualAssetPath(
            context.arena,
            context.resolvedPaths.assetRoots,
            AStringView(virtualSource),
            context.scratchArena
        );
        if(!resolvedSource){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: material '{}' {} '{}' does not resolve against any asset root")
                , StringConvert(materialName)
                , StringConvert(label)
                , StringConvert(AStringView(virtualSource))
            );
            return false;
        }

        auto resolvedText = PathToString(context.scratchArena, *resolvedSource);
        for(auto& ch : resolvedText){
            if(ch == '\\')
                ch = '/';
        }
        virtualSource.assign(AStringView(resolvedText));
        return true;
    };
    for(auto& materialEntry : materialEntries){
        const AStringView materialName(materialEntry.virtualPath);
        if(!resolveMaterialVirtualSource(materialEntry.bxdfSource, s_BxdfSourceLabel, materialName))
            return false;
        if(!resolveMaterialVirtualSource(materialEntry.surfaceSource, s_SurfaceSourceLabel, materialName))
            return false;
    }

    // Assign BXDF ids before building materials; emit dispatch before shader preparation to include it in checksums.
    if(!AssignMaterialShadingModelIds(materialEntries, context.scratchArena))
        return false;

    auto deferredBxdfIncludeRootResult = EmitDeferredBxdfDispatchModule(
        context.resolvedPaths.cacheDirectory,
        context.configurationSafeName,
        materialEntries,
        context.scratchArena
    );
    if(!deferredBxdfIncludeRootResult)
        return false;
    const Path& deferredBxdfIncludeRoot = *deferredBxdfIncludeRootResult;

    // Trace dispatch also needs assigned surface ids and must participate in shader dependency checksums.
    auto shadowSurfaceIncludeRootResult = EmitShadowSurfaceDispatchModule(
        context.resolvedPaths.cacheDirectory,
        context.configurationSafeName,
        graphicsMetadata.materialBindEntries,
        materialEntries,
        context.scratchArena
    );
    if(!shadowSurfaceIncludeRootResult)
        return false;
    const Path& shadowSurfaceIncludeRoot = *shadowSurfaceIncludeRootResult;

    // Generate per-`surface` G-buffer PS (pixel = generated, mesh = shared), synthesize shader entries like authored
    // ones. Before shader prep + ValidateMaterials.
    auto& materialCookArena = materialEntries.get_allocator().arena();
    static constexpr AStringView s_SharedMeshProgramName = "engine/graphics/mesh/shared_ms";
    auto generatedPixelShadersResult = EmitMaterialPixelShaders(
        materialCookArena,
        context.resolvedPaths.cacheDirectory,
        context.configurationSafeName,
        s_SharedMeshProgramName,
        materialEntries,
        context.scratchArena
    );
    if(!generatedPixelShadersResult)
        return false;

    // Generate AVBOIT accumulation from the same surface hook before shader preparation and material validation.
    auto generatedAvboitAccumulatePixelShadersResult = EmitMaterialAvboitAccumulatePixelShaders(
        materialCookArena,
        context.resolvedPaths.cacheDirectory,
        context.configurationSafeName,
        materialEntries,
        context.scratchArena
    );
    if(!generatedAvboitAccumulatePixelShadersResult)
        return false;

    // Occupancy, extinction, and accumulation must share the surface hook so renderCoverage agrees across passes.
    auto generatedAvboitOccupancyPixelShadersResult = EmitMaterialAvboitOccupancyPixelShaders(
        materialCookArena,
        context.resolvedPaths.cacheDirectory,
        context.configurationSafeName,
        materialEntries,
        context.scratchArena
    );
    if(!generatedAvboitOccupancyPixelShadersResult)
        return false;

    auto generatedAvboitExtinctionPixelShadersResult = EmitMaterialAvboitExtinctionPixelShaders(
        materialCookArena,
        context.resolvedPaths.cacheDirectory,
        context.configurationSafeName,
        materialEntries,
        context.scratchArena
    );
    if(!generatedAvboitExtinctionPixelShadersResult)
        return false;

    auto& shaderCookArena = graphicsMetadata.shaderEntries.get_allocator().arena();
    const auto appendGeneratedPixelShaderEntry = [&](const GeneratedMaterialPixelShader& generatedPixelShader) -> bool{
        ShaderCook::ShaderEntry pixelShaderEntry(shaderCookArena);
        pixelShaderEntry.name.assign(AStringView(generatedPixelShader.name));
        pixelShaderEntry.source.assign(AStringView(generatedPixelShader.source));
        if(
            !pixelShaderEntry.stage.assign(Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::PixelStage))
            || !pixelShaderEntry.archiveStage.assign(Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::PixelStage))
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: failed to allocate generated pixel shader entry"));
            return false;
        }
        static constexpr AStringView s_EngineGraphicsIncludeRoot = "engine/graphics";
        pixelShaderEntry.includeRoots.push_back(ShaderCook::CookString(s_EngineGraphicsIncludeRoot, shaderCookArena));
        pixelShaderEntry.emitMeshComputeShadow = false;

        const AssetsGraphicsCookDetail::ShaderStageKey shaderIdentityKey{
            ToName(pixelShaderEntry.name),
            ToName(pixelShaderEntry.archiveStage.view())
        };
        if(!graphicsMetadata.seenShaderIdentityKeys.insert(shaderIdentityKey).second){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: duplicate generated pixel shader identity '{}'")
                , StringConvert(pixelShaderEntry.name)
            );
            return false;
        }
        graphicsMetadata.shaderEntries.push_back(Move(pixelShaderEntry));
        return true;
    };
    for(const GeneratedMaterialPixelShader& generatedPixelShader : *generatedPixelShadersResult){
        if(!appendGeneratedPixelShaderEntry(generatedPixelShader))
            return false;
    }
    for(const GeneratedMaterialPixelShader& generatedPixelShader : *generatedAvboitAccumulatePixelShadersResult){
        if(!appendGeneratedPixelShaderEntry(generatedPixelShader))
            return false;
    }
    for(const GeneratedMaterialPixelShader& generatedPixelShader : *generatedAvboitOccupancyPixelShadersResult){
        if(!appendGeneratedPixelShaderEntry(generatedPixelShader))
            return false;
    }
    for(const GeneratedMaterialPixelShader& generatedPixelShader : *generatedAvboitExtinctionPixelShadersResult){
        if(!appendGeneratedPixelShaderEntry(generatedPixelShader))
            return false;
    }

    auto preparedPlan = AssetsGraphicsCookDetail::PrepareShaderEntriesForCook(
        context.arena,
        graphicsMetadata.shaderCook,
        context.resolvedPaths,
        materialBindIncludeRoot,
        csgShapeIncludeRoot,
        deferredBxdfIncludeRoot,
        shadowSurfaceIncludeRoot,
        graphicsMetadata.includeMetadata,
        graphicsMetadata.shaderEntries,
        materialEntries,
        context.scratchArena
    );
    if(!preparedPlan)
        return false;
    graphicsMetadata.preparedPlan = Move(*preparedPlan);

    if(!AssetsGraphicsCookDetail::ValidateMaterials(
        graphicsMetadata.shaderCook,
        graphicsMetadata.preparedPlan.preparedEntries,
        materialEntries,
        context.scratchArena
    ))
        return false;
    if(!Core::Assets::AddPlannedFileCount(graphicsMetadata.preparedPlan.plannedFileCount, context.plannedFileCount))
        return false;

    context.manifestCookers.emplace_back([&context](
        Core::Assets::AssetsVolumeCookDetail::AssetVolumePackManifest& manifest,
        Core::Assets::CookEntryPathHashSet& seenVirtualPathHashes,
        Core::Assets::ScratchArena& writeScratchArena
    ){
        return AssetsGraphicsCookDetail::AppendPreparedShadersToManifest(
            context.arena,
            GraphicsMetadata(context.parsedMetadata).shaderCook,
            context.resolvedPaths.cacheDirectory,
            context.configurationSafeName,
            GraphicsMetadata(context.parsedMetadata).preparedPlan.preparedEntries,
            manifest,
            seenVirtualPathHashes,
            writeScratchArena
        );
    });
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Core::Assets::AssetsVolumeCookDetail::AssetVolumePrepareAutoRegistrar s_PrepareGraphicsVolumeAssetsRegistrar(&PrepareGraphicsVolumeAssets);
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

