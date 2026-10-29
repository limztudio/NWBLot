// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/material/renderer_draw_types.h>
#include <impl/ecs_render/material/compute_emulation_output_index.h>
#include <impl/ecs_render/material/generated_geometry_state.h>
#include <impl/ecs_render/material/generated_geometry_validation.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AvboitAliasFreeComputeEmulationGraphPlan{
    using DrawItemVector = Vector<MaterialPassDrawItem, Core::Alloc::GlobalArena>;
    using BufferVector = Vector<Core::BufferHandle, Core::Alloc::GlobalArena>;
    using OutputLayoutVector = Vector<GeneratedGeometryOutputLayout, Core::Alloc::GlobalArena>;
    using HeapSlotVector = Vector<u32, Core::Alloc::GlobalArena>;


public:
    DrawItemVector drawItems;
    BufferVector outputBuffers;
    OutputLayoutVector outputLayouts;
    HeapSlotVector outputHeapSlots;
    b8 captured = false;


public:
    explicit AvboitAliasFreeComputeEmulationGraphPlan(Core::Alloc::GlobalArena& arena)noexcept
        : drawItems(arena)
        , outputBuffers(arena)
        , outputLayouts(arena)
        , outputHeapSlots(arena)
    {}


public:
    void reset()noexcept{
        drawItems.clear();
        outputBuffers.clear();
        outputLayouts.clear();
        outputHeapSlots.clear();
        captured = false;
    }

    [[nodiscard]] b8 capture(const MaterialPassDrawItemsView& sourceDrawItems, Core::Alloc::ScratchArena& scratchArena){
        reset();
        if(sourceDrawItems.computeDrawItems.empty())
            return false;

        if(!CaptureAliasFreeGeneratedGeometryDraws(*this, sourceDrawItems, scratchArena, [](const MaterialPipelineCsgMode::Enum mode)noexcept{ return mode == MaterialPipelineCsgMode::None; })){
            reset();
            return false;
        }
        captured = drawItems.size() == outputBuffers.size() && outputBuffers.size() == outputHeapSlots.size() && !drawItems.empty();
        return captured;
    }

    [[nodiscard]] b8 matches()const noexcept{
        return MatchesRetainedGeneratedGeometryOutputs(captured, drawItems, outputBuffers, outputLayouts, outputHeapSlots);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

