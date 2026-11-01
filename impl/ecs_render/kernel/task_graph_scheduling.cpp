// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_scheduling.h"

#include <impl/ecs_render/kernel/renderer_format_private.h>
#include <impl/ecs_render/kernel/task_graph_clear_timing.h>
#include <impl/ecs_render/kernel/task_graph_float_clear_tasks.h>
#include <impl/ecs_render/kernel/task_graph_queue_lookup.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/kernel/timing_names.h>

#include <core/task/gpu/capture/command_ir.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Core::Format::Enum SelectSupportedFormat(Core::Device& device, const Span<const Core::Format::Enum> candidates, const Core::FormatSupport::Mask requiredSupport)noexcept{
    for(const Core::Format::Enum format : candidates){
        if((device.queryFormatSupport(format) & requiredSupport) == requiredSupport)
            return format;
    }

    return Core::Format::UNKNOWN;
}

b8 CreateClampSampler(Core::Device& device, const b8 linearFiltering, Core::SamplerHandle& sampler){
    if(sampler)
        return true;

    Core::SamplerDesc samplerDesc;
    samplerDesc.setAllFilters(linearFiltering).setAllAddressModes(Core::SamplerAddressMode::Clamp);
    auto samplerResult = device.createSampler(samplerDesc);
    if(samplerResult)
        sampler = Move(*samplerResult);
    else
        sampler.reset();
    if(samplerResult)
        return true;

    return false;
}

b8 FloatTextureClearGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
    Core::Texture* const destination = context.declarations.textureForResource(payload.destination);
    if(!destination || commandList.isRenderPassActive())
        return false;

    const Core::GpuClearTextureTaskDesc clearDesc{ .destination = payload.destination, .subresources = payload.subresources, .valueType = Core::GpuClearTextureTaskValueType::Float, .floatValue = payload.floatValue, };
    if(context.commandIrCapture && !context.commandIrCapture->captureClearTexture(context.task, context.packet, context.queue, payload.destination, clearDesc))
        return false;
    commandList.clearTextureFloat(*destination, clearDesc.subresources, clearDesc.floatValue);
    return true;
}

const Core::GpuPhysicalQueueInfo* QueueForTask(const Core::GpuTaskRecordContext& context, const Core::GpuTaskId& task)noexcept{
    if(!task.valid())
        return nullptr;
    return context.compiledPlan.queueInfoForTask(task);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 BeginGraphClearTiming(
    Core::GraphicsRuntime* const graphics,
    Optional<Core::GpuTimingMeasure>* const timing,
    Core::GpuTimingSubmissionTicket* const timingTicket,
    const Core::GpuTimingScopeDefinition& scope,
    Core::CommandList& commandList
){
    if(!graphics || !timing || !timingTicket || *timing)
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*timingTicket);
    timing->emplace(graphics->gpuTiming(), scope, graphics->getDevice(), commandList);
    return Core::FinishSplitGpuTimingMarker(timing);
}

b8 EndGraphClearTiming(Optional<Core::GpuTimingMeasure>* const timing, Core::GpuTimingSubmissionTicket* const timingTicket, Core::CommandList& commandList){
    if(!timing || !*timing || !timingTicket)
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*timingTicket);
    timing->value().finishTiming(commandList);
    timing->reset();
    return true;
}

b8 BeginGraphClearTimingRecord(void* const rawState, Core::CommandList& commandList, [[maybe_unused]] const Core::GpuTaskRecordContext& context){
    GraphClearTimingRecordState* const state = static_cast<GraphClearTimingRecordState*>(rawState);
    if(!state)
        return false;
    return BeginGraphClearTiming(state->graphics, state->timing, ResolveGraphClearTimingTicket(*state), state->scope, commandList);
}

b8 EndGraphClearTimingRecord(void* const rawState, Core::CommandList& commandList, [[maybe_unused]] const Core::GpuTaskRecordContext& context){
    GraphClearTimingRecordState* const state = static_cast<GraphClearTimingRecordState*>(rawState);
    if(!state)
        return false;
    return EndGraphClearTiming(state->timing, ResolveGraphClearTimingTicket(*state), commandList);
}

