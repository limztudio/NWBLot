// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_postprocess.h"
#include "timing_names.h"

#include <impl/assets/graphics/reflection/temporal_constants.h>
#include <impl/assets/graphics/reflection/spatial_constants.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>

#include <core/graphics/backend_selection/backend.h>
#include <core/graphics/runtime/runtime.h>
#include <core/task/gpu/capture/command_ir.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_postprocess{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Extra temporal uses besides the surface reads: the radiance write and the previous-history read.
static constexpr usize s_TemporalExtraResourceUseCount = 2u;

struct TemporalParameters{
#define NWB_REFLECTION_CPU_POST_FIELD(name) u32 name = 0u;
    NWB_REFLECTION_TEMPORAL_UINT_FIELDS(NWB_REFLECTION_CPU_POST_FIELD)
#undef NWB_REFLECTION_CPU_POST_FIELD
};
static_assert(sizeof(TemporalParameters) == NWB_REFLECTION_TEMPORAL_PUSH_CONSTANT_BYTES);

struct SpatialParameters{
#define NWB_REFLECTION_CPU_POST_FIELD(name) u32 name = 0u;
    NWB_REFLECTION_SPATIAL_UINT_FIELDS(NWB_REFLECTION_CPU_POST_FIELD)
#undef NWB_REFLECTION_CPU_POST_FIELD
};
static_assert(sizeof(SpatialParameters) == NWB_REFLECTION_SPATIAL_PUSH_CONSTANT_BYTES);

struct DepthReduceParameters{
#define NWB_REFLECTION_DEPTH_CPU_FIELD(name) u32 name = 0u;
    NWB_REFLECTION_DEPTH_UINT_FIELDS(NWB_REFLECTION_DEPTH_CPU_FIELD)
#undef NWB_REFLECTION_DEPTH_CPU_FIELD
};
static_assert(sizeof(DepthReduceParameters) == NWB_REFLECTION_DEPTH_PUSH_CONSTANT_BYTES);

struct DepthReduceTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };

    struct Payload{
        Core::GraphicsRuntime& graphics;
        Core::ComputePipelineHandle pipeline;
        DepthReduceParameters parameters;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
        if(!payload.pipeline)
            return false;
        commandList.endRenderPass();
        Core::ComputeState state;
        state.setPipeline(payload.pipeline.get());
        commandList.setComputeState(state);
        payload.graphics.getDevice().getDescriptorHeap().bindCompute(commandList, *payload.pipeline.get());
        commandList.setPushConstants(&payload.parameters, sizeof(payload.parameters));
        Core::GpuTimingMeasure timing(payload.graphics.gpuTiming(), ReflectionGpuTimingScope::s_DepthPyramid, payload.graphics.getDevice(), commandList);

        commandList.dispatch((payload.parameters.destinationWidth + NWB_REFLECTION_DEPTH_GROUP_SIZE - 1u) / NWB_REFLECTION_DEPTH_GROUP_SIZE, (payload.parameters.destinationHeight + NWB_REFLECTION_DEPTH_GROUP_SIZE - 1u) / NWB_REFLECTION_DEPTH_GROUP_SIZE, 1u);
        return true;
    }
};

