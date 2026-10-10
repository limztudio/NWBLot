// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_reflection.h"
#include "timing_names.h"
#include "task_graph_postprocess.h"
#include "sampling_sequence.h"

#include <impl/ecs_render/raytrace/raytracing_system.h>

#include <core/graphics/backend_selection/backend.h>

#include <impl/ecs_render/kernel/task_graph_resource_utils.h>

#include <core/common/log.h>
#include <core/graphics/runtime/runtime.h>
#include <core/task/gpu/capture/command_ir.h>

#include <global/text_numeric_format.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_reflection_tasks{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr TStringView s_DisabledReflectionRoute = NWB_TEXT("disabled");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Core::GpuGraphResourceId ImportTexture(Core::GpuTaskGraph& graph, const Core::TextureHandle& texture){
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        if(!declarations.valid())
            return {};
        const Core::GpuGraphResourceId existing = declarations.findImportedTexture(texture);
        if(existing.valid())
            return existing;
    }
    // Producers overwrite everything; Unknown preserves fresh Undefined.
    return graph.importTexture(
        texture,
        TextureResourceDesc(texture->getCreationDescription().name, "Reflection Output").setInitialState(Core::ResourceStates::Unknown)
    );
}

[[nodiscard]] Core::GpuGraphResourceId ImportBuffer(Core::GpuTaskGraph& graph, const Core::BufferHandle& buffer){
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        if(!declarations.valid())
            return {};
        const Core::GpuGraphResourceId existing = declarations.findImportedBuffer(buffer);
        if(existing.valid())
            return existing;
    }
    return graph.importBuffer(buffer, BufferResourceDesc(buffer->getCreationDescription().debugName, "Reflection Buffer"));
}

[[nodiscard]] Core::GpuTaskDesc TaskDesc(const Name identity, const AStringView label, const Core::GpuTaskId& dependency){
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

struct UploadParametersTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Transfer };

    struct Payload{
        Core::BufferHandle buffer;
        ReflectionFrameParameters parameters;
        ReflectionHistoryPlan history;
        ReflectionFeedbackPlan feedback;
        bool feedbackReserved = false;
        const bool* hardwarePreparationReady = nullptr;
    };

    [[nodiscard]] static bool Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
        ReflectionFrameParameters parameters = payload.parameters;
        if(!payload.hardwarePreparationReady || !*payload.hardwarePreparationReady)
            parameters.hardwareEnabled = 0u;
        const ReflectionHistoryOutcome history = ResolveReflectionHistoryOutcome(payload.history, parameters.hardwareEnabled != 0u);
        const ReflectionFeedbackOutcome feedback = ResolveReflectionFeedbackOutcome(payload.feedback, parameters.hardwareEnabled != 0u);
        parameters.feedbackFlags = 0u;
        parameters.feedbackProbeIndex = feedback.probeIndex;
        if(payload.feedbackReserved && feedback.eligible){
            parameters.feedbackFlags = NWB_REFLECTION_FEEDBACK_WRITE_ENABLED;
            if(feedback.reused)
                parameters.feedbackFlags |= NWB_REFLECTION_FEEDBACK_PREVIOUS_VALID;
        }
        parameters.sampleIndex = history.sampleIndex;
        const ReflectionSampleBase sampleBase = ComputeReflectionSampleBase(parameters.sampleIndex);
        parameters.sampleBaseX = sampleBase.x;
        parameters.sampleBaseY = sampleBase.y;
        // Stage the frame value after scene prep; a missing scene enqueues nothing.
        commandList.writeBuffer(*payload.buffer, &parameters, sizeof(parameters));
        return true;
    }
};

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

    [[nodiscard]] static bool Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
        if(!payload.pipeline)
            return false;
        commandList.endRenderPass();
        Core::ComputeState state;
        state.setPipeline(payload.pipeline.get());
        commandList.setComputeState(state);
        payload.graphics.getDevice().getDescriptorHeap().bindCompute(commandList, *payload.pipeline.get());
        commandList.setPushConstants(&payload.parameters, sizeof(payload.parameters));
        Core::GpuTimingMeasure timing(payload.graphics.gpuTiming(), ReflectionGpuTimingScope::s_DepthPyramid, payload.graphics.getDevice(), commandList);

        commandList.dispatch(
            (payload.parameters.destinationWidth + NWB_REFLECTION_DEPTH_GROUP_SIZE - 1u) / NWB_REFLECTION_DEPTH_GROUP_SIZE,
            (payload.parameters.destinationHeight + NWB_REFLECTION_DEPTH_GROUP_SIZE - 1u) / NWB_REFLECTION_DEPTH_GROUP_SIZE,
            1u
        );
        return true;
    }
};

