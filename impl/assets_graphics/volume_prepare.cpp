// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shader_cook_plan.h"

#include <impl/assets_graphics/mesh_subsystem.adhd.h>
#include <impl/assets_csg/asset.h>
#include <impl/assets_csg/cook.h>
#include <impl/assets_csg/shape_module_writer.h>
#include <impl/assets_material/material_shadow_dispatch.h>
#include <impl/assets_material/material_avboit_shader_generator.h>
#include <impl/assets_shader/shader_types.h>
#include <impl/assets_subsystem/cook.h>

#include <core/assets/asset_file_discovery.h>
#include <core/assets/volume/volume_prepare_registry.h>
#include <core/assets/paths.h>
#include <core/common/log.h>
#include <core/graphics/shader_stage_names.h>

#include <global/overflow.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_graphics_volume_prepare{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using MaterialBindEntryVector = ShaderCook::CookVector<MaterialBindEntry>;

struct GraphicsVolumeMetadata{
    ShaderCook shaderCook;
    AssetsGraphicsCookDetail::IncludeMetadataMap includeMetadata;
    AssetsGraphicsCookDetail::ShaderEntryVector shaderEntries;
    MaterialBindEntryVector materialBindEntries;
    AssetsCsgCook::CsgShapeCookEntryVector csgShapeEntries;
    AssetsGraphicsCookDetail::PreparedShaderPlan preparedPlan;
    HashSet<ShaderStageKey, ShaderCook::CookArena, Hasher<ShaderStageKey>, EqualTo<ShaderStageKey>> seenShaderIdentityKeys;


public:
    explicit GraphicsVolumeMetadata(ShaderCook::CookArena& arena)
        : shaderCook(arena)
        , includeMetadata(arena)
        , shaderEntries(arena)
        , materialBindEntries(arena)
        , csgShapeEntries(arena)
        , preparedPlan(arena)
        , seenShaderIdentityKeys(0, Hasher<ShaderStageKey>(), EqualTo<ShaderStageKey>(), arena)
    {}
};

static b8 AppendUniqueShaderEntry(const Path* const nwbFilePath, ShaderCook::ShaderEntry& inOutShaderEntry, GraphicsVolumeMetadata& inOutGraphicsMetadata, AssetsGraphicsCookDetail::ShaderEntryVector& inOutShaderEntries){
    if(inOutShaderEntry.name.empty())
        return true;

    const auto shaderName = ToName(inOutShaderEntry.name);
    const auto stageName = ToName(inOutShaderEntry.archiveStage);
    if(!shaderName){
        if(nwbFilePath)
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: invalid shader identity in meta '{}' (failure {})"), PathToString<tchar>(*nwbFilePath), static_cast<u32>(shaderName.error()));
        else
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: generated shader name has invalid text (failure {})"), static_cast<u32>(shaderName.error()));
        return false;
    }
    if(!stageName){
        if(nwbFilePath)
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: invalid shader stage in meta '{}' (failure {})"), PathToString<tchar>(*nwbFilePath), static_cast<u32>(stageName.error()));
        else
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: generated shader stage has invalid text (failure {})"), static_cast<u32>(stageName.error()));
        return false;
    }
    const Core::Assets::CookString nwbFilePathText = nwbFilePath ? PathToString(inOutShaderEntries.get_allocator().arena(), *nwbFilePath) : Core::Assets::CookString(inOutShaderEntries.get_allocator().arena());
    if(!TryInsertUniqueShaderIdentityKey(
        *shaderName ? **shaderName : s_NameNone,
        *stageName ? **stageName : s_NameNone,
        inOutShaderEntry.name,
        inOutShaderEntry.archiveStage,
        nwbFilePath != nullptr,
        AStringView(nwbFilePathText),
        inOutGraphicsMetadata.seenShaderIdentityKeys
    ))
        return false;

    inOutShaderEntries.push_back(Move(inOutShaderEntry));
    return true;
}

