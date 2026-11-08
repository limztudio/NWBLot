// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/raytrace/rt_swbvh_helpers.h>


NWB_IMPL_BEGIN


class ShadowOccluderMaterialGather final{
public:
    ShadowOccluderMaterialGather(RendererMaterialSystem& materialSystem, ShadowMaterialSampledTextureCollector& sampledTextureCollector, MaterialTypedByteDataVector& shadowMaterialTypedBytes, ECSRenderDetail::MaterialTypedConstantRangeMap& shadowConstantTypedRanges, ECSRenderDetail::MaterialTypedByteContentRangeMap& shadowMutableTypedRanges, RayTracingOpticalSceneGather& opticalScene, b8& contentComplete)noexcept
        : m_materialSystem(materialSystem), m_sampledTextureCollector(sampledTextureCollector), m_shadowMaterialTypedBytes(shadowMaterialTypedBytes), m_shadowConstantTypedRanges(shadowConstantTypedRanges), m_shadowMutableTypedRanges(shadowMutableTypedRanges), m_opticalScene(opticalScene), m_contentComplete(contentComplete){}
    [[nodiscard]] b8 resolve(Core::ECS::EntityID entity, const Core::Assets::AssetRef<Material>& material, const Scene::TransformComponent* transform, u32 meshInstanceIndex, NwbRtInstanceMaterialGpu& outInstanceMaterial, InstanceGpuData& outShadowInstance, b8* outSceneHasTransparentOccluder = nullptr);

private:
    RendererMaterialSystem& m_materialSystem;
    ShadowMaterialSampledTextureCollector& m_sampledTextureCollector;
    MaterialTypedByteDataVector& m_shadowMaterialTypedBytes;
    ECSRenderDetail::MaterialTypedConstantRangeMap& m_shadowConstantTypedRanges;
    ECSRenderDetail::MaterialTypedByteContentRangeMap& m_shadowMutableTypedRanges;
    RayTracingOpticalSceneGather& m_opticalScene;
    b8& m_contentComplete;
};


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