namespace DispatchStage{
    enum Enum : u8{
        Classify,
        BuildArgs,
        Hardware,
    };
};

struct CsgDispatchParameters{
#define NWB_REFLECTION_PUSH_CPU_FIELD(name) u32 name = 0u;
    NWB_REFLECTION_CSG_PUSH_UINT_FIELDS(NWB_REFLECTION_PUSH_CPU_FIELD)
#undef NWB_REFLECTION_PUSH_CPU_FIELD
};
static_assert(sizeof(CsgDispatchParameters) == NWB_REFLECTION_CSG_PUSH_CONSTANT_BYTES);

struct DispatchTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };

    struct Payload{
        Core::GraphicsRuntime& graphics;
        RendererRayTracingSystem& raytracingSystem;
        ReflectionFrameSnapshot resources;
        DispatchStage::Enum stage;
        ReflectionFeedbackReservation feedbackReservation;
        const bool* hardwarePreparationReady = nullptr;
        bool* hardwareDispatchLogged = nullptr;
        bool* fallbackDispatchLogged = nullptr;
    };

    [[nodiscard]] static bool Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
        const ReflectionFrameSnapshot& resources = payload.resources;
        const bool hardware = payload.stage == DispatchStage::Hardware;
        if(hardware && (!payload.hardwarePreparationReady || !*payload.hardwarePreparationReady))
            return true;
        Core::ComputePipeline* pipeline = resources.buildArgsPipeline.get();
        if(hardware)
            pipeline = resources.hardwarePipeline.get();
        else if(payload.stage == DispatchStage::Classify)
            pipeline = resources.classifyPipeline.get();
        if(!pipeline || (hardware && !resources.scene.valid()))
            return false;
        commandList.endRenderPass();
        Core::ComputeState state;
        state.setPipeline(pipeline);
        if(hardware)
            state.setIndirectParams(resources.indirectArgs.get());
        commandList.setComputeState(state);
        payload.graphics.getDevice().getDescriptorHeap().bindCompute(
            commandList, *pipeline,
            hardware ? resources.scene.tlasHeapHandle : Core::GpuDescriptorHandle::Invalid()
        );
        const bool sliced = resources.scene.csgTraceContextBuffer && (hardware || payload.stage == DispatchStage::BuildArgs);
        CsgDispatchParameters csgParameters;
        csgParameters.frameParametersSlot = resources.frameParametersSlot;
        if(sliced)
            commandList.setPushConstants(&csgParameters, sizeof(csgParameters));
        else
            commandList.setPushConstants(&resources.frameParametersSlot, sizeof(resources.frameParametersSlot));

        const Core::GpuTimingScopeDefinition* scope = &ReflectionGpuTimingScope::s_BuildArgs;
        if(hardware)
            scope = &ReflectionGpuTimingScope::s_Hardware;
        else if(payload.stage == DispatchStage::Classify)
            scope = &ReflectionGpuTimingScope::s_Classify;
        Core::GpuTimingMeasure timing(payload.graphics.gpuTiming(), *scope, payload.graphics.getDevice(), commandList);

        if(hardware)
            commandList.dispatchIndirect(0u);
        else if(payload.stage == DispatchStage::BuildArgs)
            commandList.dispatch(1u, 1u, 1u);
        else{
            commandList.dispatch(
                (resources.parameters.width + NWB_REFLECTION_CLASSIFY_GROUP_SIZE - 1u) / NWB_REFLECTION_CLASSIFY_GROUP_SIZE,
                (resources.parameters.height + NWB_REFLECTION_CLASSIFY_GROUP_SIZE - 1u) / NWB_REFLECTION_CLASSIFY_GROUP_SIZE,
                1u
            );
        }
        return true;
    }

    static void Accepted(Payload& payload, const Core::QueueSubmissionToken& token){
        const bool hardware = payload.resources.parameters.hardwareEnabled != 0u && payload.resources.parameters.maxHardwareRays > 0u
            && payload.hardwarePreparationReady && *payload.hardwarePreparationReady;
        const bool sceneReady = payload.resources.parameters.hardwareEnabled != 0u
            && payload.hardwarePreparationReady && *payload.hardwarePreparationReady;
        payload.feedbackReservation.accept(token, sceneReady);
        if(payload.stage == DispatchStage::Hardware && hardware && payload.resources.scene.csgTraceContextBuffer)
            payload.raytracingSystem.confirmCsgTraceContextReadSubmission(token);
        if(payload.stage == DispatchStage::Hardware && hardware){
            if(payload.hardwareDispatchLogged && !*payload.hardwareDispatchLogged){
                NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Reflection resolve: hardware"));
                *payload.hardwareDispatchLogged = true;
            }
        }
        else if(payload.stage == DispatchStage::Classify && !hardware){
            if(payload.fallbackDispatchLogged && !*payload.fallbackDispatchLogged){
                const u32 mode = payload.resources.parameters.traceMode;
                const bool screen = mode == NWB_REFLECTION_MODE_SCREEN || mode == NWB_REFLECTION_MODE_HYBRID;
                TStringView route = screen ? NWB_TEXT("screen-space") : NWB_TEXT("environment");
                if(mode == NWB_REFLECTION_MODE_DISABLED)
                    route = s_DisabledReflectionRoute;
                NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Reflection resolve: {}"), route);
                *payload.fallbackDispatchLogged = true;
            }
        }
    }

    static void Discarded(Payload& payload){
        payload.feedbackReservation.discard();
    }
};

