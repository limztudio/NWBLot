// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/csg/renderer_csg_types.h>
#include <impl/ecs_render/material/renderer_draw_types.h>
#include <impl/ecs_render/material/compute_emulation_output_index.h>
#include <impl/ecs_render/material/generated_geometry_validation.h>
#include <impl/ecs_render/material/task_graph_opaque_compute_emulation_plan.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct OpaqueCsgReceiverComputeEmulationGraphPlan{
    using DrawItemVector = Vector<MaterialPassDrawItem, Core::Alloc::GlobalArena>;
    using BufferVector = Vector<Core::BufferHandle, Core::Alloc::GlobalArena>;
    using OutputLayoutVector = Vector<GeneratedGeometryOutputLayout, Core::Alloc::GlobalArena>;
    using ReceiverRangeVector = Vector<CsgReceiverRangeGpuData, Core::Alloc::GlobalArena>;
    using CutterVector = Vector<CsgCutterGpuData, Core::Alloc::GlobalArena>;


public:
    DrawItemVector meshDrawItems;
    DrawItemVector indexedDrawItems;
    DrawItemVector drawItems;
    DrawItemVector regularDrawItems;
    BufferVector outputBuffers;
    OutputLayoutVector outputLayouts;
    BufferVector regularOutputBuffers;
    OutputLayoutVector regularOutputLayouts;
    ReceiverRangeVector receiverRanges;
    CutterVector cutters;
    CsgFrameWorkRegion workRegion;
    b8 captured = false;


public:
    explicit OpaqueCsgReceiverComputeEmulationGraphPlan(Core::Alloc::GlobalArena& arena)noexcept
        : meshDrawItems(arena)
        , indexedDrawItems(arena)
        , drawItems(arena)
        , regularDrawItems(arena)
        , outputBuffers(arena)
        , outputLayouts(arena)
        , regularOutputBuffers(arena)
        , regularOutputLayouts(arena)
        , receiverRanges(arena)
        , cutters(arena)
    {}


public:
    void reset()noexcept;
    [[nodiscard]] b8 capture(const MaterialPassDrawItemsView& receiverSurfaceDrawItems, const MaterialPassDrawItemsView& sourceRegularDrawItems, const CsgFrameGpuDataView& csgFrameData, Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] b8 matches(Core::Alloc::ScratchArena& scratchArena)const;

    [[nodiscard]] MaterialPassDrawItemsView drawItemsView()const noexcept{
        return { meshDrawItems, indexedDrawItems, drawItems };
    }

    [[nodiscard]] CsgFrameGpuDataView csgFrameDataView()const noexcept{
        return { receiverRanges, cutters, workRegion };
    }
};


struct OpaqueCsgIntervalSampleComputeEmulationGraphPlan : OpaqueRegularComputeEmulationGraphPlan{
    using ReceiverRangeVector = Vector<CsgReceiverRangeGpuData, Core::Alloc::GlobalArena>;
    using CutterVector = Vector<CsgCutterGpuData, Core::Alloc::GlobalArena>;


public:
    ReceiverRangeVector receiverRanges;
    CutterVector cutters;
    CsgFrameWorkRegion workRegion;


public:
    explicit OpaqueCsgIntervalSampleComputeEmulationGraphPlan(Core::Alloc::GlobalArena& arena)noexcept
        : OpaqueRegularComputeEmulationGraphPlan(arena)
        , receiverRanges(arena)
        , cutters(arena)
    {}


public:
    void reset()noexcept;
    [[nodiscard]] b8 capture(const MaterialPassDrawItemsView& sourceDrawItems, const CsgFrameGpuDataView& csgFrameData, Core::Alloc::ScratchArena& scratchArena);

    [[nodiscard]] CsgFrameGpuDataView csgFrameDataView()const noexcept{
        return { receiverRanges, cutters, workRegion };
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