void DiscardGraphClearTimingRecord(void* const rawState){
    GraphClearTimingRecordState* const state = static_cast<GraphClearTimingRecordState*>(rawState);
    if(!state)
        return;
    Core::DiscardGpuTimingMeasure(state->timing);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererGpuTimingScope{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Optional<Name>, NameTextError::Enum> DeferredGraphQueueInternalIdle(const Core::GpuPhysicalQueueId& queue, Core::Alloc::ScratchArena& scratchArena){
    if(!queue.valid())
        return Optional<Name>{};

    const AString<Core::Alloc::ScratchArena> scopeName = StringFormat(scratchArena, "render.deferred_graph.queue_{}.internal_idle", queue.index);
    return ToName(scopeName);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Core::GpuGraphResourceDesc TextureResourceDesc(const Name& identity, const AStringView label)noexcept{
    Core::GpuGraphResourceDesc desc;
    desc.setIdentity(identity).setMarkerLabel(label).setType(Core::GpuGraphResourceType::Texture);
    return desc;
}

Core::GpuGraphResourceDesc BufferResourceDesc(const Name& identity, const AStringView label)noexcept{
    Core::GpuGraphResourceDesc desc;
    desc.setIdentity(identity).setMarkerLabel(label).setType(Core::GpuGraphResourceType::Buffer);
    return desc;
}

Expected<Core::GpuGraphResourceId> ImportExternalTexture(
    Core::GpuTaskGraph& graph,
    const Core::TextureHandle& texture,
    const Name& identity,
    const AStringView label,
    const Optional<Core::ResourceStates::Mask> initialState,
    const Optional<Core::ResourceStates::Mask> externalFinalState
){
    Core::GpuGraphResourceDesc desc = TextureResourceDesc(identity, label);
    if(initialState.has_value())
        desc.setInitialState(*initialState);
    if(externalFinalState.has_value())
        desc.setExternalFinalState(*externalFinalState);
    return graph.importTexture(texture, desc);
}

Expected<Core::GpuGraphResourceId> ImportExternalBuffer(
    Core::GpuTaskGraph& graph,
    const Core::BufferHandle& buffer,
    const Name& identity,
    const AStringView label,
    const Optional<Core::ResourceStates::Mask> initialState,
    const Optional<Core::ResourceStates::Mask> externalFinalState
){
    Core::GpuGraphResourceDesc desc = BufferResourceDesc(identity, label);
    if(initialState.has_value())
        desc.setInitialState(*initialState);
    if(externalFinalState.has_value())
        desc.setExternalFinalState(*externalFinalState);
    return graph.importBuffer(buffer, desc);
}

Expected<Core::GpuGraphResourceId> FindOrImportTexture(
    Core::GpuTaskGraph& graph,
    const Core::TextureHandle& texture,
    const AStringView resourceLabel,
    const Optional<Core::ResourceStates::Mask> initialState
){
    if(!texture)
        return MakeUnexpected(Failure{});
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        if(!declarations.valid())
            return MakeUnexpected(Failure{});
        const Core::GpuGraphResourceId existing = declarations.findImportedTexture(texture);
        if(existing.valid())
            return existing;
    }
    Core::GpuGraphResourceDesc desc = TextureResourceDesc(texture->getCreationDescription().name, resourceLabel);
    if(initialState.has_value())
        desc.setInitialState(*initialState);
    return graph.importTexture(texture, desc);
}

Expected<Core::GpuGraphResourceId> FindOrImportBuffer(
    Core::GpuTaskGraph& graph,
    const Core::BufferHandle& buffer,
    const AStringView resourceLabel
){
    if(!buffer)
        return MakeUnexpected(Failure{});
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        if(!declarations.valid())
            return MakeUnexpected(Failure{});
        const Core::GpuGraphResourceId existing = declarations.findImportedBuffer(buffer);
        if(existing.valid())
            return existing;
    }
    const Name bufferIdentity = buffer->getCreationDescription().debugName;
    if(!bufferIdentity)
        return MakeUnexpected(Failure{});
    return graph.importBuffer(buffer, BufferResourceDesc(bufferIdentity, resourceLabel));
}

Core::GpuGraphResourceDesc HazardDomainDesc(const Name& identity, const AStringView label)noexcept{
    Core::GpuGraphResourceDesc desc;
    desc.setIdentity(identity).setMarkerLabel(label).setType(Core::GpuGraphResourceType::HazardDomain);
    return desc;
}

Core::GpuTaskResourceUse ReadBufferUse(
    const Core::GpuGraphResourceId resource,
    const Core::BufferRange& range,
    const Core::ResourceStates::Mask state,
    const b8 hasIndependentStateSource
)noexcept{
    Core::GpuTaskResourceUse result = ReadUse(resource, state, hasIndependentStateSource);
    result.range.bufferRange = range;
    return result;
}

Core::GpuTaskResourceUse ReadTextureUse(
    const Core::GpuGraphResourceId resource,
    const Core::TextureSubresourceSet& subresources,
    const Core::ResourceStates::Mask state,
    const b8 hasIndependentStateSource
)noexcept{
    Core::GpuTaskResourceUse result = ReadUse(resource, state, hasIndependentStateSource);
    result.range.textureSubresources = subresources;
    return result;
}

Core::GpuTaskResourceUse WriteTextureUse(const Core::GpuGraphResourceId resource, const Core::TextureSubresourceSet& subresources, const Core::ResourceStates::Mask state)noexcept{
    Core::GpuTaskResourceUse result = WriteUse(resource, state);
    result.range.textureSubresources = subresources;
    return result;
}

Core::GpuGraphResourceDesc AccelStructResourceDesc(const Name& identity, const AStringView label)noexcept{
    Core::GpuGraphResourceDesc desc;
    desc.setIdentity(identity).setMarkerLabel(label).setType(Core::GpuGraphResourceType::AccelStruct);
    return desc;
}

Core::GpuTaskResourceUse ReadWriteTextureUse(const Core::GpuGraphResourceId resource, const Core::TextureSubresourceSet& subresources, const Core::ResourceStates::Mask state)noexcept{
    Core::GpuTaskResourceUse result = ReadWriteUse(resource, state);
    result.range.textureSubresources = subresources;
    return result;
}

Core::GpuTaskDesc TaskDesc(const Name& identity, const AStringView label, const Core::GpuTaskId& dependency)noexcept{
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Tiny;
    scheduling.forceSubmissionBoundary = false;
    scheduling.allowPacketMerge = true;
    scheduling.mergeWithPrevious = true;
    scheduling.allowMergeAcrossConsumerFrontier = true;
    Core::GpuTaskDesc desc;
    desc.setIdentity(identity).setMarkerLabel(label).setScheduling(scheduling);
    if(dependency.valid())
        desc.setDependencies({ &dependency, 1u });
    return desc;
}

void EnableSameFamilyComputeEffectRouting(Core::GpuTaskSchedulingHint& scheduling, const b8 preserveDirectDependency)noexcept{
    scheduling.allowSameClassQueueRouting = true;
    scheduling.preferNonPrimarySameClassQueue = true;
    scheduling.preserveSameClassQueueWithDirectDependency = preserveDirectDependency;
}

Core::GpuTaskSchedulingHint SharedComputeEmulationChainScheduling()noexcept{
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Medium;
    scheduling.forceSubmissionBoundary = false;
    scheduling.allowPacketMerge = true;
    scheduling.mergeWithPrevious = true;
    scheduling.allowMergeAcrossConsumerFrontier = true;
    return scheduling;
}

void MakeSharedComputeEmulationPhaseTaskDesc(
    const Name identity,
    const AStringView markerLabel,
    const Core::GpuTaskId& dependency,
    const Span<const Core::GpuTaskResourceUse> resourceUses,
    const Span<const Core::GpuTaskResourceSetUse> resourceSetUses,
    Core::GpuTaskDesc& desc
)noexcept{
    desc
        .setIdentity(identity)
        .setMarkerLabel(markerLabel)
        .setScheduling(SharedComputeEmulationChainScheduling())
        .setDependencies({ &dependency, 1u })
        .setResourceUses(resourceUses)
        .setResourceSetUses({ resourceSetUses.empty() ? nullptr : resourceSetUses.data(), resourceSetUses.size() })
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

