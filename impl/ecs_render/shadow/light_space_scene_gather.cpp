// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "light_space_scene_gather.h"

#include <impl/ecs_render/raytrace/raytracing_scene_identity_builder.adhd.h>
#include <impl/ecs_render/raytrace/rt_swbvh_helpers.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void LightSpaceShadowSceneGather::begin(const usize capacity, const b8 hardware){
    BeginLightSpaceCsgGather(m_state.m_csg, m_world, capacity, hardware);
    if(!hardware)
        m_state.m_sceneBuffers.reserve(capacity * (m_state.m_csg.gathering ? s_SceneBuffersPerCsgCandidate : s_SceneBuffersPerCandidate));
}

void LightSpaceShadowSceneGather::reserveSoftwareCasters(const usize capacity){
    m_softwareCasters.reserve(capacity);
}

void LightSpaceShadowSceneGather::appendSoftwareReceiver(
    const Core::ECS::EntityID entity, const b8 transparent, const SIMDMatrix& objectToWorld, const SIMDMatrix& worldToObject,
    const SIMDVectorSingle localMin, const SIMDVectorSingle localMax, const ECSRenderDetail::MeshRayTracingResourceSnapshot& mesh
){
    RayTracingSoftwareBvhDetail::AppendLightSpaceSceneBuffers(mesh, m_state.m_csg.gathering, m_state.m_sceneBuffers);
    if(m_state.m_csg.gathering)
        AppendLightSpaceCsgReceiver(m_state.m_csg, entity, transparent, objectToWorld, worldToObject, localMin, localMax, mesh);
}

void LightSpaceShadowSceneGather::appendSoftwareCaster(const ECSRenderDetail::MeshRayTracingResourceSnapshot& mesh){
    m_softwareCasters.push_back({
        .triangleIndexBuffer = mesh.triangleIndexBuffer,
        .meshletDescBuffer = mesh.meshletDescBuffer,
        .meshletBoundsBuffer = mesh.meshletLocalBoundsBuffer,
        .meshletCount = mesh.meshletCount,
        .meshletDescSlot = mesh.meshletCount != 0u ? mesh.meshletDescHeapHandle.slot() : 0u,
        .meshletBoundsSlot = mesh.meshletCount != 0u ? mesh.meshletLocalBoundsHeapHandle.slot() : 0u,
    });
}

void LightSpaceShadowSceneGather::appendHardwareReceiver(
    const Core::ECS::EntityID entity, const b8 transparent, const u32 instanceIndex, const SIMDMatrix& objectToWorld,
    const SIMDVectorSingle localMin, const SIMDVectorSingle localMax, const ECSRenderDetail::MeshRayTracingResourceSnapshot& mesh
){
    if(!m_state.m_csg.gathering)
        return;
    SIMDVectorSingle determinant;
    const SIMDMatrix worldToObject = MatrixInverse(&determinant, objectToWorld);
    AppendLightSpaceCsgReceiver(m_state.m_csg, entity, transparent, objectToWorld, worldToObject, localMin, localMax, mesh);
    m_state.m_casters.push_back({
        .triangleIndexBuffer = mesh.triangleIndexBuffer,
        .meshletDescBuffer = mesh.meshletDescBuffer,
        .meshletBoundsBuffer = mesh.meshletLocalBoundsBuffer,
        .instanceIndex = instanceIndex,
        .indexCount = mesh.meshletPrimitiveIndexCount,
        .meshletCount = mesh.meshletCount,
        .meshletDescSlot = mesh.meshletCount != 0u ? mesh.meshletDescHeapHandle.slot() : 0u,
        .meshletBoundsSlot = mesh.meshletCount != 0u ? mesh.meshletLocalBoundsHeapHandle.slot() : 0u,
        .transparent = transparent,
        .csg = false,
    });
    RayTracingSoftwareBvhDetail::AppendLightSpaceSceneBuffers(mesh, true, m_state.m_sceneBuffers);
}