static Core::Assets::AssetMetadataParseResult::Enum ParseGraphicsDocumentMetadata(Core::Assets::AssetDocumentMetadataParseContext& context){
    using namespace Core::Assets;

    if(ShaderAssetTypes::ShaderTypeFromAssetType(context.assetType) != Core::ShaderType::Invalid){
        GraphicsVolumeMetadata& graphicsMetadata = Core::Assets::RequireParsedMetadataExtension<GraphicsVolumeMetadata>(s_GraphicsVolumeMetadataExtensionName, context.parsedMetadata, context.parsedMetadata.arena);
        auto shaderEntryResult = graphicsMetadata.shaderCook.parseShaderMeta(context.discoveredNwbFile.filePath, context.doc, context.scratchArena);
        if(!shaderEntryResult)
            return AssetMetadataParseResult::Error;
        ShaderCook::ShaderEntry& shaderEntry = *shaderEntryResult;

        auto shaderName = AssetPathsDetail::BuildDerivedAssetVirtualPathText(context.discoveredNwbFile.assetRoot, context.discoveredNwbFile.virtualRoot, ::Path<ScratchArena>(context.scratchArena, shaderEntry.source), context.cookArena, context.scratchArena);
        if(!shaderName)
            return AssetMetadataParseResult::Error;
        shaderEntry.name = Move(*shaderName);

        if(!AppendUniqueShaderEntry(&context.discoveredNwbFile.filePath, shaderEntry, graphicsMetadata, graphicsMetadata.shaderEntries))
            return AssetMetadataParseResult::Error;
        return AssetMetadataParseResult::Parsed;
    }

    if(context.assetType == s_IncludeAssetTypeName){
        GraphicsVolumeMetadata& graphicsMetadata = Core::Assets::RequireParsedMetadataExtension<GraphicsVolumeMetadata>(s_GraphicsVolumeMetadataExtensionName, context.parsedMetadata, context.parsedMetadata.arena);
        auto includeEntryResult = graphicsMetadata.shaderCook.parseIncludeMeta(context.discoveredNwbFile.filePath, context.doc, context.scratchArena);
        if(!includeEntryResult)
            return AssetMetadataParseResult::Error;
        ShaderCook::IncludeEntry& includeEntry = *includeEntryResult;

        if(!includeEntry.source.empty() && !includeEntry.defineValues.empty()){
            const ::Path<ScratchArena> sourcePath(context.scratchArena, includeEntry.source);
            auto absSourceResult = AbsolutePath(sourcePath);
            if(!absSourceResult){
                NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: failed to resolve include metadata source '{}' from '{}': {}")
                    , ConvertString(includeEntry.source)
                    , PathToString<tchar>(context.discoveredNwbFile.filePath)
                    , ConvertString(absSourceResult.error().message())
                );
                return AssetMetadataParseResult::Error;
            }
            const ::Path<ScratchArena> absSource = absSourceResult->lexicallyNormal();

            CookString key = PathToString(context.cookArena, absSource);
#if defined(NWB_PLATFORM_WINDOWS)
            CanonicalizeTextInPlace(key);
#endif
            if(!graphicsMetadata.includeMetadata.emplace(Move(key), Move(includeEntry)).second){
                NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: duplicate include metadata for source '{}'"), PathToString<tchar>(absSource));
                return AssetMetadataParseResult::Error;
            }
        }

        return AssetMetadataParseResult::Parsed;
    }

    if(context.assetType == CsgShape::AssetTypeName()){
        GraphicsVolumeMetadata& graphicsMetadata = Core::Assets::RequireParsedMetadataExtension<GraphicsVolumeMetadata>(s_GraphicsVolumeMetadataExtensionName, context.parsedMetadata, context.parsedMetadata.arena);
        auto csgShapeEntryResult = AssetsCsgCook::ParseCsgShapeCookMetadata(context.discoveredNwbFile.filePath, context.doc, context.cookArena, context.scratchArena);
        if(!csgShapeEntryResult)
            return AssetMetadataParseResult::Error;

        graphicsMetadata.csgShapeEntries.push_back(Move(*csgShapeEntryResult));
        return AssetMetadataParseResult::Parsed;
    }

    return AssetMetadataParseResult::Unsupported;
}

