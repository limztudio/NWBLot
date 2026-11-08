// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_postprocess.h"

#include <impl/ecs_render/kernel/timing_names.h>
#include <impl/assets/graphics/reflection/depth_constants.h>
#include <impl/assets/graphics/reflection/frame_constants.h>
#include <impl/assets/graphics/reflection/temporal_constants.h>
#include <impl/assets/graphics/reflection/spatial_constants.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>

#include <core/graphics/backend_selection/backend.h>
#include <core/graphics/runtime/runtime.h>
#include <core/task/gpu/capture/command_ir.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_postprocess{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static_assert(sizeof(ReflectionTemporalPushConstants) == NWB_REFLECTION_TEMPORAL_PUSH_CONSTANT_BYTES);
static_assert(sizeof(ReflectionSpatialPushConstants) == NWB_REFLECTION_SPATIAL_PUSH_CONSTANT_BYTES);
static_assert(sizeof(ReflectionDepthReducePushConstants) == NWB_REFLECTION_DEPTH_PUSH_CONSTANT_BYTES);

struct DepthReduceTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };


public:
    struct Payload{
        Core::GraphicsRuntime& graphics;
        Core::ComputePipelineHandle pipeline;
        ReflectionDepthReducePushConstants parameters;
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

        commandList.dispatch(DivideUpWrapping(payload.parameters.destinationWidth, NWB_REFLECTION_DEPTH_GROUP_SIZE), DivideUpWrapping(payload.parameters.destinationHeight, NWB_REFLECTION_DEPTH_GROUP_SIZE), 1u);
        return true;
    }
};

struct TemporalTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };


public:
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
        const ReflectionTemporalPushConstants parameters = MakeReflectionTemporalPushConstants(
            payload.width,
            payload.height,
            payload.snapshot.current.storageSlot,
            payload.snapshot.previous.sampledSlot,
            payload.snapshot.opaqueSpecularSlot,
            outcome.reused,
            payload.snapshot.history.previousSampleCount,
            payload.snapshot.history.settings.temporalMaxSamples,
            outcome.sampleIndex,
            payload.snapshot.history.settings.samplingSeed
        );
        Core::ComputeState state;
        state.setPipeline(payload.snapshot.temporalPipeline.get());
        commandList.setComputeState(state);
        payload.graphics.getDevice().getDescriptorHeap().bindCompute(commandList, *payload.snapshot.temporalPipeline.get());
        commandList.setPushConstants(&parameters, sizeof(parameters));
        Core::GpuTimingMeasure timing(payload.graphics.gpuTiming(), ReflectionGpuTimingScope::s_Temporal, payload.graphics.getDevice(), commandList);

        commandList.dispatch(DivideUpWrapping(payload.width, NWB_REFLECTION_TEMPORAL_GROUP_SIZE), DivideUpWrapping(payload.height, NWB_REFLECTION_TEMPORAL_GROUP_SIZE), 1u);
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


public:
    struct Payload{
        Core::GraphicsRuntime& graphics;
        Core::ComputePipelineHandle pipeline;
        ReflectionSpatialPushConstants parameters;
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

        commandList.dispatch(DivideUpWrapping(payload.parameters.width, NWB_REFLECTION_SPATIAL_GROUP_SIZE), DivideUpWrapping(payload.parameters.height, NWB_REFLECTION_SPATIAL_GROUP_SIZE), 1u);
        return !commandList.commandRecordingFailed();
    }
};

struct StatisticsReadbackTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Transfer };


public:
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
        payload.reservation.acceptReservation(token, hardwareReady, &history, &feedback);
    }

    static void Discarded(Payload& payload)noexcept{
        payload.reservation.discard();
    }
};

struct FinalizeTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = {};


public:
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
    // Producers overwrite everything; Unknown preserves fresh Undefined.
    return FindOrImportTexture(graph, texture, resourceLabel, firstWrite ? Core::ResourceStates::Unknown : Core::ResourceStates::Common);
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
        desc.setDependencies({ &dependency, 1u });
    return desc;
}

