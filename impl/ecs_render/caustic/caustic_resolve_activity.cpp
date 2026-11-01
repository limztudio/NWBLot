// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "caustic_resolve_state.h"

#include <impl/ecs_render/raytrace/raytracing_system.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>


#include <core/graphics/runtime/runtime.h>
#include <core/graphics/backend_selection/backend.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 CausticResolveActivitySnapshot::valid()const noexcept{
    if(halfWidth == 0u || halfHeight == 0u)
        return false;
    for(u32 index = 0u; index < LengthOf(buffers); ++index){
        if(!buffers[index] || !descriptors[index].valid() || descriptors[index].descriptorClass() != Core::GpuDescriptorClass::StorageBuffer)
            return false;
    }
    return true;
}

b8 CausticResolveActivitySnapshot::matches(const CausticResolveActivitySnapshot& other)const noexcept{
    if(halfWidth != other.halfWidth || halfHeight != other.halfHeight)
        return false;
    for(u32 index = 0u; index < LengthOf(buffers); ++index){
        if(buffers[index] != other.buffers[index] || descriptors[index] != other.descriptors[index])
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CausticResolveActivitySnapshot RendererRayTracingSystem::causticResolveActivitySnapshot(const DeferredFrameTargets& targets)const noexcept{
    const auto& activity = m_rayTracingState.m_causticResolve.m_activity;
    if(!activity.valid() || activity.halfWidth != DivideUp(targets.width, s_CausticResolveDownscaleFactor) || activity.halfHeight != DivideUp(targets.height, s_CausticResolveDownscaleFactor))
        return {};
    return activity;
}

b8 RendererRayTracingSystem::prepareCausticResolveActivity(const u32 halfWidth, const u32 halfHeight){
    const auto& current = m_rayTracingState.m_causticResolve.m_activity;
    if(current.valid() && current.halfWidth == halfWidth && current.halfHeight == halfHeight)
        return true;
    releaseCausticResolveActivity();
    CausticResolveActivityLayout layout{};
    if(!TryResolveCausticActivityLayout(halfWidth, halfHeight, layout))
        return false;
    auto& heap = m_graphics.getDevice().getDescriptorHeap();
    if(!heap.isInitialized())
        return false;
    auto& activity = m_rayTracingState.m_causticResolve.m_activity;
    for(u32 index = 0u; index < LengthOf(activity.buffers); ++index){
        Core::BufferDesc desc;
        desc.setByteSize(layout.bufferByteSize)
            .setCanHaveRawViews(true)
            .setCanHaveUAVs(true)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
            .setDebugName(s_CausticResolveActivityBufferNames[index]);
        auto buffersResult = m_graphics.createBuffer(desc);
        if(buffersResult)
            activity.buffers[index] = Move(*buffersResult);
        else
            activity.buffers[index].reset();
        if(!buffersResult){
            releaseCausticResolveActivity();
            return false;
        }
        const Expected<Core::GpuDescriptorHandle> descriptor = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
        if(descriptor)
            activity.descriptors[index] = *descriptor;
        if(!descriptor || !heap.write(activity.descriptors[index], Core::DescriptorWriteItem::RawBufferUav(0u, activity.buffers[index].get()))){
            releaseCausticResolveActivity();
            return false;
        }
    }
    activity.halfWidth = halfWidth;
    activity.halfHeight = halfHeight;
    return true;
}

void RendererRayTracingSystem::releaseCausticResolveActivity(){
    auto& activity = m_rayTracingState.m_causticResolve.m_activity;
    auto& heap = m_graphics.getDevice().getDescriptorHeap();
    if(heap.isInitialized()){
        for(const Core::GpuDescriptorHandle descriptor : activity.descriptors){
            if(descriptor.valid())
                heap.free(descriptor);
        }
    }
    activity = {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RayTracingCausticsTaskDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void EnableCausticResolveBarriers(Core::CommandList& commandList, DeferredFrameTargets& targets){
    commandList.setEnableUavBarriersForTexture(targets.causticAccumulator.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.causticHistory.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.causticResolveHalf.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.causticResolveGeometry.get(), true);
}

void DispatchCausticResolvePass(
    Core::CommandList& commandList,
    Core::GpuDescriptorHeap& heap,
    Core::ComputePipeline& pipeline,
    DeferredFrameTargets& targets,
    const CausticResolvePassResources& input,
    const CausticResolvePassResources& output,
    const f32 effectiveIntensity,
    const u32 stepWidth,
    const CausticResolveStage::Enum stage,
    const u32 groupsX,
    const u32 groupsY,
    const CausticResolveActivitySnapshot& activity
){
    NWB_ASSERT(input.texture);
    NWB_ASSERT(output.texture);
    NWB_ASSERT(input.texture != output.texture);
    // Sequence indices match the graph: prepare, five wavelets, then upsample.
    const u32 activityPassIndex = stage == CausticResolveStage::Wavelet ? static_cast<u32>(CountTrailingZeros(stepWidth)) + 1u : (stage == CausticResolveStage::PrepareDownsample ? 0u : NWB_CAUSTIC_RESOLVE_PASS_COUNT + 1u);
    const b8 usesActivity = activity.valid();
    const i32 activityInput = usesActivity && activityPassIndex > 0u ? static_cast<i32>((activityPassIndex - 1u) % 2u) : -1;
    const i32 activityOutput = usesActivity && activityPassIndex <= NWB_CAUSTIC_RESOLVE_PASS_COUNT ? static_cast<i32>(activityPassIndex % 2u) : -1;

    const u32 activityInputSlot = activityInput >= 0 ? activity.descriptors[activityInput].slot() : s_RtInvalidSlot;
    const u32 activityOutputSlot = activityOutput >= 0 ? activity.descriptors[activityOutput].slot() : s_RtInvalidSlot;
    const CausticResolvePushConstants resolvePush = ResolveCausticResolvePushConstants(
        targets.width,
        targets.height,
        effectiveIntensity,
        stepWidth,
        stage,
        targets.bindless.gbufferWorldPosition.slot(),
        targets.bindless.gbufferDepth.slot(),
        input.sampledSlot,
        targets.bindless.causticResolveGeometry.slot(),
        targets.bindless.causticAccumulator.slot(),
        output.storageSlot,
        activityInputSlot,
        activityOutputSlot
    );

    Core::ComputeState computeState;
    computeState.setPipeline(&pipeline);
    commandList.setComputeState(computeState);
    heap.bindCompute(commandList, pipeline);
    commandList.setPushConstants(&resolvePush, sizeof(resolvePush));
    commandList.dispatch(groupsX, groupsY, 1u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

