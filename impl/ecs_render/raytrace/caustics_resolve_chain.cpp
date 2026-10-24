// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "caustics_resolve_chain.h"

#include <impl/assets/graphics/caustic/resolve_binding_slots.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/raytrace/raytracing_system.h>

#include <core/graphics/backend_selection/backend.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CausticsResolveChainBuilder::CausticsResolveChainBuilder(Core::GpuTaskGraph& graph, RendererRayTracingSystem& raytracingSystem)noexcept
    : m_graph(graph)
    , m_raytracingSystem(raytracingSystem){
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_caustics_resolve_chain{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ResolveStageDesc{
    const CausticsResolveStageNaming& naming;
    bool applyStateSources;
    CausticsResolveStageUses stageUses;
    Core::GpuTaskId& outTask;
    Expected<Core::GpuTaskId> (RendererRayTracingSystem::*declareTask)(Core::GpuTaskGraph&, const Core::GpuTaskDesc&, const bool&, DeferredFrameTargets&);
};

// One activity read of the previous stage and one activity write of this stage.
inline constexpr usize s_ActivityUsesPerStage = 2u;
inline constexpr usize s_MaxStageResourceUses = 8u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<CausticsResolveChainResult> CausticsResolveChainBuilder::declare(const CausticsResolveChainInputs& inputs, const CausticsResolveChainNaming& naming){
    using namespace RendererTaskGraphDetail;
    CausticsResolveChainResult result{};
    if(!inputs.geometryTask.valid())
        return MakeUnexpected(Failure{});

    const CausticResolveActivitySnapshot activity = m_raytracingSystem.causticResolveActivitySnapshot(inputs.targets);
    Core::GpuGraphResourceId activityResources[2];
    if(activity.valid()){
        for(u32 index = 0u; index < LengthOf(activityResources); ++index){
            Core::GpuGraphResourceDesc desc = BufferResourceDesc(s_CausticResolveActivityBufferNames[index], "caustic resolve activity");
            desc.setInitialState(Core::ResourceStates::Common).setExternalFinalState(Core::ResourceStates::Common);
            const auto activityResourcesIndexResult = m_graph.importBuffer(activity.buffers[index], desc);
            if(!activityResourcesIndexResult){
                activityResources[index] = {};
                return MakeUnexpected(Failure{});
            }
            activityResources[index] = *activityResourcesIndexResult;
        }
    }

    const __hidden_caustics_resolve_chain::ResolveStageDesc stages[] = {
        {naming.prepare, true, inputs.prepare, result.causticResolvePrepareTask, &RendererRayTracingSystem::declareCausticResolvePrepareTask},
        {naming.wavelet, true, inputs.wavelet, result.causticResolveWaveletTask, &RendererRayTracingSystem::declareCausticResolveWaveletTask},
        {naming.secondWavelet, false, inputs.secondWavelet, result.causticResolveSecondWaveletTask, &RendererRayTracingSystem::declareCausticResolveSecondWaveletTask},
        {naming.thirdWavelet, false, inputs.thirdWavelet, result.causticResolveThirdWaveletTask, &RendererRayTracingSystem::declareCausticResolveThirdWaveletTask},
        {naming.fourthWavelet, false, inputs.fourthWavelet, result.causticResolveFourthWaveletTask, &RendererRayTracingSystem::declareCausticResolveFourthWaveletTask},
        {naming.fifthWavelet, false, inputs.fifthWavelet, result.causticResolveFifthWaveletTask, &RendererRayTracingSystem::declareCausticResolveFifthWaveletTask},
        {naming.upsample, false, inputs.upsample, result.causticResolveUpsampleTask, &RendererRayTracingSystem::declareCausticResolveUpsampleTask},
    };
    Core::GpuTaskResourceUse activityUses[__hidden_caustics_resolve_chain::s_MaxStageResourceUses + __hidden_caustics_resolve_chain::s_ActivityUsesPerStage] = {};
    Core::GpuTaskSchedulingHint stageScheduling = inputs.baseScheduling;
    stageScheduling.mergeWithPrevious = true;
    Core::GpuTaskId previousTask = inputs.geometryTask;
    for(usize stageIndex = 0u; stageIndex < LengthOf(stages); ++stageIndex){
        const __hidden_caustics_resolve_chain::ResolveStageDesc& stage = stages[stageIndex];
        Core::GpuTaskDesc stageDesc;
        stageDesc.setIdentity(stage.naming.identity).setMarkerLabel(stage.naming.label).setScheduling(stageScheduling).setDependencies(&previousTask, 1u).setResourceUses(stage.stageUses.data(), stage.stageUses.size());
        if(activity.valid()){
            if(stage.stageUses.size() > __hidden_caustics_resolve_chain::s_MaxStageResourceUses)
                return MakeUnexpected(Failure{});
            usize activityUseCount = 0u;
            for(const Core::GpuTaskResourceUse& use : stage.stageUses)
                activityUses[activityUseCount++] = use;
            // Prepare publishes A; every wavelet alternates masks with its quantized color output.
            if(stageIndex > 0u)
                activityUses[activityUseCount++] = ReadUse(activityResources[(stageIndex - 1u) % 2u], Core::ResourceStates::ShaderResource);
            if(stageIndex <= NWB_CAUSTIC_RESOLVE_PASS_COUNT)
                activityUses[activityUseCount++] = WriteUse(activityResources[stageIndex % 2u], Core::ResourceStates::UnorderedAccess);
            stageDesc.setResourceUses(activityUses, activityUseCount);
        }
        if(!inputs.applyStateSourcesToPrepareOnly || stage.applyStateSources)
            stageDesc.setExternalStateSources(inputs.stateSources.data(), inputs.stateSources.size());
        const auto stageTask = (m_raytracingSystem.*(stage.declareTask))(m_graph, stageDesc, (inputs.producerDispatched), (inputs.targets));
        if(!stageTask){
            stage.outTask = {};
            NWB_LOGGER_WARNING(stage.naming.warnText);
            return MakeUnexpected(stageTask.error());
        }
        stage.outTask = *stageTask;
        previousTask = stage.outTask;
    }

    Core::GpuTaskDesc timingCloseDesc;
    timingCloseDesc.setIdentity(naming.timingClose.identity).setMarkerLabel(naming.timingClose.label).setScheduling(stageScheduling).setDependencies(&result.causticResolveUpsampleTask, 1u);
    const auto resultCausticsTaskResult = m_raytracingSystem.declareCausticResolveTask(m_graph, timingCloseDesc, (inputs.producerDispatched), (inputs.timingTicket), (inputs.resolveTiming));
    if(!resultCausticsTaskResult){
        result.causticsTask = {};
        NWB_LOGGER_WARNING(naming.timingClose.warnText);
        return MakeUnexpected(Failure{});
    }
    result.causticsTask = *resultCausticsTaskResult;
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