b8 LightSpaceShadowSceneGather::finishSoftware(const Span<const SceneSwBvhInstanceGpu> instances, const Span<NwbRtInstanceMaterialGpu> materials, const b8 contentComplete){
    if(!FinishLightSpaceCsgGather(m_registry, m_state.m_csg, m_world, m_scratchArena))
        return false;
    const auto& csg = m_state.m_csg.snapshot;
    const u32 instanceCount = static_cast<u32>(instances.size());
    m_state.m_sceneEligible = contentComplete && instanceCount <= NWB_LIGHT_SPACE_EVENT_INSTANCE_MASK;
    m_state.m_casters.clear();
    m_state.m_casters.reserve(instanceCount);
    for(u32 index = 0u; index < instanceCount; ++index){
        const u64 indexCount = static_cast<u64>(instances[index].primitiveCount) * s_RayTracingTriangleIndexCount;
        LightSpaceShadowCaster& caster = m_softwareCasters[index];
        const Core::BufferHandle& indexBuffer = caster.triangleIndexBuffer;
        if(indexCount == 0u || indexCount > Limit<u32>::s_Max || !indexBuffer || !indexBuffer->getCreationDescription().isIndexBuffer || indexBuffer->getCreationDescription().byteSize < indexCount * sizeof(u32))
            m_state.m_sceneEligible = false;
        const b8 csgReceiver = csg.hasCsg && (csg.receiverRanges[index].flags & NWB_CSG_RAY_RECEIVER_ACTIVE) != 0u;
        if(csgReceiver)
            materials[index].flags |= NWB_RT_INSTANCE_MATERIAL_FLAG_CSG_SHADOW;
        caster.instanceIndex = index;
        caster.indexCount = static_cast<u32>(indexCount);
        caster.transparent = (materials[index].flags & RtInstanceMaterialFlag::Transparent) != 0u;
        caster.csg = csgReceiver;
        m_state.m_casters.push_back(Move(caster));
    }
    return true;
}

b8 LightSpaceShadowSceneGather::finishHardware(const Span<Core::RayTracingInstanceDesc> instances, const Span<NwbRtInstanceMaterialGpu> materials, const b8 contentComplete, b8& sceneHasTransparentOccluder){
    if(!FinishLightSpaceCsgGather(m_registry, m_state.m_csg, m_world, m_scratchArena))
        return false;
    const auto& csg = m_state.m_csg.snapshot;
    if(csg.hasCsg){
        if(csg.receiverRanges.size() != instances.size())
            return false;
        for(u32 index = 0u; index < static_cast<u32>(instances.size()); ++index){
            if((csg.receiverRanges[index].flags & NWB_CSG_RAY_RECEIVER_ACTIVE) == 0u)
                continue;
            const b8 transparent = (materials[index].flags & RtInstanceMaterialFlag::Transparent) != 0u;
            instances[index].setInstanceMask(ResolveRayTracingCsgInstanceMask(transparent));
            materials[index].flags |= NWB_RT_INSTANCE_MATERIAL_FLAG_CSG_SHADOW;
            m_state.m_casters[index].csg = true;
        }
        sceneHasTransparentOccluder = true;
    }
    m_state.m_sceneEligible = csg.hasCsg && contentComplete;
    if(!csg.hasCsg){
        m_state.m_casters.clear();
        m_state.m_sceneBuffers.clear();
    }
    return true;
}


