// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/avboit/compute_emulation_record.h>

#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AvboitAccumulationComputeEmulationGraphTask : public AvboitComputeEmulationGraphTaskBase{
public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context);
};


// Shared-buffer accumulation draws cannot batch generators; keep D/R streams explicit.
struct AvboitAccumulationSharedComputeEmulationGraphTask : public AvboitSharedComputeEmulationGraphTaskBase{
public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context);
};


struct AvboitAccumulationGraphTask : public AvboitRasterPassGraphTaskBase{
public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context);
};


// Keep ShaderResource handoffs in a Graphics task right after rasterization; barriers are the contract.
struct AvboitAccumulationFinalizeGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = {};


public:
    struct Payload{};


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context)noexcept;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