struct TemporalTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };

    struct Payload{
        Core::GraphicsRuntime& graphics;
        ReflectionPostprocessSnapshot snapshot;
        ReflectionHistoryReservation reservation;
        u32 width = 0u;
        u32 height = 0u;
        const b8* hardwarePreparationReady = nullptr;
        b8 hardwareEnabled = false;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
        if(!payload.snapshot.history.eligible)
            return true;
        const b8 ready = payload.hardwareEnabled && payload.hardwarePreparationReady && *payload.hardwarePreparationReady;
        const ReflectionHistoryOutcome outcome = ResolveReflectionHistoryOutcome(payload.snapshot.history, ready);
        // Classification and tracing already produced this bank; the accepted task still publishes its history state.
        if(!outcome.reused || payload.snapshot.history.settings.temporalMaxSamples <= 1u)
            return true;
        commandList.endRenderPass();
        const TemporalParameters parameters{
            payload.width, payload.height, payload.snapshot.current.storageSlot, payload.snapshot.previous.sampledSlot,
            payload.snapshot.opaqueSpecularSlot, outcome.reused ? payload.snapshot.history.previousSampleCount : 0u,
            payload.snapshot.history.settings.temporalMaxSamples, outcome.reused ? 1u : 0u,
            outcome.sampleIndex, payload.snapshot.history.settings.samplingSeed,
        };
        Core::ComputeState state;
        state.setPipeline(payload.snapshot.temporalPipeline.get());
        commandList.setComputeState(state);
        payload.graphics.getDevice().getDescriptorHeap().bindCompute(commandList, *payload.snapshot.temporalPipeline.get());
        commandList.setPushConstants(&parameters, sizeof(parameters));
        Core::GpuTimingMeasure timing(payload.graphics.gpuTiming(), ReflectionGpuTimingScope::s_Temporal, payload.graphics.getDevice(), commandList);

        commandList.dispatch((payload.width + NWB_REFLECTION_TEMPORAL_GROUP_SIZE - 1u) / NWB_REFLECTION_TEMPORAL_GROUP_SIZE, (payload.height + NWB_REFLECTION_TEMPORAL_GROUP_SIZE - 1u) / NWB_REFLECTION_TEMPORAL_GROUP_SIZE, 1u);
        return !commandList.commandRecordingFailed();
    }

    static void Accepted(Payload& payload, const Core::QueueSubmissionToken& token)noexcept{
        payload.reservation.accept(token, payload.hardwareEnabled && payload.hardwarePreparationReady && *payload.hardwarePreparationReady);
    }

    static void Discarded(Payload& payload)noexcept{
        payload.reservation.discard();
    }
};

struct SpatialTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };

    struct Payload{
        Core::GraphicsRuntime& graphics;
        Core::ComputePipelineHandle pipeline;
        SpatialParameters parameters;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
        commandList.endRenderPass();
        Core::ComputeState state;
        state.setPipeline(payload.pipeline.get());
        commandList.setComputeState(state);
        payload.graphics.getDevice().getDescriptorHeap().bindCompute(commandList, *payload.pipeline.get());
        commandList.setPushConstants(&payload.parameters, sizeof(payload.parameters));
        Core::GpuTimingMeasure timing(payload.graphics.gpuTiming(), ReflectionGpuTimingScope::s_Spatial, payload.graphics.getDevice(), commandList);

        commandList.dispatch((payload.parameters.width + NWB_REFLECTION_SPATIAL_GROUP_SIZE - 1u) / NWB_REFLECTION_SPATIAL_GROUP_SIZE, (payload.parameters.height + NWB_REFLECTION_SPATIAL_GROUP_SIZE - 1u) / NWB_REFLECTION_SPATIAL_GROUP_SIZE, 1u);
        return !commandList.commandRecordingFailed();
    }
};

struct StatisticsReadbackTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Transfer };

    struct Payload{
        Core::BufferHandle source;
        Core::BufferHandle destination;
        Core::GpuGraphResourceId sourceResource;
        Core::GpuGraphResourceId destinationResource;
        ReflectionStatisticsReservation reservation;
        ReflectionHistoryPlan history;
        ReflectionFeedbackPlan feedback;
        const b8* hardwarePreparationReady = nullptr;
        b8 feedbackReserved = false;
        b8 hardwareEnabled = false;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
        if(!payload.source || !payload.destination || !payload.reservation.valid())
            return false;
        if(context.commandIrCapture && !context.commandIrCapture->captureCopyBuffer(context.task, context.packet, context.queue, payload.sourceResource, 0u, payload.destinationResource, 0u, NWB_REFLECTION_COUNTER_SIZE))
            return false;
        commandList.endRenderPass();
        commandList.copyBuffer(*payload.destination, 0u, *payload.source, 0u, NWB_REFLECTION_COUNTER_SIZE);
        return !commandList.commandRecordingFailed();
    }

    static void Accepted(Payload& payload, const Core::QueueSubmissionToken& token)noexcept{
        const b8 hardwareReady = payload.hardwareEnabled && payload.hardwarePreparationReady && *payload.hardwarePreparationReady;
        const ReflectionHistoryOutcome history = ResolveReflectionHistoryOutcome(payload.history, hardwareReady);
        ReflectionFeedbackOutcome feedback = ResolveReflectionFeedbackOutcome(payload.feedback, hardwareReady);
        if(!payload.feedbackReserved){
            feedback.eligible = false;
            feedback.reused = false;
        }
        payload.reservation.accept(token, hardwareReady, &history, &feedback);
    }

    static void Discarded(Payload& payload)noexcept{
        payload.reservation.discard();
    }
};

