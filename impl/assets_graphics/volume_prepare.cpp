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
#include <impl/assets_volume/cook_extension.h>

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

static Core::Assets::AssetMetadataParseResult ParseGraphicsDocumentMetadata(
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
            Path(shaderEntry.source),
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
            const Path absSource = AbsolutePath(Path(includeEntry.source), errorCode).lexically_normal();
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

    Path materialBindIncludeRoot;
    if(!EmitMaterialBindIncludes(
        context.arena,
        context.resolvedPaths.cacheDirectory,
        context.configurationSafeName,
        graphicsMetadata.materialBindEntries,
        materialBindIncludeRoot,
        context.scratchArena
    ))
        return false;

    Path csgShapeIncludeRoot;
    if(!AssetsCsgCook::EmitCsgShapeModuleIncludes(
        context.resolvedPaths.cacheDirectory,
        context.configurationSafeName,
        graphicsMetadata.csgShapeEntries,
        csgShapeIncludeRoot,
        context.scratchArena
    ))
        return false;

    if(!AssetsGraphicsCookDetail::PrepareShaderEntriesForCook(
        context.arena,
        graphicsMetadata.shaderCook,
        context.resolvedPaths,
        materialBindIncludeRoot,
        csgShapeIncludeRoot,
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
