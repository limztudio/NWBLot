// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "post_gbuffer_normalize_builder.h"

#include <impl/ecs_render/deferred/graph_resource_import_builder.h>
#include <impl/ecs_render/deferred/task_graph_post_gbuffer_normalize_task.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/shared/frame_push_constants.adhd.h>

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Core::GpuTaskId> PostGbufferNormalizeBuilder::declare(const PostGbufferNormalizeInputs& inputs, Core::Alloc::ScratchArena& scratchArena){
    using namespace RendererTaskGraphDetail;

    const b8 shadowTraceGeometryStatesGraphOwned = inputs.shadowTraceGeometrySet.valid();
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> normalizeResourceUses{ scratchArena };
    normalizeResourceUses.reserve(s_NormalizeResourceUseCapacity + (shadowTraceGeometryStatesGraphOwned ? 0u : inputs.shadowTraceGeometryResources.size()));
    normalizeResourceUses.push_back(ReadUse(inputs.resources.meshView, Core::ResourceStates::ConstantBuffer));
    normalizeResourceUses.push_back(ReadUse(inputs.resources.normal, Core::ResourceStates::ShaderResource));
    normalizeResourceUses.push_back(ReadUse(inputs.resources.worldPosition, Core::ResourceStates::ShaderResource));
    normalizeResourceUses.push_back(ReadUse(inputs.resources.specularRoughness, Core::ResourceStates::ShaderResource));
    normalizeResourceUses.push_back(ReadUse(inputs.resources.depth, Core::ResourceStates::ShaderResource));
    normalizeResourceUses.push_back(ReadUse(inputs.resources.sceneShading, Core::ResourceStates::ConstantBuffer));
    normalizeResourceUses.push_back(ReadUse(inputs.resources.lights, Core::ResourceStates::ShaderResource));
    normalizeResourceUses.push_back(ReadUse(inputs.resources.currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    normalizeResourceUses.push_back(ReadUse(inputs.resources.materialContextSlots, Core::ResourceStates::ConstantBuffer));
    for(const Core::GpuGraphResourceId resource : inputs.shadowTraceGeometryResources){
        if(!resource.valid())
            return MakeUnexpected(Failure{});
        if(!shadowTraceGeometryStatesGraphOwned)
            normalizeResourceUses.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
    }
    const Core::GpuTaskResourceSetUse shadowTraceGeometrySetUse{ .resourceSet = inputs.shadowTraceGeometrySet, .range = {}, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read, };
    Core::GpuTaskSchedulingHint normalizeScheduling;
    normalizeScheduling.cost = Core::GpuTaskCostHint::Tiny;
    normalizeScheduling.forceSubmissionBoundary = false;
    normalizeScheduling.allowPacketMerge = true;
    normalizeScheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc normalizeDesc;
    normalizeDesc
        .setIdentity(Name(s_FrameGraphNameRenderGraphicsPrefixNormalize))
        .setMarkerLabel("Post-G-Buffer Normalize")
        .setScheduling(normalizeScheduling)
        .setDependencies({ &inputs.gbufferCompletionTask, 1u })
        .setResourceUses(normalizeResourceUses)
        .setResourceSetUses({ shadowTraceGeometryStatesGraphOwned ? &shadowTraceGeometrySetUse : nullptr, shadowTraceGeometryStatesGraphOwned ? 1u : 0u })
    ;
    const auto graphicsPrefixTaskResult = m_graph.addTask<PostGbufferNormalizeGraphTask>(
        normalizeDesc,
        PostGbufferNormalizeGraphTask::Payload{
            .raytracingSystem = m_raytracingSystem,
            .asyncPrefixTiming = &inputs.asyncPrefixTiming,
            .timingTicket = inputs.timingTicket,
            .shadowVisibilityTask = inputs.shadowVisibilityTask,
        }
    );
    if(!graphicsPrefixTaskResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare post-G-buffer normalization task"));
        return MakeUnexpected(Failure{});
    }
    return *graphicsPrefixTaskResult;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