struct FinalizeTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = {};

    struct Payload{};


public:
    [[nodiscard]] static b8 Record(const Payload&, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
        commandList.endRenderPass();
        return true;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Core::GpuGraphResourceId> ImportReflectionTexture(
    Core::GpuTaskGraph& graph,
    const Core::TextureHandle& texture,
    const AStringView resourceLabel,
    const b8 firstWrite
){
    {
        const Core::GpuTaskGraph::DeclarationReadView view(graph);
        if(!view.valid())
            return MakeUnexpected(Failure{});
        const Core::GpuGraphResourceId existing = view.findImportedTexture(texture);
        if(existing.valid())
            return existing;
    }
    // Producers overwrite everything; Unknown preserves fresh Undefined.
    return graph.importTexture(
        texture,
        TextureResourceDesc(texture->getCreationDescription().name, resourceLabel).setInitialState(firstWrite ? Core::ResourceStates::Unknown : Core::ResourceStates::Common)
    );
}

Expected<Core::GpuGraphResourceId> ImportReflectionBuffer(Core::GpuTaskGraph& graph, const Core::BufferHandle& buffer){
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        if(!declarations.valid())
            return MakeUnexpected(Failure{});
        const Core::GpuGraphResourceId existing = declarations.findImportedBuffer(buffer);
        if(existing.valid())
            return existing;
    }
    return graph.importBuffer(buffer, BufferResourceDesc(buffer->getCreationDescription().debugName, "Reflection Buffer"));
}

Core::GpuTaskDesc ReflectionTaskDesc(const Name identity, const AStringView label, const Core::GpuTaskId& dependency)noexcept{
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Tiny;
    scheduling.allowPacketMerge = true;
    scheduling.mergeWithPrevious = true;
    scheduling.allowMergeAcrossConsumerFrontier = true;
    Core::GpuTaskDesc desc;
    desc.setIdentity(identity).setMarkerLabel(label).setScheduling(scheduling);
    if(dependency.valid())
        desc.setDependencies(&dependency, 1u);
    return desc;
}

Expected<Core::GpuTaskId> DeclareReflectionDepthPyramidTasks(
    Core::GpuTaskGraph& graph,
    Core::GraphicsRuntime& graphics,
    const ReflectionDepthPyramidSnapshot& pyramid,
    const Core::GpuGraphResourceId opaqueDepth,
    Core::GpuTaskId dependency,
    Core::GpuGraphResourceId& outDepthPyramid
){
    using namespace __hidden_task_graph_postprocess;
    const auto depthPyramidResult = ImportReflectionTexture(graph, pyramid.texture);
    if(!depthPyramidResult){
        outDepthPyramid = {};
        return MakeUnexpected(Failure{});
    }
    outDepthPyramid = *depthPyramidResult;
    for(u32 mipIndex = 0u; mipIndex < pyramid.mipCount; ++mipIndex){
        const ReflectionDepthPyramidMip& destination = pyramid.mips[mipIndex];
        const ReflectionDepthPyramidMip& source = pyramid.mips[mipIndex == 0u ? 0u : mipIndex - 1u];
        DepthReduceParameters parameters;
        parameters.sourceSlot = mipIndex == 0u ? pyramid.sourceDepthSlot : source.sampledSlot;
        parameters.destinationSlot = destination.storageSlot;
        parameters.sourceWidth = source.width;
        parameters.sourceHeight = source.height;
        parameters.destinationWidth = destination.width;
        parameters.destinationHeight = destination.height;
        const Core::GpuTaskResourceUse depthUses[] = {
            ReadTextureUse(mipIndex == 0u ? opaqueDepth : outDepthPyramid, Core::TextureSubresourceSet(mipIndex == 0u ? 0u : mipIndex - 1u, 1u, 0u, 1u)),
            WriteTextureUse(outDepthPyramid, Core::TextureSubresourceSet(mipIndex, 1u, 0u, 1u), Core::ResourceStates::UnorderedAccess),
        };
        Core::GpuTaskDesc depthDesc = ReflectionTaskDesc(destination.taskIdentity, "Reflection Depth Reduce", dependency);
        depthDesc.setResourceUses(depthUses, LengthOf(depthUses));
        depthDesc.setTiming(Core::GpuTaskTimingMetadata{.policy = Core::GpuTaskTimingPolicy::PacketOnly});
        const auto depthTask = graph.addTask<DepthReduceTask>(depthDesc, DepthReduceTask::Payload{graphics, pyramid.pipeline, parameters});
        if(!depthTask)
            return MakeUnexpected(Failure{});
        dependency = *depthTask;
    }
    return dependency;
}

