// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/avboit/compute_effect_record_inputs.h>
#include <impl/ecs_render/avboit/compute_emulation_record.h>

#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AvboitExtinctionComputeEmulationGraphTask : public AvboitComputeEmulationGraphTaskBase{
public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context);
};

// Shared-buffer extinction draws keep native D/R order; phases stay graph-visible.
struct AvboitExtinctionSharedComputeEmulationGraphTask : public AvboitSharedComputeEmulationGraphTaskBase{
public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context);
};

struct AvboitExtinctionGraphTask : public AvboitRasterPassGraphTaskBase{
public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context);
};

struct AvboitIntegrationGraphTask{
    // The avboit system, the frame targets, and the timing ticket are required: integration always dispatches against this frame's targets.
    // The timing feedback and scope stay optional: tasks without feedback skip sample attribution.
    using Payload = AvboitComputeEffectRecordInputs;


public:
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context);

    static void Accepted(Payload& payload, const Core::QueueSubmissionToken& token)noexcept{
        AcceptAvboitComputeEffect(payload, token);
    }

    static void Discarded(Payload& payload)noexcept{
        DiscardAvboitComputeEffect(payload);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

