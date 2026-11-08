// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shadow_occluder_material_gather.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


b8 ShadowOccluderMaterialGather::resolve(const Core::ECS::EntityID entity, const Core::Assets::AssetRef<Material>& material, const Scene::TransformComponent* const transform, const u32 meshInstanceIndex, NwbRtInstanceMaterialGpu& outInstanceMaterial, InstanceGpuData& outShadowInstance, b8* const outSceneHasTransparentOccluder){
    outInstanceMaterial = {};
    outShadowInstance = {};
    MaterialSurfaceInfo* materialInfo = nullptr;
    if(const auto materialInfoResult = m_materialSystem.findMaterialSurfaceInfo(material); materialInfoResult){
        materialInfo = materialInfoResult->get();
        if(materialInfo->transparent && outSceneHasTransparentOccluder)
            *outSceneHasTransparentOccluder = true;
        // Trace dispatcher reads Texture2D fields via non-uniform slots. Retain resolved handles in preflight.
        const b8 sampledTexturesReady = materialInfo->surfaceDispatchId == Limit<u32>::s_Max || m_sampledTextureCollector.collect(
            *materialInfo,
            [this](const MaterialSurfaceInfo& surface, MaterialSampledTextureCollector<Core::Alloc::ScratchArena>& pending){
                return AppendPreparedMaterialSurfaceSampledTextures(surface, m_materialSystem.materialState().m_resourceState, pending);
            }
        );
        if(!sampledTexturesReady)
            return false;
        const auto materialContext = m_materialSystem.appendShadowOccluderMaterialContext(entity, *materialInfo, transform, m_shadowMaterialTypedBytes, m_shadowConstantTypedRanges, m_shadowMutableTypedRanges);
        if(!materialContext)
            return false;
        outShadowInstance = materialContext->instance;
        outInstanceMaterial = ResolveInstanceShadowMaterialData(
            materialInfo->surfaceDispatchId,
            materialInfo->shadingModelId,
            materialInfo->transparent,
            materialInfo->refractive,
            materialContext->constantByteOffset,
            meshInstanceIndex
        );
    }
    if(!materialInfo || materialInfo->surfaceDispatchId == Limit<u32>::s_Max)
        m_contentComplete = false;
    // An unavailable hook need not hide a boundary. Unknown/untransparent surfaces cannot use the outside shortcut.
    if(!materialInfo || (materialInfo->transparent && materialInfo->surfaceDispatchId == Limit<u32>::s_Max))
        m_opticalScene.markIncomplete();
    return true;
}


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
