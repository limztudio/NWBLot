// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "soft_shadow_temporal_merge_recorder.h"

#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SoftShadowTemporalMergeRecorder::SoftShadowTemporalMergeRecorder(
    const RendererRayTracingState& state, const u32 slotRangeCount, const u32 halfWidth, const u32 halfHeight, const u32 groupsX, const u32 groupsY, const b8 historyReadable,
    Core::CommandList& commandList, DeferredFrameTargets& targets, Core::GpuDescriptorHeap& heap
)noexcept
    : m_state(state)
    , m_commandList(commandList)
    , m_targets(targets)
    , m_heap(heap)
    , m_slotRangeCount(slotRangeCount)
    , m_halfWidth(halfWidth)
    , m_halfHeight(halfHeight)
    , m_groupsX(groupsX)
    , m_groupsY(groupsY)
    , m_historyReadable(historyReadable)
{}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void SoftShadowTemporalMergeRecorder::record(
    const ShadowReprojectMergeHeapResources& resources, const b8 graphOwnsSoftTraceInputState, const b8 graphOwnsMergeCurrentGeometryEntryState, const b8 graphOwnsMergeStaticReadEntryStates,
    const b8 graphOwnsMergeTemporalEntryStates
)const{
    const u32 historyValid = m_historyReadable ? 1u : 0u;
    NWB_ASSERT(resources.softTrace && resources.historyIn && resources.momentsIn && resources.historyOut && resources.momentsOut);
    if(!graphOwnsSoftTraceInputState)
        m_commandList.setTextureState(resources.softTrace, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::ShaderResource);
    if(!graphOwnsMergeTemporalEntryStates){
        if(m_historyReadable){
            m_commandList.setTextureState(resources.historyIn, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::ShaderResource);
            m_commandList.setTextureState(resources.momentsIn, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::ShaderResource);
        }
        m_commandList.setTextureState(resources.historyOut, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
        m_commandList.setTextureState(resources.momentsOut, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
    }
    if(!graphOwnsMergeCurrentGeometryEntryState)
        m_commandList.setTextureState(m_targets.shadowSoftGeometry.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
    if(!graphOwnsMergeStaticReadEntryStates){
        if(m_historyReadable)
            m_commandList.setTextureState(m_targets.shadowSoftGeometryPrev.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        m_commandList.setTextureState(m_targets.worldPosition.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
    }
    m_commandList.setEnableUavBarriersForTexture(resources.historyOut, true);
    m_commandList.setEnableUavBarriersForTexture(resources.momentsOut, true);
    m_commandList.commitBarriers();

    const ShadowReprojectMergePushConstants push = MakeShadowReprojectMergePushConstants(
        Span<const f32>(m_state.m_prevWorldToClip.raw, 16u),
        m_targets.width,
        m_targets.height,
        m_halfWidth,
        m_halfHeight,
        m_slotRangeCount,
        historyValid,
        resources.softTraceSlot,
        resources.historyInSlot,
        resources.momentsInSlot,
        m_targets.bindless.shadowSoftGeometry.slot(),
        m_targets.bindless.shadowSoftGeometryPrev.slot(),
        m_targets.bindless.gbufferWorldPosition.slot(),
        resources.historyOutStorageSlot,
        resources.momentsOutStorageSlot,
        m_targets.shadowReceiverFactor
    );

    m_commandList.setComputeState(RayTracingDetail::MakeComputePassState(m_state.m_shadowReprojectMergePipeline));
    m_heap.bindCompute(m_commandList, *m_state.m_shadowReprojectMergePipeline.get());
    m_commandList.setPushConstants(&push, sizeof(push));
    m_commandList.dispatch(m_groupsX, m_groupsY, 1u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

