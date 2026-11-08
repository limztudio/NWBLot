// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "refraction_cap_builder.h"

#include <impl/ecs_render/avboit/avboit_frame_targets.h>
#include <impl/ecs_render/avboit/types.adhd.h>
#include <impl/ecs_render/csg/csg_system.h>
#include <impl/ecs_render/csg/csg_graph_resource_snapshot.h>
#include <impl/ecs_render/deferred/deferred_frame_targets.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/kernel/task_graph_scheduling.h>

#include <core/graphics/backend_selection/backend.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_refraction_cap{


struct CaptureCapTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = {Core::GpuQueueCapability::Graphics};


public:
    struct Payload{
        RendererCsgSystem& csgSystem;
        const DeferredFrameTargets& targets;
        ECSRenderDetail::CsgGraphResourceSnapshot csgResources;
        Core::Rect workRect;
        Vector<u32, Core::Alloc::GlobalArena> receiverIndices;


    public:
        explicit Payload(Core::Alloc::GlobalArena& arena, RendererCsgSystem& csgSystemIn, const DeferredFrameTargets& targetsIn)noexcept
            : csgSystem(csgSystemIn)
            , targets(targetsIn)
            , receiverIndices(arena)
        {}
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
        if(!payload.csgResources.refractionCapFillPipeline || !payload.csgResources.bindingValid() || !payload.targets.avboit.refractionFramebuffer || !payload.targets.bindless.slotsBufferDescriptor.valid() || payload.receiverIndices.empty())
            return false;
        commandList.endRenderPass();
        payload.csgSystem.renderRefractionIntervalCaps(commandList, payload.targets, payload.csgResources, payload.workRect, payload.receiverIndices);
        commandList.endRenderPass();
        return true;
    }
};


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Core::GpuTaskId> AvboitRefractionCapBuilder::Declare(const AvboitRefractionCapInputs& inputs){
    using namespace __hidden_refraction_cap;
    auto& scratchArena = inputs.scratchArena;
    auto& graph = inputs.graph;
    auto& arena = inputs.arena;
    auto& csgSystem = inputs.csgSystem;
    const auto& targets = inputs.targets;
    const auto& csgResources = inputs.csgResources;
    const auto& csgFrameData = inputs.csgFrameData;
    const auto commonUses = inputs.commonUses;
    const auto csgUses = inputs.csgUses;
    const auto opaqueDepth = inputs.opaqueDepth;
    const auto depth = inputs.depth;
    const auto normalIor = inputs.normalIor;
    const auto tintCoverage = inputs.tintCoverage;
    const auto instance = inputs.instance;
    const auto specularRoughness = inputs.specularRoughness;
    const auto dependency = inputs.dependency;

    if(!csgResources.refractionCapFillPipeline)
        return MakeUnexpected(Failure{});
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> uses{scratchArena};
    uses.reserve(commonUses.size() + csgUses.size() + s_RefractionCaptureCapExtraUseCapacity);
    uses.assign(commonUses.begin(), commonUses.end());
    uses.insert(uses.end(), csgUses.begin(), csgUses.end());
    uses.push_back(ReadUse(opaqueDepth));
    uses.push_back(ReadWriteUse(depth, Core::ResourceStates::DepthWrite));
    uses.push_back(ReadWriteUse(normalIor, Core::ResourceStates::RenderTarget));
    uses.push_back(ReadWriteUse(tintCoverage, Core::ResourceStates::RenderTarget));
    uses.push_back(ReadWriteUse(instance, Core::ResourceStates::RenderTarget));
    uses.push_back(ReadWriteUse(specularRoughness, Core::ResourceStates::RenderTarget));
    Core::GpuTaskDesc desc = TaskDesc(Name("render.refraction.capture.caps"), "Refraction Capture Caps", dependency);
    desc.setResourceUses(uses);
    if(inputs.sampledTextureSetUse)
        desc.setResourceSetUses({ inputs.sampledTextureSetUse, 1u });
    CaptureCapTask::Payload payload{arena, csgSystem, targets};
    payload.csgResources = csgResources;
    payload.workRect = ToCoreRect(csgFrameData.workRegion.resolveRect(targets.width, targets.height));
    payload.receiverIndices.reserve(csgFrameData.receiverRanges.size());
    for(usize receiverIndex = 0u; receiverIndex < csgFrameData.receiverRanges.size(); ++receiverIndex){
        if(csgFrameData.receiverRanges[receiverIndex].cutterCount != 0u)
            payload.receiverIndices.push_back(static_cast<u32>(receiverIndex));
    }
    if(payload.receiverIndices.empty())
        return MakeUnexpected(Failure{});
    const auto dependencyResult = graph.addTask<CaptureCapTask>(desc, Move(payload));
    if(!dependencyResult)
        return MakeUnexpected(Failure{});
    return *dependencyResult;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