struct CsgHardwareSliceTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = {
        .requiredCapabilities = Core::GpuQueueCapability::Compute, .requiresPrimaryGraphicsQueue = true,
    };

    struct Payload{
        Core::GraphicsRuntime& graphics;
        RendererRayTracingSystem& raytracingSystem;
        ReflectionCsgDispatchHandle dispatch;
        u32 slice = 0u;
        u32 sliceCount = 0u;
        const bool* hardwarePreparationReady = nullptr;
        bool* hardwareDispatchLogged = nullptr;
    };

    [[nodiscard]] static bool Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
        if(!payload.hardwarePreparationReady || !*payload.hardwarePreparationReady)
            return true;
        if(!payload.dispatch || payload.slice >= payload.sliceCount)
            return false;
        const ReflectionFrameSnapshot& resources = payload.dispatch->resources;
        Core::ComputePipeline* const pipeline = resources.hardwarePipeline.get();
        if(!pipeline || !resources.scene.valid())
            return false;
        commandList.endRenderPass();
        Core::ComputeState state;
        state.setPipeline(pipeline).setIndirectParams(resources.indirectArgs.get());
        commandList.setComputeState(state);
        payload.graphics.getDevice().getDescriptorHeap().bindCompute(commandList, *pipeline, resources.scene.tlasHeapHandle);
        CsgDispatchParameters parameters;
        parameters.frameParametersSlot = resources.frameParametersSlot;
        parameters.rayOffset = payload.slice * NWB_REFLECTION_TRACE_SLICE_RAYS;
        commandList.setPushConstants(&parameters, sizeof(parameters));
        if(
            payload.slice == 0u
            && !payload.dispatch->timing.begin(ReflectionGpuTimingScope::s_Hardware, payload.graphics.getDevice(), commandList)
        )
            return false;

        commandList.dispatchIndirect(payload.slice * NWB_REFLECTION_INDIRECT_ARGUMENT_BYTES);
        if(payload.slice + 1u == payload.sliceCount)
            return payload.dispatch->timing.recordEnd(commandList);
        return true;
    }

    static void Accepted(Payload& payload, const Core::QueueSubmissionToken& token){
        if(!payload.hardwarePreparationReady || !*payload.hardwarePreparationReady)
            return;
        // An accepted prefix already reads the frozen context even when a later packet fails.
        payload.raytracingSystem.confirmCsgTraceContextReadSubmission(token);
        if(payload.slice == 0u && !payload.dispatch->timing.confirmBeginSubmission(token)){
            NWB_LOGGER_WARNING(NWB_TEXT("Reflection: failed to confirm accepted hardware timing begin"));
            payload.dispatch->timing.discard();
        }
        if(payload.slice + 1u == payload.sliceCount && !payload.dispatch->timing.confirmEndSubmission(token, true)){
            NWB_LOGGER_WARNING(NWB_TEXT("Reflection: failed to confirm accepted hardware timing end"));
            payload.dispatch->timing.discard();
        }
        if(payload.hardwareDispatchLogged && !*payload.hardwareDispatchLogged){
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Reflection resolve: hardware"));
            *payload.hardwareDispatchLogged = true;
        }
    }

    static void Discarded(Payload& payload){
        if(payload.dispatch)
            payload.dispatch->timing.discard();
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
        bool feedbackReserved = false;
        const bool* hardwarePreparationReady = nullptr;
        bool hardwareEnabled = false;
    };

    [[nodiscard]] static bool Record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        if(!payload.source || !payload.destination || !payload.reservation.valid())
            return false;
        if(
            context.commandIrCapture
            && !context.commandIrCapture->captureCopyBuffer(
                context.task, context.packet, context.queue,
                payload.sourceResource, 0u, payload.destinationResource, 0u, NWB_REFLECTION_COUNTER_SIZE
            )
        )
            return false;
        commandList.endRenderPass();
        commandList.copyBuffer(*payload.destination, 0u, *payload.source, 0u, NWB_REFLECTION_COUNTER_SIZE);
        return !commandList.commandRecordingFailed();
    }

    static void Accepted(Payload& payload, const Core::QueueSubmissionToken& token){
        const bool hardwareReady = payload.hardwareEnabled && payload.hardwarePreparationReady && *payload.hardwarePreparationReady;
        const ReflectionHistoryOutcome history = ResolveReflectionHistoryOutcome(payload.history, hardwareReady);
        ReflectionFeedbackOutcome feedback = ResolveReflectionFeedbackOutcome(payload.feedback, hardwareReady);
        if(!payload.feedbackReserved){
            feedback.eligible = false;
            feedback.reused = false;
        }
        payload.reservation.accept(token, hardwareReady, &history, &feedback);
    }

    static void Discarded(Payload& payload){
        payload.reservation.discard();
    }
};