void LightSpaceShadowSceneGather::appendSoftwareCaptureIdentity(
    const Core::ECS::EntityID entity, const b8 runtimeMesh, const ECSRenderDetail::MeshRayTracingResourceSnapshot& mesh,
    const NwbRtInstanceMaterialGpu& material, const InstanceGpuData& instance, const RayTracingOpticalInstanceGpu& opticalInstance
){
    Fnv64AppendValue(m_captureSceneIdentity, entity.id);
    Fnv64AppendValue(m_captureSceneIdentity, mesh.positionBuffer.get());
    Fnv64AppendValue(m_captureSceneIdentity, mesh.triangleIndexBuffer.get());
    Fnv64AppendValue(m_captureSceneIdentity, mesh.attributeBuffer.get());
    Fnv64AppendValue(m_captureSceneIdentity, mesh.meshletCount);
    Fnv64AppendValue(m_captureSceneIdentity, mesh.meshletDescBuffer.get());
    Fnv64AppendValue(m_captureSceneIdentity, mesh.meshletLocalBoundsBuffer.get());
    Fnv64AppendValue(m_captureSceneIdentity, mesh.meshletDescHeapHandle.value);
    Fnv64AppendValue(m_captureSceneIdentity, mesh.meshletLocalBoundsHeapHandle.value);
    RayTracingSceneIdentityBuilder().updateSoftwareCapture(
        mesh.meshletPrimitiveIndexCount,
        mesh.runtimeMeshVersion,
        mesh.runtimeGeometryContentRevision,
        material,
        instance.translation.w,
        instance.geometryHeapSlots,
        opticalInstance,
        m_captureSceneIdentity
    );
    if(runtimeMesh && mesh.runtimeGeometryContentRevision == 0u)
        m_captureSceneTrusted = false;
}

void LightSpaceShadowSceneGather::publishSoftwareCaptureIdentity(
    const u32 instanceCount, const Span<const NwbRtInstanceMaterialGpu> materials, const Span<const InstanceGpuData> instances,
    const Span<const u8> materialBytes, const Span<const Core::TextureHandle> textures, const b8 contentComplete
){
    RayTracingSceneIdentityBuilder().appendMaterialBytes(instanceCount, materialBytes, m_captureSceneIdentity);
    for(const auto& texture : textures)
        Fnv64AppendValue(m_captureSceneIdentity, texture.get());
    m_state.m_sceneTextures.assign(textures.begin(), textures.end());
    const auto& csg = m_state.m_csg.snapshot;
    if(csg.hasCsg){
        Fnv64AppendValue(m_captureSceneIdentity, csg.identity);
        if(m_state.m_settings.captureCadence == SoftwareShadowCaptureCadence::ReuseOneFrame || m_state.m_settings.captureCadence == SoftwareShadowCaptureCadence::ReuseTwoFrames){
            m_captureSceneIdentity = BuildLightSpaceCsgCaptureIdentity(m_state.m_csg, materials, instances, materialBytes, textures);
            m_captureSceneTrusted = m_state.m_csg.captureGeometryTrusted;
        }
    }
    m_state.m_captureSceneIdentity = m_captureSceneIdentity;
    m_state.m_captureSceneTrusted = m_captureSceneTrusted && contentComplete;
}

void LightSpaceShadowSceneGather::publishHardwareCaptureIdentity(
    const u64 materialHash, const u64 tlasHash, const b8 staticScene, const b8 contentComplete,
    const Span<const NwbRtInstanceMaterialGpu> materials, const Span<const InstanceGpuData> instances,
    const Span<const u8> materialBytes, const Span<const Core::TextureHandle> textures
){
    const auto& csg = m_state.m_csg.snapshot;
    if(!csg.hasCsg)
        return;
    u64 captureIdentity = materialHash;
    Fnv64AppendValue(captureIdentity, tlasHash);
    Fnv64AppendValue(captureIdentity, csg.identity);
    for(const auto& buffer : m_state.m_sceneBuffers)
        Fnv64AppendValue(captureIdentity, buffer.get());
    for(const auto& caster : m_state.m_casters)
        RayTracingSceneIdentityBuilder().updateHardwareCaster(caster.meshletCount, caster.meshletDescSlot, caster.meshletBoundsSlot, captureIdentity);
    b8 captureTrusted = staticScene && contentComplete;
    if(m_state.m_settings.captureCadence == SoftwareShadowCaptureCadence::ReuseOneFrame || m_state.m_settings.captureCadence == SoftwareShadowCaptureCadence::ReuseTwoFrames){
        captureIdentity = BuildLightSpaceCsgCaptureIdentity(m_state.m_csg, materials, instances, materialBytes, textures);
        captureTrusted = m_state.m_csg.captureGeometryTrusted && contentComplete;
    }
    m_state.m_captureSceneIdentity = captureIdentity;
    m_state.m_captureSceneTrusted = captureTrusted;
    m_state.m_sceneTextures.assign(textures.begin(), textures.end());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

