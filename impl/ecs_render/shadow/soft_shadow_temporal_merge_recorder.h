// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <core/graphics/rhi/foundation.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererRayTracingState;
struct DeferredFrameTargets;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ShadowReprojectMergeHeapResources{
    Core::Texture* softTrace = nullptr;
    Core::Texture* historyIn = nullptr;
    Core::Texture* momentsIn = nullptr;
    Core::Texture* historyOut = nullptr;
    Core::Texture* momentsOut = nullptr;
    u32 softTraceSlot = 0u;
    u32 historyInSlot = 0u;
    u32 momentsInSlot = 0u;
    u32 historyOutStorageSlot = 0u;
    u32 momentsOutStorageSlot = 0u;
};


class SoftShadowTemporalMergeRecorder final : NoCopy{
public:
    SoftShadowTemporalMergeRecorder(
        const RendererRayTracingState& state, u32 slotRangeCount, u32 halfWidth, u32 halfHeight, u32 groupsX, u32 groupsY, b8 historyReadable,
        Core::CommandList& commandList, DeferredFrameTargets& targets, Core::GpuDescriptorHeap& heap
    )noexcept;


public:
    void record(const ShadowReprojectMergeHeapResources& resources, b8 graphOwnsSoftTraceInputState, b8 graphOwnsMergeCurrentGeometryEntryState, b8 graphOwnsMergeStaticReadEntryStates, b8 graphOwnsMergeTemporalEntryStates)const;


private:
    const RendererRayTracingState& m_state;
    Core::CommandList& m_commandList;
    DeferredFrameTargets& m_targets;
    Core::GpuDescriptorHeap& m_heap;
    u32 m_slotRangeCount;
    u32 m_halfWidth;
    u32 m_halfHeight;
    u32 m_groupsX;
    u32 m_groupsY;
    b8 m_historyReadable;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

