// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_scene_private.h"
#include "renderer_frame_bindings.h"
#include "task_graph_draw_snapshots.h"

#include <impl/ecs_render/caustic/settings.adhd.h>
#include <impl/ecs_render/shadow/types.adhd.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_renderer_scene_private{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Snapshot>
void CaptureMaterialPassSnapshot(
    const MaterialPassDrawItemPartitionsView& drawItems,
    const CsgFrameGpuDataView& csgFrameData,
    const usize inInstanceCount,
    const usize inMaterialTypedByteCount,
    Snapshot& snapshot
){
    AssignVector(snapshot.regularMeshDrawItems, drawItems.regular.meshDrawItems);
    AssignVector(snapshot.regularIndexedDrawItems, drawItems.regular.indexedDrawItems);
    AssignVector(snapshot.regularComputeDrawItems, drawItems.regular.computeDrawItems);
    AssignVector(snapshot.csgMeshDrawItems, drawItems.csg.meshDrawItems);
    AssignVector(snapshot.csgIndexedDrawItems, drawItems.csg.indexedDrawItems);
    AssignVector(snapshot.csgComputeDrawItems, drawItems.csg.computeDrawItems);
    AssignVector(snapshot.csgReceiverRanges, csgFrameData.receiverRanges);
    AssignVector(snapshot.csgCutters, csgFrameData.cutters);
    snapshot.csgWorkRegion = csgFrameData.workRegion;
    snapshot.instanceCount = inInstanceCount;
    snapshot.materialTypedByteCount = inMaterialTypedByteCount;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 MeshViewBufferSnapshot::bindingValid()const noexcept{
    return buffer && heapHandle.valid() && heapHandle.descriptorClass() == Core::GpuDescriptorClass::UniformBuffer;
}

b8 MeshFrameBindingSnapshot::bindingValid()const noexcept{
    return
        instanceBuffer
        && materialTypedBuffer
        && meshView.bindingValid()
        && instanceHeapHandle.valid()
        && instanceHeapHandle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer
        && materialTypedHeapHandle.valid()
        && materialTypedHeapHandle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer
    ;
}

b8 MeshFrameBindingSnapshot::matches(const MaterialPassBufferSnapshot& materialBuffers, const Core::BufferHandle& meshViewBuffer)const noexcept{
    return
        bindingValid()
        && instanceBuffer == materialBuffers.instanceBuffer
        && materialTypedBuffer == materialBuffers.materialTypedBuffer
        && meshView.buffer == meshViewBuffer
        && instanceBufferCapacity == materialBuffers.instanceBufferCapacity
        && materialTypedBufferCapacity == materialBuffers.materialTypedBufferCapacity
    ;
}

b8 MeshFrameBindingSnapshot::frameReady(const usize instanceCount, const usize materialTypedByteCount)const noexcept{
    if(materialTypedByteCount == 0u || (materialTypedByteCount & (sizeof(u32) - 1u)) != 0u)
        return false;
    return bindingValid() && instanceBufferCapacity >= instanceCount && materialTypedBufferCapacity >= materialTypedByteCount;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void OpaqueMaterialPassGraphSnapshot::capture(
    const MaterialPassDrawItemPartitionsView& drawItems,
    const CsgFrameGpuDataView& csgFrameData,
    const usize inInstanceCount,
    const usize inMaterialTypedByteCount
){
    __hidden_renderer_scene_private::CaptureMaterialPassSnapshot(drawItems, csgFrameData, inInstanceCount, inMaterialTypedByteCount, *this);
    AssignVector(csgReceiverSurfaceMeshDrawItems, drawItems.csgReceiverSurface.meshDrawItems);
    AssignVector(csgReceiverSurfaceIndexedDrawItems, drawItems.csgReceiverSurface.indexedDrawItems);
    AssignVector(csgReceiverSurfaceComputeDrawItems, drawItems.csgReceiverSurface.computeDrawItems);
    captured = true;
}

void TransparentCsgIntervalGraphSnapshot::capture(
    const MaterialPassDrawItemsView& receiverSurfaceDrawItems,
    const CsgFrameGpuDataView& csgFrameData,
    const usize inInstanceCount,
    const usize inMaterialTypedByteCount
){
    AssignVector(receiverSurfaceMeshDrawItems, receiverSurfaceDrawItems.meshDrawItems);
    AssignVector(receiverSurfaceIndexedDrawItems, receiverSurfaceDrawItems.indexedDrawItems);
    AssignVector(receiverSurfaceComputeDrawItems, receiverSurfaceDrawItems.computeDrawItems);
    AssignVector(csgReceiverRanges, csgFrameData.receiverRanges);
    AssignVector(csgCutters, csgFrameData.cutters);
    csgWorkRegion = csgFrameData.workRegion;
    instanceCount = inInstanceCount;
    materialTypedByteCount = inMaterialTypedByteCount;
    captured = true;
}

void TransparentMaterialPassGraphSnapshot::capture(
    const MaterialPassDrawItemPartitionsView& drawItems,
    const CsgFrameGpuDataView& csgFrameData,
    const usize inInstanceCount,
    const usize inMaterialTypedByteCount
){
    __hidden_renderer_scene_private::CaptureMaterialPassSnapshot(drawItems, csgFrameData, inInstanceCount, inMaterialTypedByteCount, *this);
    captured = true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u32 ResolveSceneLights(Core::ECS::World& world, const Span<SceneLightGpuData> outLights, const Span<f32> outCausticImportance)noexcept{
    const usize maxLightCount = Min(outLights.size(), outCausticImportance.size());
    if(maxLightCount == 0u)
        return 0u;

    const u32 capacity = static_cast<u32>(Min<usize>(maxLightCount, NWB_SCENE_MAX_LIGHTS));

    const SIMDMatrix defaultBasis = NWB::Impl::Scene::BuildDefaultSceneViewBasis();
    Array<NWB::Impl::Scene::SceneLight, NWB_SCENE_MAX_LIGHTS> sceneLights{};
    Array<f32, NWB_SCENE_MAX_LIGHTS> shadowImportance{};
    const usize gatheredCount = NWB::Impl::Scene::GatherSceneLights(world, defaultBasis.v[2], Span(sceneLights.data(), capacity));

    for(usize i = 0u; i < gatheredCount; ++i){
        const NWB::Impl::Scene::SceneLight& src = sceneLights[i];
        SceneLightGpuData& dst = outLights[i];
        dst.position = src.position;
        dst.direction = src.direction;
        dst.colorIntensity = src.colorIntensity;
        dst.params = Float4(src.range, static_cast<f32>(src.type), s_ShadowSlotUnassigned, src.enableCaustics ? s_ShadowSlotUnassigned : s_CausticSlotDisabled); // z = shadow slot; w = caustic slot, or disabled when the light did not opt in
        dst.params2 = Float4(src.angularRadius, src.sourceRadius, 0.f, 0.f); // soft-shadow source size
        outCausticImportance[i] = CausticSlotImportanceData(src.colorIntensity);
    }

    // Importance-ranked shadow slots: most important lights get the pool (slot -> params.z; rest stay lit).
    // Selection over <= NWB_SCENE_MAX_LIGHTS lights stays cheap.
    const u32 lightCount = static_cast<u32>(gatheredCount);
    const NWB::Impl::Scene::SceneCameraView cameraView = NWB::Impl::Scene::ResolveSceneCameraView(world, NWB::Impl::Scene::s_CameraFallbackAspectRatio);
    const SIMDVectorSingle cameraPosition = cameraView.valid() ? LoadFloat(cameraView.transform->position) : VectorZero();
    Float4 cameraPositionData;
    StoreFloat(cameraPosition, cameraPositionData);
    for(u32 i = 0u; i < lightCount; ++i){
        const NWB::Impl::Scene::SceneLight& light = sceneLights[i];
        shadowImportance[i] = ShadowSlotImportanceData(
            light.colorIntensity,
            Float4(light.range, static_cast<f32>(light.type), s_ShadowSlotUnassigned, light.enableCaustics ? s_ShadowSlotUnassigned : s_CausticSlotDisabled),
            light.position,
            cameraPositionData
        );
    }

    const u32 slotCount = (lightCount < NWB_SCENE_SHADOW_SLOT_COUNT) ? lightCount : NWB_SCENE_SHADOW_SLOT_COUNT;
    for(u32 slot = 0u; slot < slotCount; ++slot){
        const u32 bestIndex = SelectNextShadowSlotLightIndex(Span<const SceneLightGpuData>(outLights.data(), lightCount), Span<const f32>(shadowImportance.data(), lightCount));
        if(bestIndex >= lightCount)
            break;
        outLights[bestIndex].params.z = static_cast<f32>(slot);
    }

    return static_cast<u32>(gatheredCount);
}

SceneShadingGpuData ResolveSceneShadingState(const f32 fallbackAspectRatio, const u32 lightCount, Core::ECS::World& world)noexcept{
    SceneShadingGpuData state;
    const SIMDMatrix defaultBasis = NWB::Impl::Scene::BuildDefaultSceneViewBasis();

    const NWB::Impl::Scene::SceneCameraView cameraView = NWB::Impl::Scene::ResolveSceneCameraView(world, fallbackAspectRatio);
    const SIMDVectorSingle cameraPosition = cameraView.valid() ? LoadFloat(cameraView.transform->position) : defaultBasis.v[3];
    StoreFloat(VectorSetW(cameraPosition, static_cast<f32>(lightCount)), state.cameraPositionLightCount);
    return state;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