Expected<Core::GpuTaskId> DeclareReflectionDepthPyramidTasks(
    const ReflectionDepthPyramidSnapshot& pyramid,
    const Core::GpuGraphResourceId opaqueDepth,
    Core::GpuTaskId dependency,
    Core::GpuTaskGraph& graph,
    Core::GraphicsRuntime& graphics,
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
        const ReflectionDepthReducePushConstants parameters = MakeReflectionDepthReducePushConstants(
            mipIndex == 0u ? pyramid.sourceDepthSlot : source.sampledSlot,
            destination.storageSlot,
            source.width,
            source.height,
            destination.width,
            destination.height
        );
        const Array<Core::GpuTaskResourceUse, 2u> depthUses = {
            ReadTextureUse(mipIndex == 0u ? opaqueDepth : outDepthPyramid, Core::TextureSubresourceSet(mipIndex == 0u ? 0u : mipIndex - 1u, 1u, 0u, 1u)),
            WriteTextureUse(outDepthPyramid, Core::TextureSubresourceSet(mipIndex, 1u, 0u, 1u), Core::ResourceStates::UnorderedAccess),
        };
        Core::GpuTaskDesc depthDesc = ReflectionTaskDesc(destination.taskIdentity, s_ReflectionDepthReduceTaskLabel, dependency);
        depthDesc.setResourceUses(depthUses);
        depthDesc.setTiming(Core::GpuTaskTimingMetadata{.policy = Core::GpuTaskTimingPolicy::PacketOnly});
        const auto depthTask = graph.addTask<DepthReduceTask>(depthDesc, DepthReduceTask::Payload{graphics, pyramid.pipeline, parameters});
        if(!depthTask)
            return MakeUnexpected(Failure{});
        dependency = *depthTask;
    }
    return dependency;
}

Expected<Core::GpuTaskId> DeclareReflectionPostprocessTasks(
    const ReflectionFrameSnapshot& resources,
    const ReflectionGraphInputs& inputs,
    Core::GpuTaskId dependency,
    Core::GpuTaskGraph& graph,
    Core::GraphicsRuntime& graphics,
    Core::Alloc::ScratchArena& scratchArena,
    ReflectionGraphResult& result
){
    using namespace __hidden_task_graph_postprocess;
    const ReflectionPostprocessSnapshot& snapshot = resources.postprocess;
    ReflectionHistoryReservation reservation(snapshot.control, snapshot.history);
    if(!reservation.valid() || (snapshot.history.eligible && !snapshot.temporalPipeline))
        return MakeUnexpected(Failure{});
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> uses{scratchArena};
    uses.reserve(inputs.surfaceReads.size() + s_ReflectionTemporalExtraResourceUseCount);
    if(snapshot.history.eligible){
        uses.assign(inputs.surfaceReads.begin(), inputs.surfaceReads.end());
        uses.push_back(ReadWriteUse(result.opaqueRadiance, Core::ResourceStates::UnorderedAccess));
        if(snapshot.history.reused){
            const auto previousResult = ImportReflectionTexture(graph, snapshot.previous.texture, s_ReflectionHistoryImageLabel, false);
            if(!previousResult)
                return MakeUnexpected(Failure{});
            const Core::GpuGraphResourceId previous = *previousResult;
            uses.push_back(ReadUse(previous));
        }
    }
    Core::GpuTaskDesc desc = ReflectionTaskDesc(s_ReflectionTemporalTaskName, s_ReflectionTemporalTaskLabel, dependency);
    desc.setResourceUses(uses);
    if(snapshot.history.eligible)
        desc.setTiming(Core::GpuTaskTimingMetadata{.policy = Core::GpuTaskTimingPolicy::PacketOnly});
    const auto temporalTask = graph.addTask<TemporalTask>(
        desc,
        TemporalTask::Payload{ graphics, snapshot, Move(reservation), resources.parameters.width, resources.parameters.height, inputs.hardwarePreparationReady, resources.parameters.hardwareEnabled != 0u, });
    if(!temporalTask)
        return MakeUnexpected(temporalTask.error());
    dependency = *temporalTask;
    if(!snapshot.spatialEnabled)
        return dependency;
    if(!snapshot.spatialPipeline || !snapshot.spatial.texture)
        return MakeUnexpected(Failure{});
    const auto spatialResult = ImportReflectionTexture(graph, snapshot.spatial.texture, s_ReflectionHistoryImageLabel, true);
    if(!spatialResult)
        return MakeUnexpected(Failure{});
    const Core::GpuGraphResourceId spatial = *spatialResult;
    uses.assign(inputs.surfaceReads.begin(), inputs.surfaceReads.end());
    uses.push_back(ReadUse(result.opaqueRadiance));
    uses.push_back(WriteUse(spatial, Core::ResourceStates::UnorderedAccess));
    desc = ReflectionTaskDesc(s_ReflectionSpatialTaskName, s_ReflectionSpatialTaskLabel, dependency);
    desc.setResourceUses(uses);
    desc.setTiming(Core::GpuTaskTimingMetadata{.policy = Core::GpuTaskTimingPolicy::PacketOnly});
    const ReflectionSpatialPushConstants spatialParameters = MakeReflectionSpatialPushConstants(
        resources.parameters.width,
        resources.parameters.height,
        snapshot.current.sampledSlot,
        snapshot.spatial.storageSlot,
        snapshot.deferredResourcesSlot,
        snapshot.opaqueSpecularSlot,
        snapshot.viewSlot,
        snapshot.history.settings.spatialRadius
    );
    const auto spatialTask = graph.addTask<SpatialTask>(
        desc,
        SpatialTask::Payload{ graphics, snapshot.spatialPipeline, spatialParameters });
    if(!spatialTask)
        return MakeUnexpected(spatialTask.error());
    dependency = *spatialTask;
    result.opaqueRadiance = spatial;
    return dependency;
}