Expected<Core::GpuTaskId> DeclareReflectionPostprocessTasks(
    Core::GpuTaskGraph& graph,
    Core::GraphicsRuntime& graphics,
    Core::Alloc::ScratchArena& scratchArena,
    const ReflectionFrameSnapshot& resources,
    const ReflectionGraphInputs& inputs,
    Core::GpuTaskId dependency,
    ReflectionGraphResult& result
){
    using namespace __hidden_task_graph_postprocess;
    const ReflectionPostprocessSnapshot& snapshot = resources.postprocess;
    ReflectionHistoryReservation reservation(snapshot.control, snapshot.history);
    if(!reservation.valid() || (snapshot.history.eligible && !snapshot.temporalPipeline))
        return MakeUnexpected(Failure{});
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> uses{scratchArena};
    uses.reserve(inputs.surfaceReads.size() + s_TemporalExtraResourceUseCount);
    if(snapshot.history.eligible){
        uses.assign(inputs.surfaceReads.begin(), inputs.surfaceReads.end());
        uses.push_back(ReadWriteUse(result.opaqueRadiance, Core::ResourceStates::UnorderedAccess));
        if(snapshot.history.reused){
            const auto previousResult = ImportReflectionTexture(graph, snapshot.previous.texture, "Reflection History Image", false);
            if(!previousResult)
                return MakeUnexpected(Failure{});
            const Core::GpuGraphResourceId previous = *previousResult;
            uses.push_back(ReadUse(previous));
        }
    }
    Core::GpuTaskDesc desc = ReflectionTaskDesc(Name("render.reflection.temporal"), "Reflection Temporal Accumulation", dependency);
    desc.setResourceUses(uses.data(), uses.size());
    if(snapshot.history.eligible)
        desc.setTiming(Core::GpuTaskTimingMetadata{.policy = Core::GpuTaskTimingPolicy::PacketOnly});
    const auto temporalTask = graph.addTask<TemporalTask>(
        desc,
        TemporalTask::Payload{ graphics, snapshot, Move(reservation), resources.parameters.width, resources.parameters.height, inputs.hardwarePreparationReady, resources.parameters.hardwareEnabled != 0u, }
    );
    if(!temporalTask)
        return MakeUnexpected(temporalTask.error());
    dependency = *temporalTask;
    if(!snapshot.spatialEnabled)
        return dependency;
    if(!snapshot.spatialPipeline || !snapshot.spatial.texture)
        return MakeUnexpected(Failure{});
    const auto spatialResult = ImportReflectionTexture(graph, snapshot.spatial.texture, "Reflection History Image", true);
    if(!spatialResult)
        return MakeUnexpected(Failure{});
    const Core::GpuGraphResourceId spatial = *spatialResult;
    uses.assign(inputs.surfaceReads.begin(), inputs.surfaceReads.end());
    uses.push_back(ReadUse(result.opaqueRadiance));
    uses.push_back(WriteUse(spatial, Core::ResourceStates::UnorderedAccess));
    desc = ReflectionTaskDesc(Name("render.reflection.spatial"), "Reflection Spatial Filter", dependency);
    desc.setResourceUses(uses.data(), uses.size());
    desc.setTiming(Core::GpuTaskTimingMetadata{.policy = Core::GpuTaskTimingPolicy::PacketOnly});
    const auto spatialTask = graph.addTask<SpatialTask>(
        desc,
        SpatialTask::Payload{
            graphics, snapshot.spatialPipeline,
            SpatialParameters{
                resources.parameters.width, resources.parameters.height, snapshot.current.sampledSlot, snapshot.spatial.storageSlot,
                snapshot.deferredResourcesSlot, snapshot.opaqueSpecularSlot, snapshot.viewSlot, snapshot.history.settings.spatialRadius,
            },
        }
    );
    if(!spatialTask)
        return MakeUnexpected(spatialTask.error());
    dependency = *spatialTask;
    result.opaqueRadiance = spatial;
    return dependency;
}

