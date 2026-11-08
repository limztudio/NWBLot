// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/deferred/deferred_frame_targets.h>

#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class DeferredGraphResourceImportResult;


class DeferredCsgTextureImports final{
public:
    void capture(Core::GpuTaskGraph& graph, const DeferredFrameTargets& deferredTargets);
    [[nodiscard]] b8 valid()const noexcept;
    void publish(DeferredGraphResourceImportResult& result)const noexcept;


private:
    Expected<Core::GpuGraphResourceId> m_csgCapBackNormal = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> m_csgIntervalDepth = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> m_csgIntervalId = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> m_csgReceiverEventData = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> m_csgReceiverEventCount = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> m_csgReceiverSpanData = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> m_csgReceiverSpanCount = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> m_csgRemovedIntervalDepth = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> m_csgRemovedIntervalCapNormal = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> m_csgRemovedIntervalData = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> m_csgRemovedIntervalCount = MakeUnexpected(Failure{});
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

