// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/caustics_resolve_chain.h>

#include <core/graphics/vulkan/backend.h>

#include <impl/ecs_render/kernel/task_graph_queue_requests.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/raytrace/raytracing_system.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CausticsResolveChainBuilder::CausticsResolveChainBuilder(
    Core::GpuTaskGraph& graph,
    RendererRayTracingSystem& raytracingSystem
)
    : m_graph(graph)
    , m_raytracingSystem(raytracingSystem){
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_caustics_resolve_chain{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ResolveStageDesc{
    const CausticsResolveStageNaming* naming;
    bool applyStateSources;
    const CausticsResolveStageUses* stageUses;
    Core::GpuTaskId* outTask;
    Core::GpuTaskId (RendererRayTracingSystem::*declareTask)(
        Core::GpuTaskGraph&,
        const Core::GpuTaskDesc&,
        DeferredFrameTargets&,
        const bool*,
        bool
    );
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool CausticsResolveChainBuilder::declare(
    const CausticsResolveChainInputs& inputs,
    const CausticsResolveChainNaming& naming,
    const Core::GpuQueueRequest& stageQueue,
    const Core::GpuQueueRequest& timingCloseQueue,
    CausticsResolveChainResult& outResult,
    Core::Alloc::ScratchArena& scratchArena
){
    using namespace RendererTaskGraphDetail;
    outResult = CausticsResolveChainResult{};
    if(
        !inputs.targets
        || !inputs.geometryTask.valid()
        || !inputs.producerDispatched
        || !inputs.timingTicket
        || !inputs.resolveTiming
    )
        return false;

    const CausticResolveActivitySnapshot activity = m_raytracingSystem.causticResolveActivitySnapshot(*inputs.targets);
    Core::GpuGraphResourceId activityResources[2];
    if(activity.valid()){
        const Name names[] = { Name("caustic_resolve_activity_a"), Name("caustic_resolve_activity_b") };
        for(u32 index = 0u; index < LengthOf(activityResources); ++index){
            Core::GpuGraphResourceDesc desc = BufferResourceDesc(names[index], "caustic resolve activity");
            desc.setInitialState(Core::ResourceStates::Common).setExternalFinalState(Core::ResourceStates::Common);
            activityResources[index] = m_graph.importBuffer(activity.buffers[index], desc);
            if(!activityResources[index].valid())
                return false;
        }
    }

    const __hidden_caustics_resolve_chain::ResolveStageDesc stages[] = {
        {&naming.prepare, true, &inputs.prepare, &outResult.causticResolvePrepareTask, &RendererRayTracingSystem::declareCausticResolvePrepareTask},
        {&naming.wavelet, true, &inputs.wavelet, &outResult.causticResolveWaveletTask, &RendererRayTracingSystem::declareCausticResolveWaveletTask},
        {&naming.secondWavelet, false, &inputs.secondWavelet, &outResult.causticResolveSecondWaveletTask, &RendererRayTracingSystem::declareCausticResolveSecondWaveletTask},
        {&naming.thirdWavelet, false, &inputs.thirdWavelet, &outResult.causticResolveThirdWaveletTask, &RendererRayTracingSystem::declareCausticResolveThirdWaveletTask},
        {&naming.fourthWavelet, false, &inputs.fourthWavelet, &outResult.causticResolveFourthWaveletTask, &RendererRayTracingSystem::declareCausticResolveFourthWaveletTask},
        {&naming.fifthWavelet, false, &inputs.fifthWavelet, &outResult.causticResolveFifthWaveletTask, &RendererRayTracingSystem::declareCausticResolveFifthWaveletTask},
        {&naming.upsample, false, &inputs.upsample, &outResult.causticResolveUpsampleTask, &RendererRayTracingSystem::declareCausticResolveUpsampleTask},
    };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> activityUses{ scratchArena };
    if(activity.valid()){
        usize maximumUses = 0u;
        for(const auto& stage : stages)
            maximumUses = Max(maximumUses, static_cast<usize>(stage.stageUses->useCount));
        activityUses.reserve(maximumUses + 2u);
    }
    Core::GpuTaskSchedulingHint stageScheduling = inputs.baseScheduling;
    stageScheduling.mergeWithPrevious = true;
    Core::GpuTaskId previousTask = inputs.geometryTask;
    for(usize stageIndex = 0u; stageIndex < 7u; ++stageIndex){
        const __hidden_caustics_resolve_chain::ResolveStageDesc& stage = stages[stageIndex];
        Core::GpuTaskDesc stageDesc;
        stageDesc
            .setIdentity(stage.naming->identity)
            .setMarkerLabel(stage.naming->label)
            .setQueue(stageQueue)
            .setScheduling(stageScheduling)
            .setDependencies(&previousTask, 1u)
            .setResourceUses(stage.stageUses->uses, stage.stageUses->useCount)
        ;
        if(activity.valid() && stageIndex >= 3u && stageIndex <= 5u){
            activityUses.clear();
            for(usize useIndex = 0u; useIndex < stage.stageUses->useCount; ++useIndex)
                activityUses.push_back(stage.stageUses->uses[useIndex]);
            if(stageIndex == 3u)
                activityUses.push_back(WriteUse(activityResources[0], Core::ResourceStates::UnorderedAccess));
            else if(stageIndex == 4u){
                activityUses.push_back(ReadUse(activityResources[0], Core::ResourceStates::ShaderResource));
                activityUses.push_back(WriteUse(activityResources[1], Core::ResourceStates::UnorderedAccess));
            }
            else
                activityUses.push_back(ReadUse(activityResources[1], Core::ResourceStates::ShaderResource));
            stageDesc.setResourceUses(activityUses.data(), activityUses.size());
        }
        if(!inputs.applyStateSourcesToPrepareOnly || stage.applyStateSources)
            stageDesc.setExternalStateSources(inputs.stateSources, inputs.stateSourceCount);
        *stage.outTask = (m_raytracingSystem.*(stage.declareTask))(
            m_graph,
            stageDesc,
            (*inputs.targets),
            inputs.producerDispatched,
            true
        );
        if(!stage.outTask->valid()){
            NWB_LOGGER_WARNING(stage.naming->warnText);
            return false;
        }
        previousTask = *stage.outTask;
    }

    Core::GpuTaskDesc timingCloseDesc;
    timingCloseDesc
        .setIdentity(naming.timingClose.identity)
        .setMarkerLabel(naming.timingClose.label)
        .setQueue(timingCloseQueue)
        .setScheduling(stageScheduling)
        .setDependencies(&outResult.causticResolveUpsampleTask, 1u)
    ;
    outResult.causticsTask = m_raytracingSystem.declareCausticResolveTask(
        m_graph,
        timingCloseDesc,
        (*inputs.timingTicket),
        inputs.producerDispatched,
        inputs.resolveTiming
    );
    if(!outResult.causticsTask.valid()){
        NWB_LOGGER_WARNING(naming.timingClose.warnText);
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

