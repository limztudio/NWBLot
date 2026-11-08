// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "material_system.h"
#include "material_resource_resolver.h"
#include <impl/ecs_render/material/material_surface_builder.adhd.h>
#include <impl/ecs_render/material/material_instance_override_applier.adhd.h>
#include "sampled_texture_collection.h"

#include <impl/ecs_render/material/instance_typed_ranges.adhd.h>
#include <impl/assets_material/asset.h>
#include <impl/assets_shader/asset.h>
#include <impl/ecs_render/material/renderer_material_state.h>
#include <impl/ecs_render/shader/shader_system.h>

#include <core/assets/manager.h>
#include <core/common/log.h>
#include <core/ecs/world.h>
#include <core/graphics/backend_selection.h>
#include <core/graphics/runtime/runtime.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererMaterialSystem::ApplyMaterialInstanceOverrides(const Core::ECS::EntityID entity, const MaterialSurfaceInfo& materialInfo, const MaterialInstanceComponent& materialInstance, Span<u8> inOutMutableTypedBytes){
    return MaterialInstanceOverrideApplier().apply(
        entity.id,
        materialInfo.materialName,
        materialInfo.materialInterface,
        materialInfo.typedLayoutBlocks,
        materialInfo.typedLayoutFields,
        materialInstance,
        inOutMutableTypedBytes
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<NotNull<MaterialSurfaceInfo*>> RendererMaterialSystem::createMaterialSurfaceInfo(const Core::Assets::AssetRef<Material>& materialAsset){
    const Name materialPath = materialAsset.name();
    if(!materialPath){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: renderer material is empty"));
        return MakeUnexpected(Failure{});
    }

    const auto foundInfo = m_materialState.m_surfaceInfos.find(materialPath);
    if(foundInfo != m_materialState.m_surfaceInfos.end()){
        MaterialSurfaceInfo& info = foundInfo.value();
        if(!RendererMaterialResourceResolver::resolve(info, m_materialState.m_resourceState, m_graphics, m_assetManager))
            return MakeUnexpected(Failure{});
        return MakeNotNull(&info);
    }

    auto loadedAsset = m_assetManager.loadTypedSync<Material>(materialPath, s_RendererAssetOwnerName);
    if(!loadedAsset)
        return MakeUnexpected(Failure{});

    const Material& material = **loadedAsset;

    MaterialSurfaceInfo createdInfo(m_arena);
    // Material::loadBinary already rejected empty shader variants and missing material interfaces.
    NWB_ASSERT(!material.shaderVariant().empty());
    NWB_ASSERT(material.materialInterface());

    const auto pixelShader = material.findShader<PixelShader>();
    const auto meshShader = material.findShader<MeshShader>();
    const b8 hasPixelShader = pixelShader.has_value();
    const b8 hasMeshShader = meshShader.has_value();
    if(pixelShader)
        createdInfo.pixelShader = *pixelShader;
    if(meshShader)
        createdInfo.meshShader = *meshShader;
    createdInfo.avboitAccumulatePixelShader = material.avboitAccumulatePixelShader();
    createdInfo.avboitOccupancyPixelShader = material.avboitOccupancyPixelShader();
    createdInfo.avboitExtinctionPixelShader = material.avboitExtinctionPixelShader();

    // Material::loadBinary already validated the typed layout (hash, blocks, fields, bytes).
    NWB_ASSERT(material.typedLayoutHash() != 0u && !material.typedBlockBytes().empty());
    NWB_ASSERT(material.typedLayoutBlocks().size() <= static_cast<usize>(Limit<u32>::s_Max));
    NWB_ASSERT(material.typedLayoutFields().size() <= static_cast<usize>(Limit<u32>::s_Max));
    NWB_ASSERT(material.typedBlockBytes().size() <= static_cast<usize>(Limit<u32>::s_Max));
    if(!MaterialSurfaceBuilder().populate(
        materialPath,
        AStringView(material.shaderVariant()),
        hasMeshShader,
        hasPixelShader,
        material.transparent(),
        material.materialInterface(),
        material.typedLayoutHash(),
        material.typedLayoutBlocks(),
        material.typedLayoutFields(),
        material.typedBlockBytes(),
        material.resourceReferences(),
        material.shadingModelId(),
        material.surfaceDispatchId(),
        material.twoSided(),
        material.refractive(),
        createdInfo.materialName,
        createdInfo.shaderVariant,
        createdInfo.materialInterface,
        createdInfo.typedLayoutHash,
        createdInfo.typedLayoutBlocks,
        createdInfo.typedLayoutFields,
        createdInfo.resourceReferences,
        createdInfo.constantTypedBytes,
        createdInfo.mutableDefaultTypedBytes,
        createdInfo.unpatchedConstantTypedBytes,
        createdInfo.shadingModelId,
        createdInfo.surfaceDispatchId,
        createdInfo.csgCapSurfaceDispatchAvailable,
        createdInfo.transparent,
        createdInfo.twoSided,
        createdInfo.refractive
    ))
        return MakeUnexpected(Failure{});
    if(!RendererMaterialResourceResolver::resolve(createdInfo, m_materialState.m_resourceState, m_graphics, m_assetManager))
        return MakeUnexpected(Failure{});

    auto result = m_materialState.m_surfaceInfos.try_emplace(materialPath, Move(createdInfo));
    auto it = result.first;
    return MakeNotNull(&it.value());
}

Expected<NotNull<MaterialSurfaceInfo*>> RendererMaterialSystem::findMaterialSurfaceInfo(const Core::Assets::AssetRef<Material>& materialAsset)noexcept{
    const Name materialPath = materialAsset.name();
    if(!materialPath)
        return MakeUnexpected(Failure{});

    const auto foundInfo = m_materialState.m_surfaceInfos.find(materialPath);
    if(foundInfo == m_materialState.m_surfaceInfos.end())
        return MakeUnexpected(Failure{});

    MaterialSurfaceInfo& materialInfo = foundInfo.value();
    if(!materialInfo.resourceReferencesResolved)
        return MakeUnexpected(Failure{});

    return MakeNotNull(&materialInfo);
}

b8 RendererMaterialSystem::prepareVisibleMaterialSurfaceInfos(){
    b8 hasTransparentRenderers = false;
    auto rendererView = m_world.view<RendererComponent>();
    for([[maybe_unused]] auto&& [entity, renderer] : rendererView){
        if(!renderer.visible)
            continue;

        const auto materialInfoResult = createMaterialSurfaceInfo(renderer.material);
        if(!materialInfoResult)
            continue;
        const NotNull<MaterialSurfaceInfo*> materialInfo = *materialInfoResult;
        if(materialInfo->transparent)
            hasTransparentRenderers = true;
    }

    return hasTransparentRenderers;
}

void RendererMaterialSystem::prepareVisibleMaterialInstanceMutableCache(){
    m_materialState.m_instanceMutableCache.prune(m_world);

    auto rendererView = m_world.view<RendererComponent>();
    for(auto&& [entity, renderer] : rendererView){
        if(!renderer.visible)
            continue;

        const MaterialInstanceComponent* materialInstance = m_world.tryGetComponent<MaterialInstanceComponent>(entity);
        if(!materialInstance || materialInstance->overrides.empty())
            continue;

        const auto materialInfoResult = findMaterialSurfaceInfo(renderer.material);
        if(!materialInfoResult)
            continue;
        const NotNull<MaterialSurfaceInfo*> materialInfo = *materialInfoResult;

        if(!prepareMaterialInstanceMutableTypedBytes(entity, *materialInfo, materialInstance))
            continue;
    }
}

b8 RendererMaterialSystem::hasTransparentRenderers(const RendererResourceLookupMode::Enum lookupMode){
    auto materialIsTransparent = [&](const Core::Assets::AssetRef<Material>& material) -> b8{
        const auto materialInfoResult = lookupMode == RendererResourceLookupMode::CreateMissing ? createMaterialSurfaceInfo(material) : findMaterialSurfaceInfo(material);
        if(!materialInfoResult)
            return false;
        const NotNull<MaterialSurfaceInfo*> materialInfo = *materialInfoResult;
        return materialInfo->transparent;
    };

    auto rendererView = m_world.view<RendererComponent>();
    for([[maybe_unused]] auto&& [entity, renderer] : rendererView){
        if(!renderer.visible)
            continue;

        if(materialIsTransparent(renderer.material))
            return true;
    }
    return false;
}

Expected<Span<const u8>> RendererMaterialSystem::prepareMaterialInstanceMutableTypedBytes(const Core::ECS::EntityID entity, const MaterialSurfaceInfo& materialInfo, const MaterialInstanceComponent* materialInstance){
    return m_materialState.m_instanceMutableCache.prepare(m_world, entity, materialInfo, materialInstance);
}

Expected<Span<const u8>> RendererMaterialSystem::findPreparedMaterialInstanceMutableTypedBytes(const Core::ECS::EntityID entity, const MaterialSurfaceInfo& materialInfo, const MaterialInstanceComponent* materialInstance)const noexcept{
    return m_materialState.m_instanceMutableCache.find(entity, materialInfo, materialInstance);
}

Expected<ShadowOccluderMaterialContext> RendererMaterialSystem::appendShadowOccluderMaterialContext(
    const Core::ECS::EntityID entity,
    const MaterialSurfaceInfo& materialInfo,
    const NWB::Impl::Scene::TransformComponent* transform,
    MaterialTypedByteDataVector& inOutMaterialTypedBytes,
    ECSRenderDetail::MaterialTypedConstantRangeMap& inOutConstantRanges,
    ECSRenderDetail::MaterialTypedByteContentRangeMap& inOutMutableRanges
){
    const auto constantRange = ECSRenderDetail::FindOrAppendMaterialConstantTypedByteRange(materialInfo.materialName, materialInfo.typedLayoutHash, materialInfo.constantTypedBytes, inOutMaterialTypedBytes, inOutConstantRanges);
    if(!constantRange)
        return MakeUnexpected(Failure{});

    const MaterialInstanceComponent* materialInstance = m_world.tryGetComponent<MaterialInstanceComponent>(entity);
    const auto mutableTypedBytes = prepareMaterialInstanceMutableTypedBytes(entity, materialInfo, materialInstance);
    if(!mutableTypedBytes)
        return MakeUnexpected(Failure{});
    const auto mutableRange = ECSRenderDetail::FindOrAppendMaterialTypedByteRange(*mutableTypedBytes, inOutMaterialTypedBytes, inOutMutableRanges);
    if(!mutableRange)
        return MakeUnexpected(Failure{});

    NWB_ASSERT(MaterialTypedByteRangeEmptyOffsetValid(*constantRange));
    NWB_ASSERT(MaterialTypedByteRangeEmptyOffsetValid(*mutableRange));
    const NWB::Impl::Scene::TransformComponent defaultTransform;
    const NWB::Impl::Scene::TransformComponent& resolvedTransform = transform ? *transform : defaultTransform;
    ShadowOccluderMaterialContext context;
    context.instance = ::NWB::Impl::BuildInstanceGpuData(entity, transform != nullptr, LoadFloat(resolvedTransform.position), LoadFloat(resolvedTransform.rotation), LoadFloat(resolvedTransform.scale), mutableRange->byteOffset);
    context.constantByteOffset = constantRange->byteOffset;
    return context;
}

void RendererMaterialSystem::releaseMaterialResourceReferences(){
    RendererMaterialResourceResolver::release(m_graphics, m_materialState.m_resourceState);
    for(auto it = m_materialState.m_surfaceInfos.begin(); it != m_materialState.m_surfaceInfos.end(); ++it){
        MaterialSurfaceInfo& materialInfo = it.value();
        materialInfo.constantTypedBytes = materialInfo.unpatchedConstantTypedBytes;
        materialInfo.resourceReferencesResolved = false;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

