// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_physics/volume_entry.adhd.h>
#include <impl/assets_physics/cook_source_io.h>

#include <core/assets/cook_dependency_registry.h>
#include <core/assets/cook_metadata.h>
#include <core/assets/volume/volume_prepare_registry.h>
#include <core/metascript/parser.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NWB::Impl{
namespace __hidden_assets_physics_volume_entry_registration{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static b8 IsPhysicsExternalSourceKey(const AStringView key)noexcept{
    return key == "source_mesh"
        || key == "source_heightmap"
        || key == "hole_mask_image"
        || key == "source_material"
        || key == "source_tet_mesh"
        || key == "urdf_file"
        || key == "mjcf_file"
        || key == "source_skeleton"
    ;
}

[[nodiscard]] static b8 MetaTextMayContainExternalPhysicsSource(const AStringView metaText)noexcept{
    return metaText.find("source_mesh") != AStringView::npos
        || metaText.find("source_heightmap") != AStringView::npos
        || metaText.find("hole_mask_image") != AStringView::npos
        || metaText.find("source_material") != AStringView::npos
        || metaText.find("source_tet_mesh") != AStringView::npos
        || metaText.find("urdf_file") != AStringView::npos
        || metaText.find("mjcf_file") != AStringView::npos
        || metaText.find("source_skeleton") != AStringView::npos
    ;
}

[[nodiscard]] static b8 CollectExternalPathsFromMetascriptValue(
    const Path& ownerNwbFile,
    const Core::Metascript::Value& value,
    Core::Assets::ScratchArena& scratchArena,
    Vector<::Path<Core::Assets::ScratchArena>, Core::Assets::ScratchArena>& outPaths,
    b8& outCanUseCache
)noexcept{
    if(value.isMap()){
        return value.forEachMapEntry([&](const Core::Metascript::MStringView key, const Core::Metascript::Value& fieldVal) -> b8{
            if(IsPhysicsExternalSourceKey(key)){
                if(!fieldVal.isString()){
                    outCanUseCache = false;
                    return false;
                }
                const AStringView refText = fieldVal.asString();
                ::Path<Core::Assets::ScratchArena> resolved(scratchArena);
                PhysicsCookIO::ResolvePhysicsSourcePath(ownerNwbFile, refText, resolved);
                outPaths.push_back(resolved);
                if(key == "source_mesh" && (HasCaseInsensitiveExtensionSuffix(refText, ".gltf") || HasCaseInsensitiveExtensionSuffix(refText, ".glb"))){
                    const b8 gltfOk = PhysicsCookIO::CollectGltfExternalBufferPaths(
                        resolved,
                        [&](const AStringView bufPathText) -> b8{
                            ::Path<Core::Assets::ScratchArena> bufPath(scratchArena, bufPathText);
                            outPaths.push_back(Move(bufPath));
                            return true;
                        }
                    );
                    if(!gltfOk){
                        outCanUseCache = false;
                        return false;
                    }
                }
                if(key == "source_tet_mesh" && HasCaseInsensitiveExtensionSuffix(refText, ".node")){
                    ::Path<Core::Assets::ScratchArena> elePath(resolved);
                    elePath.replaceExtension(".ele");
                    outPaths.push_back(Move(elePath));
                }
                return true;
            }
            return CollectExternalPathsFromMetascriptValue(ownerNwbFile, fieldVal, scratchArena, outPaths, outCanUseCache);
        });
    }
    if(value.isList()){
        const Span<const Core::Metascript::Value> items = value.listView();
        for(usize i = 0u; i < items.size(); ++i){
            if(!CollectExternalPathsFromMetascriptValue(ownerNwbFile, items[i], scratchArena, outPaths, outCanUseCache))
                return false;
        }
    }
    return true;
}

[[nodiscard]] static b8 CollectPhysicsExternalSourceFiles(
    const Core::Assets::CookExternalSourceCollectContext& context,
    Vector<::Path<Core::Assets::ScratchArena>, Core::Assets::ScratchArena>& outPaths,
    b8& outCanUseCache
)noexcept{
    if(!MetaTextMayContainExternalPhysicsSource(context.metaText))
        return true;

    const auto walkDocument = [&](const Core::Metascript::Document& doc)noexcept{
        doc.forEachDeclaration([&](const Core::Metascript::MStringView, const Core::Metascript::MStringView declVar) -> b8{
            const Core::Metascript::Value* varVal = doc.findVariable(declVar);
            if(varVal != nullptr)
                (void)CollectExternalPathsFromMetascriptValue(context.discoveredNwbFile.filePath, *varVal, context.scratchArena, outPaths, outCanUseCache);
            return outCanUseCache;
        });
    };

    if(context.ensureParsedDocument == nullptr){
        outCanUseCache = false;
        return true;
    }

    const Core::Metascript::Document* const parsedDoc = context.ensureParsedDocument(context.ensureParsedDocumentUser);
    if(parsedDoc == nullptr){
        outCanUseCache = false;
        return true;
    }

    walkDocument(*parsedDoc);
    return true;
}


static Core::Assets::CookEntryAutoRegistrar s_PhysicsCookEntriesRegistrar([](Core::Assets::CookEntryRegistry& registry){
    const auto componentLabel = FOUNDATION_TEXT("component");
    return Core::Assets::RegisterCookEntry<RigidBodyComponentCookEntry, RigidBodyComponentAsset, RigidBodyComponentCodec, s_RigidBodyComponentDiagnosticPrefix, true>(
            registry, componentLabel, true, NWB_COOK_DISPATCH(ParseRigidBodyComponentCookMetadata), NWB_COOK_DISPATCH(CookRigidBodyComponentVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<ColliderComponentCookEntry, ColliderComponentAsset, ColliderComponentCodec, s_ColliderComponentDiagnosticPrefix, true>(
            registry, componentLabel, true, NWB_COOK_DISPATCH(ParseColliderComponentCookMetadata), NWB_COOK_DISPATCH(CookColliderComponentVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<JointComponentCookEntry, JointComponentAsset, JointComponentCodec, s_JointComponentDiagnosticPrefix, true>(
            registry, componentLabel, true, NWB_COOK_DISPATCH(ParseJointComponentCookMetadata), NWB_COOK_DISPATCH(CookJointComponentVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<PhysicsWorldConfigComponentCookEntry, PhysicsWorldConfigComponentAsset, PhysicsWorldConfigComponentCodec, s_PhysicsWorldConfigComponentDiagnosticPrefix, true>(
            registry, componentLabel, true, NWB_COOK_DISPATCH(ParsePhysicsWorldConfigComponentCookMetadata), NWB_COOK_DISPATCH(CookPhysicsWorldConfigComponentVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<PhysicsParentComponentCookEntry, PhysicsParentComponentAsset, PhysicsParentComponentCodec, s_PhysicsParentComponentDiagnosticPrefix, true>(
            registry, componentLabel, true, NWB_COOK_DISPATCH(ParsePhysicsParentComponentCookMetadata), NWB_COOK_DISPATCH(CookPhysicsParentComponentVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<CharacterControllerComponentCookEntry, CharacterControllerComponentAsset, CharacterControllerComponentCodec, s_CharacterControllerComponentDiagnosticPrefix, true>(
            registry, componentLabel, true, NWB_COOK_DISPATCH(ParseCharacterControllerComponentCookMetadata), NWB_COOK_DISPATCH(CookCharacterControllerComponentVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<VehicleComponentCookEntry, VehicleComponentAsset, VehicleComponentCodec, s_VehicleComponentDiagnosticPrefix, true>(
            registry, componentLabel, true, NWB_COOK_DISPATCH(ParseVehicleComponentCookMetadata), NWB_COOK_DISPATCH(CookVehicleComponentVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<RagdollComponentCookEntry, RagdollComponentAsset, RagdollComponentCodec, s_RagdollComponentDiagnosticPrefix, true>(
            registry, componentLabel, true, NWB_COOK_DISPATCH(ParseRagdollComponentCookMetadata), NWB_COOK_DISPATCH(CookRagdollComponentVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<FluidVolumeComponentCookEntry, FluidVolumeComponentAsset, FluidVolumeComponentCodec, s_FluidVolumeComponentDiagnosticPrefix, true>(
            registry, componentLabel, true, NWB_COOK_DISPATCH(ParseFluidVolumeComponentCookMetadata), NWB_COOK_DISPATCH(CookFluidVolumeComponentVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<ForceFieldComponentCookEntry, ForceFieldComponentAsset, ForceFieldComponentCodec, s_ForceFieldComponentDiagnosticPrefix, true>(
            registry, componentLabel, true, NWB_COOK_DISPATCH(ParseForceFieldComponentCookMetadata), NWB_COOK_DISPATCH(CookForceFieldComponentVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<ArticulationComponentCookEntry, ArticulationComponentAsset, ArticulationComponentCodec, s_ArticulationComponentDiagnosticPrefix, true>(
            registry, componentLabel, true, NWB_COOK_DISPATCH(ParseArticulationComponentCookMetadata), NWB_COOK_DISPATCH(CookArticulationComponentVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<SoftBodyComponentCookEntry, SoftBodyComponentAsset, SoftBodyComponentCodec, s_SoftBodyComponentDiagnosticPrefix, true>(
            registry, componentLabel, true, NWB_COOK_DISPATCH(ParseSoftBodyComponentCookMetadata), NWB_COOK_DISPATCH(CookSoftBodyComponentVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<FractureComponentCookEntry, FractureComponentAsset, FractureComponentCodec, s_FractureComponentDiagnosticPrefix, true>(
            registry, componentLabel, true, NWB_COOK_DISPATCH(ParseFractureComponentCookMetadata), NWB_COOK_DISPATCH(CookFractureComponentVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<GpuRenderTransformExportComponentCookEntry, GpuRenderTransformExportComponentAsset, GpuRenderTransformExportComponentCodec, s_GpuRenderTransformExportComponentDiagnosticPrefix, true>(
            registry, componentLabel, true, NWB_COOK_DISPATCH(ParseGpuRenderTransformExportComponentCookMetadata), NWB_COOK_DISPATCH(CookGpuRenderTransformExportComponentVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<PhysicsMaterialCookEntry, PhysicsMaterialAsset, PhysicsMaterialAssetCodec, s_PhysicsMaterialDiagnosticPrefix, true>(
            registry, FOUNDATION_TEXT("physics_material"), true, NWB_COOK_DISPATCH(ParsePhysicsMaterialCookMetadata), NWB_COOK_DISPATCH(CookPhysicsMaterialVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<CollisionLayerMatrixCookEntry, CollisionLayerMatrixAsset, CollisionLayerMatrixAssetCodec, s_CollisionLayerMatrixDiagnosticPrefix, true>(
            registry, FOUNDATION_TEXT("collision_layer_matrix"), true, NWB_COOK_DISPATCH(ParseCollisionLayerMatrixCookMetadata), NWB_COOK_DISPATCH(CookCollisionLayerMatrixVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<ConvexHullCookEntry, ConvexHullAsset, ConvexHullAssetCodec, s_ConvexHullDiagnosticPrefix, true>(
            registry, FOUNDATION_TEXT("convex_hull"), true, NWB_COOK_DISPATCH(ParseConvexHullCookMetadata), NWB_COOK_DISPATCH(CookConvexHullVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<CompoundHullCookEntry, CompoundHullAsset, CompoundHullAssetCodec, s_CompoundHullDiagnosticPrefix, true>(
            registry, FOUNDATION_TEXT("compound_hull"), true, NWB_COOK_DISPATCH(ParseCompoundHullCookMetadata), NWB_COOK_DISPATCH(CookCompoundHullVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<CollisionMeshCookEntry, CollisionMeshAsset, CollisionMeshAssetCodec, s_CollisionMeshDiagnosticPrefix, true>(
            registry, FOUNDATION_TEXT("collision_mesh"), true, NWB_COOK_DISPATCH(ParseCollisionMeshCookMetadata), NWB_COOK_DISPATCH(CookCollisionMeshVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<HeightfieldCookEntry, HeightfieldAsset, HeightfieldAssetCodec, s_HeightfieldDiagnosticPrefix, true>(
            registry, FOUNDATION_TEXT("heightfield"), true, NWB_COOK_DISPATCH(ParseHeightfieldCookMetadata), NWB_COOK_DISPATCH(CookHeightfieldVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<SdfCookEntry, SdfAsset, SdfAssetCodec, s_SdfDiagnosticPrefix, true>(
            registry, FOUNDATION_TEXT("physics_sdf"), true, NWB_COOK_DISPATCH(ParseSdfCookMetadata), NWB_COOK_DISPATCH(CookSdfVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<ChainShapeCookEntry, ChainShapeAsset, ChainShapeAssetCodec, s_ChainShapeDiagnosticPrefix, true>(
            registry, FOUNDATION_TEXT("physics_chain"), true, NWB_COOK_DISPATCH(ParseChainShapeCookMetadata), NWB_COOK_DISPATCH(CookChainShapeVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<CompoundShapeCookEntry, CompoundShapeAsset, CompoundShapeAssetCodec, s_CompoundShapeDiagnosticPrefix, true>(
            registry, FOUNDATION_TEXT("physics_compound"), true, NWB_COOK_DISPATCH(ParseCompoundShapeCookMetadata), NWB_COOK_DISPATCH(CookCompoundShapeVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<FractureCookEntry, FractureAsset, FractureAssetCodec, s_FractureDiagnosticPrefix, true>(
            registry, FOUNDATION_TEXT("physics_fracture"), true, NWB_COOK_DISPATCH(ParseFractureCookMetadata), NWB_COOK_DISPATCH(CookFractureVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<SoftBodyCookEntry, SoftBodyAsset, SoftBodyAssetCodec, s_SoftBodyDiagnosticPrefix, true>(
            registry, FOUNDATION_TEXT("physics_soft_body"), true, NWB_COOK_DISPATCH(ParseSoftBodyCookMetadata), NWB_COOK_DISPATCH(CookSoftBodyVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<RagdollCookEntry, RagdollAsset, RagdollAssetCodec, s_RagdollDiagnosticPrefix, true>(
            registry, FOUNDATION_TEXT("physics_ragdoll"), true, NWB_COOK_DISPATCH(ParseRagdollCookMetadata), NWB_COOK_DISPATCH(CookRagdollVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<ArticulationCookEntry, ArticulationAsset, ArticulationAssetCodec, s_ArticulationDiagnosticPrefix, true>(
            registry, FOUNDATION_TEXT("physics_articulation"), true, NWB_COOK_DISPATCH(ParseArticulationCookMetadata), NWB_COOK_DISPATCH(CookArticulationVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<VehicleCookEntry, VehicleAsset, VehicleAssetCodec, s_VehicleDiagnosticPrefix, true>(
            registry, FOUNDATION_TEXT("physics_vehicle"), true, NWB_COOK_DISPATCH(ParseVehicleCookMetadata), NWB_COOK_DISPATCH(CookVehicleVolumeEntry)
        );
});

template<typename EntryT>
[[nodiscard]] static Span<const EntryT> TryCookEntrySpan(const Core::Assets::CookEntryRegistry& registry, const Name& assetType)noexcept{
    if(!registry.has(assetType))
        return {};
    const auto& entries = registry.entries<EntryT>(assetType);
    return Span<const EntryT>(entries.data(), entries.size());
}

[[nodiscard]] static b8 PreparePhysicsSceneValidation(Core::Assets::AssetsVolumeCookDetail::AssetVolumePrepareContext& context){
    const Core::Assets::CookEntryRegistry& registry = context.parsedMetadata.entryRegistry;
    const Span<const SceneCookEntry> scenes = TryCookEntrySpan<SceneCookEntry>(registry, SceneAsset::AssetTypeName());
    if(scenes.empty())
        return true;

    Vector<Name, Core::Assets::ScratchArena> cachedPaths(context.scratchArena);
    for(const Core::Assets::NwbFileAssetCacheRecord& record : context.parsedMetadata.fileCacheRecords){
        if(!record.loadedFromCache)
            continue;
        for(const Name cachedPath : record.cachedVirtualPaths)
            cachedPaths.push_back(cachedPath);
    }

    return ValidateScenePhysicsEntityReferences(
        scenes,
        TryCookEntrySpan<EntityCookEntry>(registry, EntityAsset::AssetTypeName()),
        TryCookEntrySpan<RigidBodyComponentCookEntry>(registry, RigidBodyComponentAsset::AssetTypeName()),
        TryCookEntrySpan<ColliderComponentCookEntry>(registry, ColliderComponentAsset::AssetTypeName()),
        TryCookEntrySpan<JointComponentCookEntry>(registry, JointComponentAsset::AssetTypeName()),
        TryCookEntrySpan<PhysicsParentComponentCookEntry>(registry, PhysicsParentComponentAsset::AssetTypeName()),
        TryCookEntrySpan<VehicleComponentCookEntry>(registry, VehicleComponentAsset::AssetTypeName()),
        TryCookEntrySpan<RagdollComponentCookEntry>(registry, RagdollComponentAsset::AssetTypeName()),
        TryCookEntrySpan<ArticulationComponentCookEntry>(registry, ArticulationComponentAsset::AssetTypeName()),
        TryCookEntrySpan<SoftBodyComponentCookEntry>(registry, SoftBodyComponentAsset::AssetTypeName()),
        Span<const Name>(cachedPaths.data(), cachedPaths.size()),
        true
    );
}


static Core::Assets::CookDependencyAutoRegistrar s_PhysicsDependencyRegistrar(&RegisterPhysicsComponentCookDependencies);
static Core::Assets::CookExternalSourceCollectorAutoRegistrar s_PhysicsExternalSourceRegistrar(&CollectPhysicsExternalSourceFiles);
static Core::Assets::AssetsVolumeCookDetail::AssetVolumePrepareAutoRegistrar s_PhysicsSceneValidationPreparer(&PreparePhysicsSceneValidation);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

