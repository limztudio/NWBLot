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

    DrawItemVector drawItems;
    BufferVector outputBuffers;
    OutputLayoutVector outputLayouts;
    HeapSlotVector outputHeapSlots;
    bool captured = false;

    explicit AvboitAliasFreeComputeEmulationGraphPlan(Core::Alloc::GlobalArena& arena)
        : drawItems(arena)
        , outputBuffers(arena)
        , outputLayouts(arena)
        , outputHeapSlots(arena)
    {}


public:
    void reset(){
        drawItems.clear();
        outputBuffers.clear();
        outputLayouts.clear();
        outputHeapSlots.clear();
        captured = false;
    }

    [[nodiscard]] bool capture(const MaterialPassDrawItems& sourceDrawItems, Core::Alloc::ScratchArena& scratchArena){
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

    [[nodiscard]] bool matches()const{
        return MatchesRetainedGeneratedGeometryOutputs(captured, drawItems, outputBuffers, outputLayouts, outputHeapSlots);
    }

    void materialize(MaterialPassDrawItems& outDrawItems)const{
        outDrawItems.computeDrawItems.reserve(drawItems.size());
        outDrawItems.computeDrawItems.assign(drawItems.begin(), drawItems.end());
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