struct FinalizeTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = {};

    struct Payload{};
    [[nodiscard]] static bool Record(const Payload&, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
        commandList.endRenderPass();
        return true;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Name ReflectionCsgHardwareSliceIdentity(const u32 slice){
    char suffix[11];
    const AStringView index = FormatI64(static_cast<i64>(slice), suffix);
    if(index.empty())
        return {};
    return DeriveName(s_ReflectionCsgHardwareSliceFamily, index);
}

ReflectionGraphResult DeclareReflectionTasks(
    Core::GpuTaskGraph& graph,
    Core::GraphicsRuntime& graphics,
    RendererReflectionSystem& reflectionSystem,
    Core::Alloc::ScratchArena& scratchArena,
    const ReflectionFrameSnapshot& resources,
    const ReflectionGraphInputs& inputs,
    Core::GpuTaskId dependency
){
    using namespace __hidden_reflection_tasks;
    if(
        !resources.valid() || !inputs.surfaceReads || inputs.surfaceReadCount == 0u
        || (inputs.hardwareReadCount > 0u && !inputs.hardwareReads)
        || (inputs.hardwareSetReadCount > 0u && !inputs.hardwareSetReads)
    )
        return {};
    const bool screen = resources.parameters.traceMode == NWB_REFLECTION_MODE_SCREEN
        || resources.parameters.traceMode == NWB_REFLECTION_MODE_HYBRID;
    if(screen && (!inputs.opaqueDepth.valid() || !inputs.opaqueColor.valid()))
        return {};
    ReflectionFeedbackReservation feedbackReservation(resources.feedback.control, resources.feedback.plan);
    const bool feedbackReserved = feedbackReservation.valid();
    const bool feedbackWrites = feedbackReserved && resources.feedback.plan.eligible && resources.parameters.hardwareEnabled != 0u;
    const bool buildArguments = resources.hasHardwareWork();
    const DispatchStage::Enum feedbackPublicationStage = buildArguments ? DispatchStage::BuildArgs : DispatchStage::Classify;
    // A stale feedback lease disables this optimization; rendering remains available.
    Core::GpuGraphResourceId feedbackCurrent;
    Core::GpuGraphResourceId feedbackPrevious;
    if(feedbackWrites){
        feedbackCurrent = ImportBuffer(graph, resources.feedback.current.buffer);
        if(resources.feedback.plan.reused)
            feedbackPrevious = ImportBuffer(graph, resources.feedback.previous.buffer);
        if(!feedbackCurrent.valid() || (resources.feedback.plan.reused && !feedbackPrevious.valid()))
            return {};
    }
    ReflectionGraphResult result;
    result.opaqueRadiance = ImportTexture(graph, resources.opaqueRadiance);
    result.glassRadiance = ImportTexture(graph, resources.glassRadiance);
    result.frameParameters = ImportBuffer(graph, resources.frameParameters);
    result.counters = ImportBuffer(graph, resources.counters);
    const Core::GpuGraphResourceId queue = ImportBuffer(graph, resources.queue);
    const Core::GpuGraphResourceId args = ImportBuffer(graph, resources.indirectArgs);
    if(
        !result.opaqueRadiance.valid() || !result.glassRadiance.valid() || !result.frameParameters.valid()
        || !result.counters.valid() || !queue.valid() || !args.valid()
    )
        return {};

    Core::GpuTaskDesc desc = TaskDesc(Name("render.reflection.parameters_upload"), "Reflection Parameters Upload", dependency);
    const Core::GpuTaskResourceUse parameterWrite = WriteUse(result.frameParameters, Core::ResourceStates::CopyDest);
    desc.setResourceUses(&parameterWrite, 1u);
    dependency = graph.addTask<UploadParametersTask>(
        desc,
        UploadParametersTask::Payload{
            resources.frameParameters, resources.parameters, resources.postprocess.history,
            resources.feedback.plan, feedbackReserved, inputs.hardwarePreparationReady,
        }
    );
    if(!dependency.valid())
        return {};

    desc = TaskDesc(Name("render.reflection.clear_counters"), "Reflection Clear Counters", dependency);
    dependency = graph.addClearBufferTask(desc, Core::GpuClearBufferTaskDesc{.destination = result.counters, .clearValue = 0u});
    if(!dependency.valid())
        return {};

    Core::GpuGraphResourceId depthPyramid;
    if(screen){
        const ReflectionDepthPyramidSnapshot& pyramid = resources.depthPyramid;
        depthPyramid = ImportTexture(graph, pyramid.texture);
        if(!depthPyramid.valid())
            return {};
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
                ReadTextureUse(
                    mipIndex == 0u ? inputs.opaqueDepth : depthPyramid,
                    Core::TextureSubresourceSet(mipIndex == 0u ? 0u : mipIndex - 1u, 1u, 0u, 1u)
                ),
                WriteTextureUse(
                    depthPyramid, Core::TextureSubresourceSet(mipIndex, 1u, 0u, 1u), Core::ResourceStates::UnorderedAccess
                ),
            };
            Core::GpuTaskDesc depthDesc = TaskDesc(destination.taskIdentity, "Reflection Depth Reduce", dependency);
            depthDesc.setResourceUses(depthUses, LengthOf(depthUses));
            depthDesc.setTimingMetadata(Core::GpuTaskTimingMetadata{.policy = Core::GpuTaskTimingPolicy::PacketOnly});
            dependency = graph.addTask<DepthReduceTask>(
                depthDesc, DepthReduceTask::Payload{graphics, pyramid.pipeline, parameters}
            );
            if(!dependency.valid())
                return {};
        }
    }

    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> uses{scratchArena};
    constexpr usize s_ReflectionUseCapacity = 12u;
    uses.reserve(inputs.surfaceReadCount + inputs.hardwareReadCount + s_ReflectionUseCapacity);
    const auto appendSurfaceReads = [&](){
        uses.assign(inputs.surfaceReads, inputs.surfaceReads + inputs.surfaceReadCount);
        uses.push_back(ReadUse(result.frameParameters, Core::ResourceStates::ConstantBuffer));
    };
    const auto appendDispatch = [&](const Name identity, const AStringView label, const DispatchStage::Enum stage){
        Core::GpuTaskDesc dispatchDesc = TaskDesc(identity, label, dependency);
        dispatchDesc.setResourceUses(uses.data(), uses.size());
        dispatchDesc.setTimingMetadata(Core::GpuTaskTimingMetadata{.policy = Core::GpuTaskTimingPolicy::PacketOnly});
        if(stage == DispatchStage::Hardware)
            dispatchDesc.setResourceSetUses(inputs.hardwareSetReads, inputs.hardwareSetReadCount);
        ReflectionFeedbackReservation dispatchFeedback(nullptr, {});
        if(stage == feedbackPublicationStage)
            dispatchFeedback = Move(feedbackReservation);
        dependency = graph.addTask<DispatchTask>(
            dispatchDesc,
            DispatchTask::Payload{
                graphics, inputs.raytracingSystem, resources, stage, Move(dispatchFeedback), inputs.hardwarePreparationReady,
                inputs.hardwareDispatchLogged, inputs.fallbackDispatchLogged,
            }
        );
        return dependency.valid();
    };
    appendSurfaceReads();
    if(screen){
        uses.push_back(ReadTextureUse(depthPyramid, Core::TextureSubresourceSet(0u, resources.depthPyramid.mipCount, 0u, 1u)));
        uses.push_back(ReadUse(inputs.opaqueColor));
    }
    if(feedbackWrites){
        Core::GpuTaskResourceUse entryWrite = WriteUse(feedbackCurrent, Core::ResourceStates::UnorderedAccess);
        entryWrite.range.bufferRange = Core::BufferRange(
            NWB_REFLECTION_FEEDBACK_HEADER_BYTES, resources.feedback.extent.byteCount - NWB_REFLECTION_FEEDBACK_HEADER_BYTES
        );
        uses.push_back(entryWrite);
        if(feedbackPrevious.valid())
            uses.push_back(ReadUse(feedbackPrevious));
    }
    uses.push_back(WriteUse(result.opaqueRadiance, Core::ResourceStates::UnorderedAccess));
    uses.push_back(WriteUse(result.glassRadiance, Core::ResourceStates::UnorderedAccess));
    uses.push_back(WriteUse(queue, Core::ResourceStates::UnorderedAccess));
    uses.push_back(ReadWriteUse(result.counters, Core::ResourceStates::UnorderedAccess));
    if(!appendDispatch(Name("render.reflection.classify"), "Reflection Classify", DispatchStage::Classify))
        return {};

    if(buildArguments){
        uses.clear();
        uses.push_back(ReadUse(result.frameParameters, Core::ResourceStates::ConstantBuffer));
        uses.push_back(ReadUse(result.counters));
        uses.push_back(WriteUse(args, Core::ResourceStates::UnorderedAccess));
        if(feedbackWrites){
            Core::GpuTaskResourceUse headerWrite = WriteUse(feedbackCurrent, Core::ResourceStates::UnorderedAccess);
            headerWrite.range.bufferRange = Core::BufferRange(0u, NWB_REFLECTION_FEEDBACK_HEADER_BYTES);
            uses.push_back(headerWrite);
        }
        if(!appendDispatch(Name("render.reflection.build_args"), "Reflection Build Arguments", DispatchStage::BuildArgs))
            return {};
        if(!resources.hardwarePipeline || !resources.scene.valid() || inputs.hardwareReadCount == 0u)
            return {};
        appendSurfaceReads();
        for(usize index = 0u; index < inputs.hardwareReadCount; ++index)
            uses.push_back(inputs.hardwareReads[index]);
        uses.push_back(ReadUse(queue));
        uses.push_back(ReadUse(args, Core::ResourceStates::IndirectArgument));
        uses.push_back(ReadWriteUse(result.counters, Core::ResourceStates::UnorderedAccess));
        uses.push_back(ReadWriteUse(result.opaqueRadiance, Core::ResourceStates::UnorderedAccess));
        uses.push_back(ReadWriteUse(result.glassRadiance, Core::ResourceStates::UnorderedAccess));
        if(resources.scene.csgTraceContextBuffer){
            const ReflectionCsgDispatchHandle dispatch = reflectionSystem.createCsgDispatchState(resources);
            if(!dispatch)
                return {};
            const u32 rayCapacity = Min(resources.parameters.maxHardwareRays, resources.parameters.queueCapacity);
            const u32 sliceCount = (rayCapacity + NWB_REFLECTION_TRACE_SLICE_RAYS - 1u) / NWB_REFLECTION_TRACE_SLICE_RAYS;
            // Separate native packets let the driver retire bounded work; shared UAV state also orders CPU recording.
            for(u32 slice = 0u; slice < sliceCount; ++slice){
                Core::GpuTaskDesc sliceDesc = TaskDesc(ReflectionCsgHardwareSliceIdentity(slice), "Reflection Hardware Resolve", dependency);
                Core::GpuTaskSchedulingHint scheduling;
                scheduling.forceSubmissionBoundary = true;
                scheduling.allowPacketMerge = false;
                sliceDesc.setScheduling(scheduling).setResourceUses(uses.data(), uses.size());
                sliceDesc.setResourceSetUses(inputs.hardwareSetReads, inputs.hardwareSetReadCount);
                sliceDesc.setTimingMetadata(Core::GpuTaskTimingMetadata{.policy = Core::GpuTaskTimingPolicy::PacketOnly});
                dependency = graph.addTask<CsgHardwareSliceTask>(sliceDesc,
                    CsgHardwareSliceTask::Payload{
                        graphics, inputs.raytracingSystem, dispatch, slice, sliceCount,
                        inputs.hardwarePreparationReady, inputs.hardwareDispatchLogged,
                    }
                );
                if(!dependency.valid())
                    return {};
            }
        }
        else if(!appendDispatch(Name("render.reflection.hardware"), "Reflection Hardware Resolve", DispatchStage::Hardware))
            return {};
    }

    dependency = DeclareReflectionPostprocessTasks(graph, graphics, scratchArena, resources, inputs, result, dependency);
    if(!dependency.valid())
        return {};

    if(resources.parameters.diagnosticsEnabled != 0u && resources.statistics.control){
        ReflectionStatistics metadata = resources.statistics.metadata;
        metadata.feedbackSequence = feedbackReserved ? resources.feedback.plan.sequence : 0u;
        ReflectionStatisticsReservation reservation(resources.statistics.control, metadata);
        if(reservation.valid()){
            const Core::BufferHandle readback = resources.statistics.buffers[reservation.slotIndex()];
            if(!readback)
                return {};
            const Core::GpuGraphResourceId readbackResource = ImportBuffer(graph, readback);
            if(!readbackResource.valid())
                return {};
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
            Core::GpuTaskDesc copyDesc = TaskDesc(Name("render.reflection.statistics"), "Reflection Statistics Readback", dependency);
            copyDesc.setResourceUses(copyUses, LengthOf(copyUses));
            dependency = graph.addTask<StatisticsReadbackTask>(
                copyDesc,
                StatisticsReadbackTask::Payload{
                    resources.counters, readback, result.counters, readbackResource, Move(reservation), resources.postprocess.history,
                    resources.feedback.plan, feedbackReserved, inputs.hardwarePreparationReady, resources.parameters.hardwareEnabled != 0u,
                }
            );
            if(!dependency.valid())
                return {};
        }
    }

    const Core::GpuTaskResourceUse finalUses[] = {ReadUse(result.opaqueRadiance), ReadUse(result.glassRadiance)};
    desc = TaskDesc(Name("render.reflection.finalize"), "Reflection Finalize Outputs", dependency);
    desc.setResourceUses(finalUses, LengthOf(finalUses));
    result.completion = graph.addTask<FinalizeTask>(desc, FinalizeTask::Payload{});
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