static b8 ParseMaterialBindFiles(Core::Assets::AssetsVolumeCookDetail::AssetVolumePrepareContext& context, GraphicsVolumeMetadata& inOutGraphicsMetadata){
    const auto bindFiles = Core::Assets::AssetFileDiscovery::Discover(context.resolvedPaths.assetRoots, s_MaterialBindSourceExtensionText, context.arena, context.scratchArena);
    if(!bindFiles)
        return false;

    inOutGraphicsMetadata.materialBindEntries.reserve(bindFiles->size());
    for(const Core::Assets::DiscoveredNwbFile& discoveredBindFile : *bindFiles){
        auto bindResult = ParseMaterialBindSource(discoveredBindFile.filePath, context.arena, context.scratchArena);
        if(!bindResult)
            return false;
        MaterialBindEntry& bindEntry = *bindResult;

        auto virtualPath = Core::Assets::AssetPathsDetail::BuildDerivedAssetVirtualPathText(discoveredBindFile.assetRoot, discoveredBindFile.virtualRoot, discoveredBindFile.filePath, context.arena, context.scratchArena);
        if(!virtualPath)
            return false;
        bindEntry.virtualPath = Move(*virtualPath);

        inOutGraphicsMetadata.materialBindEntries.push_back(Move(bindEntry));
    }

    return true;
}