Expected<Core::GpuTaskId> DeclareReflectionStatisticsAndFinalizeTasks(
    Core::GpuTaskGraph& graph,
    const ReflectionFrameSnapshot& resources,
    const ReflectionGraphInputs& inputs,
    const b8 feedbackReserved,
    Core::GpuTaskId dependency,
    ReflectionGraphResult& result
){
    using namespace __hidden_task_graph_postprocess;
    if(resources.parameters.diagnosticsEnabled != 0u && resources.statistics.control){
        ReflectionStatistics metadata = resources.statistics.metadata;
        metadata.feedbackSequence = feedbackReserved ? resources.feedback.plan.sequence : 0u;
        ReflectionStatisticsReservation reservation(resources.statistics.control, metadata);
        if(reservation.valid()){
            const Core::BufferHandle& readback = resources.statistics.buffers[reservation.slotIndex()];
            if(!readback)
                return MakeUnexpected(Failure{});
            const auto readbackResourceResult = ImportReflectionBuffer(graph, readback);
            if(!readbackResourceResult)
                return MakeUnexpected(Failure{});
            const Core::GpuGraphResourceId readbackResource = *readbackResourceResult;
            const Core::GpuTaskResourceUse copyUses[] = {
                Core::GpuTaskResourceUse{
                    .resource = result.counters,
                    .range = Core::GpuTaskResourceRange{.bufferRange = Core::BufferRange(0u, NWB_REFLECTION_COUNTER_SIZE)},
                    .requiredState = Core::ResourceStates::CopySource,
                    .access = Core::GpuTaskResourceAccess::Read,
                },
                Core::GpuTaskResourceUse{
                    .resource = readbackResource,
                    .range = Core::GpuTaskResourceRange{.bufferRange = Core::BufferRange(0u, NWB_REFLECTION_COUNTER_SIZE)},
                    .requiredState = Core::ResourceStates::CopyDest,
                    .access = Core::GpuTaskResourceAccess::Write,
                },
            };
            Core::GpuTaskDesc copyDesc = ReflectionTaskDesc(Name("render.reflection.statistics"), "Reflection Statistics Readback", dependency);
            copyDesc.setResourceUses(copyUses, LengthOf(copyUses));
            const auto copyTask = graph.addTask<StatisticsReadbackTask>(
                copyDesc,
                StatisticsReadbackTask::Payload{
                    resources.counters, readback, result.counters, readbackResource, Move(reservation), resources.postprocess.history,
                    resources.feedback.plan, inputs.hardwarePreparationReady, feedbackReserved, resources.parameters.hardwareEnabled != 0u,
                }
            );
            if(!copyTask)
                return MakeUnexpected(Failure{});
            dependency = *copyTask;
        }
    }

    const Core::GpuTaskResourceUse finalUses[] = {ReadUse(result.opaqueRadiance), ReadUse(result.glassRadiance)};
    Core::GpuTaskDesc desc = ReflectionTaskDesc(Name("render.reflection.finalize"), "Reflection Finalize Outputs", dependency);
    desc.setResourceUses(finalUses, LengthOf(finalUses));
    return graph.addTask<FinalizeTask>(desc, FinalizeTask::Payload{});
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