Expected<Core::GpuTaskId> DeclareReflectionStatisticsAndFinalizeTasks(
    const ReflectionFrameSnapshot& resources,
    const ReflectionGraphInputs& inputs,
    const b8 feedbackReserved,
    Core::GpuTaskId dependency,
    Core::GpuTaskGraph& graph,
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
            const auto readbackResourceResult = FindOrImportBuffer(graph, readback, s_ReflectionBufferLabel);
            if(!readbackResourceResult)
                return MakeUnexpected(Failure{});
            const Core::GpuGraphResourceId readbackResource = *readbackResourceResult;
            const Array<Core::GpuTaskResourceUse, 2u> copyUses = {
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
            Core::GpuTaskDesc copyDesc = ReflectionTaskDesc(s_ReflectionStatisticsTaskName, s_ReflectionStatisticsTaskLabel, dependency);
            copyDesc.setResourceUses(copyUses);
            const auto copyTask = graph.addTask<StatisticsReadbackTask>(
                copyDesc,
                StatisticsReadbackTask::Payload{
                    resources.counters, readback, result.counters, readbackResource, Move(reservation), resources.postprocess.history,
                    resources.feedback.plan, inputs.hardwarePreparationReady, feedbackReserved, resources.parameters.hardwareEnabled != 0u,
                });
            if(!copyTask)
                return MakeUnexpected(Failure{});
            dependency = *copyTask;
        }
    }

    const Array<Core::GpuTaskResourceUse, 2u> finalUses = {ReadUse(result.opaqueRadiance), ReadUse(result.glassRadiance)};
    Core::GpuTaskDesc desc = ReflectionTaskDesc(s_ReflectionFinalizeTaskName, s_ReflectionFinalizeTaskLabel, dependency);
    desc.setResourceUses(finalUses);
    return graph.addTask<FinalizeTask>(desc, FinalizeTask::Payload{});
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