static b8 PrepareGraphicsVolumeAssets(Core::Assets::AssetsVolumeCookDetail::AssetVolumePrepareContext& context){
    const GraphicsVolumeMetadata* selectedGraphics = Core::Assets::FindParsedMetadataExtension<GraphicsVolumeMetadata>(s_GraphicsVolumeMetadataExtensionName, context.parsedMetadata);
    const Core::Assets::ICookEntryBucket* materialBucket = context.parsedMetadata.entryRegistry.find(Material::AssetTypeName());
    // Only selected graphics metadata or materials require generated modules and the shader index.
    if(!selectedGraphics && (!materialBucket || materialBucket->size() == 0u))
        return true;

    GraphicsVolumeMetadata& graphicsMetadata = Core::Assets::RequireParsedMetadataExtension<GraphicsVolumeMetadata>(s_GraphicsVolumeMetadataExtensionName, context.parsedMetadata, context.parsedMetadata.arena);
    if(!ParseMaterialBindFiles(context, graphicsMetadata))
        return false;
    if(!AssetsCsgCook::AssignCsgShapeCookIds(graphicsMetadata.csgShapeEntries, context.scratchArena))
        return false;

    auto& materialEntries = context.parsedMetadata.entryRegistry.entries<MaterialCookEntry>(Material::AssetTypeName());
    if(graphicsMetadata.shaderEntries.empty() && !materialEntries.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: material assets require at least one shader entry"));
        return false;
    }

    if(!ValidateMaterialCookInterfaces(graphicsMetadata.materialBindEntries, materialEntries, context.scratchArena))
        return false;

    auto materialBindIncludeRootResult = EmitMaterialBindIncludes(context.resolvedPaths.cacheDirectory, context.configurationSafeName, graphicsMetadata.materialBindEntries, context.arena, context.scratchArena);
    if(!materialBindIncludeRootResult)
        return false;
    const Path& materialBindIncludeRoot = *materialBindIncludeRootResult;

    const auto resolveIncludeVirtualSource = [&context](const AStringView ownerKind, const AStringView ownerName, const AStringView fieldLabel, auto& inOutVirtualSource) -> b8{
        if(inOutVirtualSource.empty())
            return true;

        const auto resolvedSource = Core::Assets::ResolveVirtualAssetPath(context.resolvedPaths.assetRoots, AStringView(inOutVirtualSource), context.arena, context.scratchArena);
        if(!resolvedSource){
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: {} '{}' {} '{}' does not resolve against any asset root"), ConvertString(ownerKind), ConvertString(ownerName), ConvertString(fieldLabel), ConvertString(AStringView(inOutVirtualSource)));
            return false;
        }

        auto resolvedText = PathToString(context.scratchArena, *resolvedSource);
        Replace(resolvedText.begin(), resolvedText.end(), '\\', '/');
        inOutVirtualSource.assign(AStringView(resolvedText));
        return true;
    };

    // Resolve each CSG eval path to its absolute source (verbatim include, checksum-covered). Runs before shape includes.
    for(auto& csgShapeEntry : graphicsMetadata.csgShapeEntries){
        if(!resolveIncludeVirtualSource("CSG shape", csgShapeEntry.shapeName.resolvedText(), "eval", csgShapeEntry.evalInclude))
            return false;
    }

    auto csgShapeIncludeRootResult = AssetsCsgCook::CsgShapeModuleWriter().emit(context.resolvedPaths.cacheDirectory, context.configurationSafeName, graphicsMetadata.csgShapeEntries, context.scratchArena);
    if(!csgShapeIncludeRootResult)
        return false;
    Path& csgShapeIncludeRoot = *csgShapeIncludeRootResult;

    // Resolve each material bxdf/surface path to its absolute source (verbatim include, checksum-covered).
    for(auto& materialEntry : materialEntries){
        const AStringView materialName(materialEntry.virtualPath);
        if(!resolveIncludeVirtualSource("material", materialName, s_MaterialBxdfSourceLabel, materialEntry.bxdfSource))
            return false;
        if(!resolveIncludeVirtualSource("material", materialName, s_MaterialSurfaceSourceLabel, materialEntry.surfaceSource))
            return false;
    }

    // Assign BXDF ids before building materials; emit dispatch before shader preparation to include it in checksums.
    if(!AssignMaterialShadingModelIds(materialEntries, context.scratchArena))
        return false;

    auto deferredBxdfIncludeRootResult = EmitDeferredBxdfDispatchModule(context.resolvedPaths.cacheDirectory, context.configurationSafeName, materialEntries, context.scratchArena);
    if(!deferredBxdfIncludeRootResult)
        return false;
    const Path& deferredBxdfIncludeRoot = *deferredBxdfIncludeRootResult;

    // Trace dispatch also needs assigned surface ids and must participate in shader dependency checksums.
    auto shadowSurfaceIncludeRootResult = ShadowSurfaceDispatchGenerator().emit(context.resolvedPaths.cacheDirectory, context.configurationSafeName, graphicsMetadata.materialBindEntries, materialEntries, context.scratchArena);
    if(!shadowSurfaceIncludeRootResult)
        return false;
    const Path& shadowSurfaceIncludeRoot = *shadowSurfaceIncludeRootResult;

    Name sharedMeshShaderName = s_NameNone;
    if(const Core::Assets::ICookEntryBucket* const meshSubsystemBucket = context.parsedMetadata.entryRegistry.find(MeshSubsystem::AssetTypeName()); meshSubsystemBucket && meshSubsystemBucket->size() > 0u){
        const auto& meshSubsystemEntries = context.parsedMetadata.entryRegistry.entries<SubsystemCookEntry<MeshSubsystem>>(MeshSubsystem::AssetTypeName());
        sharedMeshShaderName = meshSubsystemEntries[0u].subsystem.sharedMs.name();
    }

    // Generate per-surface G-buffer PS (pixel generated, mesh shared), like authored entries. Before shader prep.
    auto& materialCookArena = materialEntries.get_allocator().arena();
    auto generatedPixelShadersResult = EmitMaterialPixelShaders(context.resolvedPaths.cacheDirectory, context.configurationSafeName, sharedMeshShaderName, materialCookArena, context.scratchArena, materialEntries);
    if(!generatedPixelShadersResult)
        return false;

    // Generate AVBOIT accumulation from the same surface hook before shader preparation and material validation.
    auto generatedAvboitAccumulatePixelShadersResult = MaterialAvboitShaderGenerator::EmitAccumulate(context.resolvedPaths.cacheDirectory, context.configurationSafeName, materialCookArena, context.scratchArena, materialEntries);
    if(!generatedAvboitAccumulatePixelShadersResult)
        return false;

    // Occupancy, extinction, and accumulation must share the surface hook so renderCoverage agrees across passes.
    auto generatedAvboitOccupancyPixelShadersResult = MaterialAvboitShaderGenerator::EmitOccupancy(context.resolvedPaths.cacheDirectory, context.configurationSafeName, materialCookArena, context.scratchArena, materialEntries);
    if(!generatedAvboitOccupancyPixelShadersResult)
        return false;

    auto generatedAvboitExtinctionPixelShadersResult = MaterialAvboitShaderGenerator::EmitExtinction(context.resolvedPaths.cacheDirectory, context.configurationSafeName, materialCookArena, context.scratchArena, materialEntries);
    if(!generatedAvboitExtinctionPixelShadersResult)
        return false;

    usize shaderEntryCapacity = 0u;
    if(!TryAccumulateGeneratedShaderEntryCapacity(
        graphicsMetadata.shaderEntries.size(),
        generatedPixelShadersResult->size(),
        generatedAvboitAccumulatePixelShadersResult->size(),
        generatedAvboitOccupancyPixelShadersResult->size(),
        generatedAvboitExtinctionPixelShadersResult->size(),
        shaderEntryCapacity
    ))
        return false;
    graphicsMetadata.shaderEntries.reserve(shaderEntryCapacity);
    graphicsMetadata.seenShaderIdentityKeys.reserve(shaderEntryCapacity);

    auto& shaderCookArena = graphicsMetadata.shaderEntries.get_allocator().arena();
    const auto appendGeneratedPixelShaderEntry = [&](const GeneratedMaterialPixelShader& generatedPixelShader) -> b8{
        ShaderCook::ShaderEntry pixelShaderEntry(shaderCookArena);
        pixelShaderEntry.name.assign(AStringView(generatedPixelShader.name));
        pixelShaderEntry.source.assign(AStringView(generatedPixelShader.source));
        pixelShaderEntry.stage = Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::PixelStage);
        pixelShaderEntry.archiveStage = pixelShaderEntry.stage;
        pixelShaderEntry.includeRoots.push_back(ShaderCook::CookString(s_EngineGraphicsIncludeRoot, shaderCookArena));
        pixelShaderEntry.emitMeshComputeShadow = false;
        return AppendUniqueShaderEntry(nullptr, pixelShaderEntry, graphicsMetadata, graphicsMetadata.shaderEntries);
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
        context.resolvedPaths,
        materialBindIncludeRoot,
        csgShapeIncludeRoot,
        deferredBxdfIncludeRoot,
        shadowSurfaceIncludeRoot,
        graphicsMetadata.includeMetadata,
        graphicsMetadata.materialBindEntries,
        materialEntries,
        context.arena,
        context.scratchArena,
        graphicsMetadata.shaderEntries
    );
    if(!preparedPlan)
        return false;
    graphicsMetadata.preparedPlan = Move(*preparedPlan);

    if(!AssetsGraphicsCookDetail::ValidateMaterials(graphicsMetadata.preparedPlan.preparedEntries, materialEntries, context.scratchArena))
        return false;
    if(!Core::Assets::AddPlannedFileCount(graphicsMetadata.preparedPlan.plannedFileCount, context.plannedFileCount))
        return false;

    context.manifestCookers.emplace_back([&context](Core::Assets::AssetsVolumeCookDetail::AssetVolumePackManifest& manifest, Core::Assets::CookEntryPathHashSet& seenVirtualPathHashes, Core::Assets::ScratchArena& writeScratchArena){
        GraphicsVolumeMetadata& metadata = Core::Assets::RequireParsedMetadataExtension<GraphicsVolumeMetadata>(s_GraphicsVolumeMetadataExtensionName, context.parsedMetadata, context.parsedMetadata.arena);
        return AssetsGraphicsCookDetail::AppendPreparedShadersToManifest(
            context.resolvedPaths.cacheDirectory,
            context.configurationSafeName,
            metadata.preparedPlan.preparedEntries,
            context.arena,
            writeScratchArena,
            manifest,
            seenVirtualPathHashes
        );
    });
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Core::Assets::AssetsVolumeCookDetail::AssetVolumePrepareAutoRegistrar S_PrepareGraphicsVolumeAssetsRegistrar(&PrepareGraphicsVolumeAssets);
static Core::Assets::AssetMetadataParserAutoRegistrar S_GraphicsVolumeMetadataParserRegistrar(&ParseGraphicsDocumentMetadata, nullptr);
static Core::Assets::NonStandaloneAssetTypeAutoRegistrar S_MeshSubsystemNonStandaloneAssetTypeRegistrar(MeshSubsystem::AssetTypeName(), "mesh_subsystem");
static Core::Assets::NonStandaloneAssetTypeAutoRegistrar S_IncludeNonStandaloneAssetTypeRegistrar(s_IncludeAssetTypeName, "include");
static Core::Assets::NonStandaloneAssetTypeAutoRegistrar S_ShaderNonStandaloneAssetTypeRegistrar(Name("shader"), "shader");
static Core::Assets::NonStandaloneFirstTokenSuffixAutoRegistrar S_ShaderSuffixNonStandaloneRegistrar("_shader");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

