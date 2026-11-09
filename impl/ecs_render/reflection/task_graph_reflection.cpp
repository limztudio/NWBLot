// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_reflection.h"
#include "task_graph_postprocess.h"
#include <impl/ecs_render/kernel/timing_names.h>
#include <impl/assets/graphics/reflection/frame_constants.h>
#include <impl/ecs_render/raytrace/raytracing_system.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <core/graphics/backend_selection/backend.h>
#include <core/common/log.h>
#include <core/graphics/runtime/runtime.h>
#include <core/task/gpu/capture/command_ir.h>
#include <global/algorithm.h>
#include <global/numeric_text.h>
#include "frame_graph_reflection_resolve.h"
#include <impl/assets/graphics/reflection/depth_constants.h>
#include <impl/assets/graphics/reflection/temporal_constants.h>
#include <impl/assets/graphics/reflection/spatial_constants.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_reflection{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct UploadParametersTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Transfer };


public:
    struct Payload{
        Core::BufferHandle buffer;
        ReflectionFrameParameters parameters;
        ReflectionHistoryPlan history;
        ReflectionFeedbackPlan feedback;
        b8 feedbackReserved = false;
        const b8* hardwarePreparationReady = nullptr;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
        const b8 hardwareReady = payload.hardwarePreparationReady && *payload.hardwarePreparationReady;
        const ReflectionFrameParameters parameters = ResolveReflectionUploadFrameParameters(payload.parameters, payload.history, payload.feedback, payload.feedbackReserved, hardwareReady);
        // Stage the frame value after scene prep; a missing scene enqueues nothing.
        commandList.writeBuffer(*payload.buffer, &parameters, sizeof(parameters));
        return true;
    }
};

namespace DispatchStage = ReflectionDispatchStage;
static_assert(sizeof(ReflectionCsgDispatchPushConstants) == NWB_REFLECTION_CSG_PUSH_CONSTANT_BYTES);

struct DispatchTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };


public:
    struct Payload{
        Core::GraphicsRuntime& graphics;
        RendererRayTracingSystem& raytracingSystem;
        ReflectionFrameSnapshot resources;
        DispatchStage::Enum stage;
        ReflectionFeedbackReservation feedbackReservation;
        const b8* hardwarePreparationReady = nullptr;
        b8* hardwareDispatchLogged = nullptr;
        b8* fallbackDispatchLogged = nullptr;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
        const ReflectionFrameSnapshot& resources = payload.resources;
        const b8 hardware = payload.stage == DispatchStage::Hardware;
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
        payload.graphics.getDevice().getDescriptorHeap().bindCompute(commandList, *pipeline, hardware ? resources.scene.tlasHeapHandle : Core::GpuDescriptorHandle::Invalid());
        const b8 sliced = resources.scene.csgTraceContextBuffer && (hardware || payload.stage == DispatchStage::BuildArgs);
        const ReflectionCsgDispatchPushConstants csgParameters = MakeReflectionCsgDispatchPushConstants(resources.frameParametersSlot, 0u);
        if(sliced)
            commandList.setPushConstants(&csgParameters, sizeof(csgParameters));
        else
            commandList.setPushConstants(&resources.frameParametersSlot, sizeof(resources.frameParametersSlot));

        NotNull<const Core::GpuTimingScopeDefinition*> scope(&ReflectionGpuTimingScope::s_BuildArgs);
        if(hardware)
            scope = &ReflectionGpuTimingScope::s_Hardware;
        else if(payload.stage == DispatchStage::Classify)
            scope = &ReflectionGpuTimingScope::s_Classify;
        Core::GpuTimingMeasure timing(payload.graphics.gpuTiming(), *scope, payload.graphics.getDevice(), commandList);

        if(hardware)
            commandList.dispatchIndirect(0u);
        else if(payload.stage == DispatchStage::BuildArgs)
            commandList.dispatch(1u, 1u, 1u);
        else
            commandList.dispatch(DivideUpWrapping(resources.parameters.width, NWB_REFLECTION_CLASSIFY_GROUP_SIZE), DivideUpWrapping(resources.parameters.height, NWB_REFLECTION_CLASSIFY_GROUP_SIZE), 1u);
        return true;
    }

    static void Accepted(Payload& payload, const Core::QueueSubmissionToken& token){
        const b8 hardware = payload.resources.parameters.hardwareEnabled != 0u && payload.resources.parameters.maxHardwareRays > 0u && payload.hardwarePreparationReady && *payload.hardwarePreparationReady;
        const b8 sceneReady = payload.resources.parameters.hardwareEnabled != 0u && payload.hardwarePreparationReady && *payload.hardwarePreparationReady;
        payload.feedbackReservation.accept(token, sceneReady);
        if(payload.stage == DispatchStage::Hardware && hardware && payload.resources.scene.csgTraceContextBuffer)
            payload.raytracingSystem.confirmRayTraceContextReadSubmission(token);
        if(payload.stage == DispatchStage::Hardware && hardware){
            if(payload.hardwareDispatchLogged)
                LogHardwareReflectionResolveIfNeeded(*payload.hardwareDispatchLogged);
        }
        else if(payload.stage == DispatchStage::Classify && !hardware){
            if(payload.fallbackDispatchLogged)
                LogFallbackReflectionResolveIfNeeded(payload.resources.parameters.traceMode, *payload.fallbackDispatchLogged);
        }
    }

    static void Discarded(Payload& payload)noexcept{
        payload.feedbackReservation.discard();
    }
};

struct CsgHardwareSliceTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { .requiredCapabilities = Core::GpuQueueCapability::Compute, .requiresPrimaryGraphicsQueue = true, };


public:
    struct Payload{
        Core::GraphicsRuntime& graphics;
        RendererRayTracingSystem& raytracingSystem;
        ReflectionCsgDispatchHandle dispatch;
        u32 slice = 0u;
        u32 sliceCount = 0u;
        const b8* hardwarePreparationReady = nullptr;
        b8* hardwareDispatchLogged = nullptr;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
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
        const ReflectionCsgDispatchPushConstants parameters = MakeReflectionCsgDispatchPushConstants(resources.frameParametersSlot, payload.slice * s_ReflectionTraceSliceRays);
        commandList.setPushConstants(&parameters, sizeof(parameters));
        if(payload.slice == 0u && !payload.dispatch->timing->begin(ReflectionGpuTimingScope::s_Hardware, payload.graphics.getDevice(), commandList))
            return false;

        commandList.dispatchIndirect(payload.slice * s_ReflectionIndirectArgumentBytes);
        if(payload.slice + 1u == payload.sliceCount)
            return payload.dispatch->timing->recordEnd(commandList);
        return true;
    }

    static void Accepted(Payload& payload, const Core::QueueSubmissionToken& token){
        if(!payload.hardwarePreparationReady || !*payload.hardwarePreparationReady)
            return;
        // An accepted prefix already reads the frozen context even when a later packet fails.
        payload.raytracingSystem.confirmRayTraceContextReadSubmission(token);
        if(payload.slice == 0u && !payload.dispatch->timing->confirmBeginSubmission(token)){
            NWB_LOGGER_WARNING(NWB_TEXT("Reflection: failed to confirm accepted hardware timing begin"));
            payload.dispatch->timing->discard();
        }
        if(payload.slice + 1u == payload.sliceCount && !payload.dispatch->timing->confirmEndSubmission(token, true)){
            NWB_LOGGER_WARNING(NWB_TEXT("Reflection: failed to confirm accepted hardware timing end"));
            payload.dispatch->timing->discard();
        }
        if(payload.hardwareDispatchLogged)
            LogHardwareReflectionResolveIfNeeded(*payload.hardwareDispatchLogged);
    }

    static void Discarded(Payload& payload)noexcept{
        if(payload.dispatch)
            payload.dispatch->timing->discard();
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Name, ErrorCode> ReflectionCsgHardwareSliceIdentity(const u32 slice){
    Array<char, s_ReflectionSliceIndexTextBufferSize> suffix{};
    const auto index = FormatI64(static_cast<i64>(slice), suffix);
    if(!index)
        return MakeUnexpected(index.error());
    const auto identity = DeriveName(s_ReflectionCsgHardwareSliceFamily, *index);
    if(!identity)
        return MakeUnexpected(MakeErrorCode(ErrorCondition::invalid_argument));
    return *identity;
}

Expected<ReflectionGraphResult> DeclareReflectionTasks(
    const ReflectionFrameSnapshot& resources,
    const ReflectionGraphInputs& inputs,
    Core::GpuTaskId dependency,
    Core::GpuTaskGraph& graph,
    Core::GraphicsRuntime& graphics,
    RendererReflectionSystem& reflectionSystem,
    Core::Alloc::ScratchArena& scratchArena
){
    using namespace __hidden_task_graph_reflection;
    if(!resources.valid() || inputs.surfaceReads.empty())
        return MakeUnexpected(Failure{});
    const b8 screen = resources.parameters.traceMode == NWB_REFLECTION_MODE_SCREEN || resources.parameters.traceMode == NWB_REFLECTION_MODE_HYBRID;
    if(screen && (!inputs.opaqueDepth.valid() || !inputs.opaqueColor.valid()))
        return MakeUnexpected(Failure{});
    ReflectionFeedbackReservation feedbackReservation(resources.feedback.control, resources.feedback.plan);
    const b8 feedbackReserved = feedbackReservation.valid();
    const b8 feedbackWrites = feedbackReserved && resources.feedback.plan.eligible && resources.parameters.hardwareEnabled != 0u;
    const b8 buildArguments = resources.hasHardwareWork();
    const DispatchStage::Enum feedbackPublicationStage = buildArguments ? DispatchStage::BuildArgs : DispatchStage::Classify;
    // A stale feedback lease disables this optimization; rendering remains available.
    Core::GpuGraphResourceId feedbackCurrent;
    Core::GpuGraphResourceId feedbackPrevious;
    if(feedbackWrites){
        const auto feedbackCurrentResult = FindOrImportBuffer(graph, resources.feedback.current.buffer, s_ReflectionBufferLabel);
        Expected<Core::GpuGraphResourceId> feedbackPreviousResult = MakeUnexpected(Failure{});
        if(resources.feedback.plan.reused)
            feedbackPreviousResult = FindOrImportBuffer(graph, resources.feedback.previous.buffer, s_ReflectionBufferLabel);
        if(!feedbackCurrentResult || (resources.feedback.plan.reused && !feedbackPreviousResult))
            return MakeUnexpected(Failure{});
        feedbackCurrent = *feedbackCurrentResult;
        if(resources.feedback.plan.reused)
            feedbackPrevious = *feedbackPreviousResult;
    }
    ReflectionGraphResult result;
    const auto opaqueRadianceResult = ImportReflectionTexture(graph, resources.opaqueRadiance);
    const auto glassRadianceResult = ImportReflectionTexture(graph, resources.glassRadiance);
    const auto frameParametersResult = FindOrImportBuffer(graph, resources.frameParameters, s_ReflectionBufferLabel);
    const auto countersResult = FindOrImportBuffer(graph, resources.counters, s_ReflectionBufferLabel);
    const auto queueResult = FindOrImportBuffer(graph, resources.queue, s_ReflectionBufferLabel);
    const auto argsResult = FindOrImportBuffer(graph, resources.indirectArgs, s_ReflectionBufferLabel);
    if(!opaqueRadianceResult || !glassRadianceResult || !frameParametersResult || !countersResult || !queueResult || !argsResult)
        return MakeUnexpected(Failure{});
    result.opaqueRadiance = *opaqueRadianceResult;
    result.glassRadiance = *glassRadianceResult;
    result.frameParameters = *frameParametersResult;
    result.counters = *countersResult;
    const Core::GpuGraphResourceId queue = *queueResult;
    const Core::GpuGraphResourceId args = *argsResult;

    Core::GpuTaskDesc desc = ReflectionTaskDesc(s_ReflectionParametersUploadTaskName, s_ReflectionParametersUploadTaskLabel, dependency);
    const Core::GpuTaskResourceUse parameterWrite = WriteUse(result.frameParameters, Core::ResourceStates::CopyDest);
    desc.setResourceUses({ &parameterWrite, 1u });
    const auto dependencyResult = graph.addTask<UploadParametersTask>(
        desc,
        UploadParametersTask::Payload{ resources.frameParameters, resources.parameters, resources.postprocess.history, resources.feedback.plan, feedbackReserved, inputs.hardwarePreparationReady, });
    if(!dependencyResult)
        return MakeUnexpected(Failure{});
    dependency = *dependencyResult;

    desc = ReflectionTaskDesc(s_ReflectionClearCountersTaskName, s_ReflectionClearCountersTaskLabel, dependency);
    const auto dependencyResult2 = graph.addClearBufferTask(desc, Core::GpuClearBufferTaskDesc{.destination = result.counters, .clearValue = 0u});
    if(!dependencyResult2)
        return MakeUnexpected(Failure{});
    dependency = *dependencyResult2;

    Core::GpuGraphResourceId depthPyramid;
    if(screen){
        const auto depthPyramidTaskResult = DeclareReflectionDepthPyramidTasks(resources.depthPyramid, inputs.opaqueDepth, dependency, graph, graphics, depthPyramid);
        if(!depthPyramidTaskResult)
            return MakeUnexpected(Failure{});
        dependency = *depthPyramidTaskResult;
    }

    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> uses{scratchArena};
    uses.reserve(inputs.surfaceReads.size() + inputs.hardwareReads.size() + s_ReflectionUseCapacity);
    const auto appendSurfaceReads = [&]()noexcept{
        uses.assign(inputs.surfaceReads.begin(), inputs.surfaceReads.end());
        uses.push_back(ReadUse(result.frameParameters, Core::ResourceStates::ConstantBuffer));
    };
    const auto appendDispatch = [&](const Name identity, const AStringView label, const DispatchStage::Enum stage){
        Core::GpuTaskDesc dispatchDesc = ReflectionTaskDesc(identity, label, dependency);
        dispatchDesc.setResourceUses(uses);
        dispatchDesc.setTiming(Core::GpuTaskTimingMetadata{.policy = Core::GpuTaskTimingPolicy::PacketOnly});
        if(stage == DispatchStage::Hardware)
            dispatchDesc.setResourceSetUses(inputs.hardwareSetReads);
        ReflectionFeedbackReservation dispatchFeedback(nullptr, {});
        if(stage == feedbackPublicationStage)
            dispatchFeedback = Move(feedbackReservation);
        const auto dispatchTask = graph.addTask<DispatchTask>(
            dispatchDesc,
            DispatchTask::Payload{ graphics, inputs.raytracingSystem, resources, stage, Move(dispatchFeedback), inputs.hardwarePreparationReady, inputs.hardwareDispatchLogged, inputs.fallbackDispatchLogged, });
        if(!dispatchTask){
            dependency = {};
            return false;
        }
        dependency = *dispatchTask;
        return true;
    };
    appendSurfaceReads();
    if(screen){
        uses.push_back(ReadTextureUse(depthPyramid, Core::TextureSubresourceSet(0u, resources.depthPyramid.mipCount, 0u, 1u)));
        uses.push_back(ReadUse(inputs.opaqueColor));
    }
    if(feedbackWrites){
        Core::GpuTaskResourceUse entryWrite = WriteUse(feedbackCurrent, Core::ResourceStates::UnorderedAccess);
        entryWrite.range.bufferRange = Core::BufferRange(s_ReflectionFeedbackHeaderBytes, resources.feedback.extent.byteCount - s_ReflectionFeedbackHeaderBytes);
        uses.push_back(entryWrite);
        if(feedbackPrevious.valid())
            uses.push_back(ReadUse(feedbackPrevious));
    }
    uses.push_back(WriteUse(result.opaqueRadiance, Core::ResourceStates::UnorderedAccess));
    uses.push_back(WriteUse(result.glassRadiance, Core::ResourceStates::UnorderedAccess));
    uses.push_back(WriteUse(queue, Core::ResourceStates::UnorderedAccess));
    uses.push_back(ReadWriteUse(result.counters, Core::ResourceStates::UnorderedAccess));
    if(!appendDispatch(s_ReflectionClassifyTaskName, s_ReflectionClassifyTaskLabel, DispatchStage::Classify))
        return MakeUnexpected(Failure{});

    if(buildArguments){
        uses.clear();
        uses.push_back(ReadUse(result.frameParameters, Core::ResourceStates::ConstantBuffer));
        uses.push_back(ReadUse(result.counters));
        uses.push_back(WriteUse(args, Core::ResourceStates::UnorderedAccess));
        if(feedbackWrites){
            Core::GpuTaskResourceUse headerWrite = WriteUse(feedbackCurrent, Core::ResourceStates::UnorderedAccess);
            headerWrite.range.bufferRange = Core::BufferRange(0u, s_ReflectionFeedbackHeaderBytes);
            uses.push_back(headerWrite);
        }
        if(!appendDispatch(s_ReflectionBuildArgsTaskName, s_ReflectionBuildArgsTaskLabel, DispatchStage::BuildArgs))
            return MakeUnexpected(Failure{});
        if(!resources.hardwarePipeline || !resources.scene.valid() || inputs.hardwareReads.empty())
            return MakeUnexpected(Failure{});
        appendSurfaceReads();
        for(const Core::GpuTaskResourceUse& use : inputs.hardwareReads)
            uses.push_back(use);
        uses.push_back(ReadUse(queue));
        uses.push_back(ReadUse(args, Core::ResourceStates::IndirectArgument));
        uses.push_back(ReadWriteUse(result.counters, Core::ResourceStates::UnorderedAccess));
        uses.push_back(ReadWriteUse(result.opaqueRadiance, Core::ResourceStates::UnorderedAccess));
        uses.push_back(ReadWriteUse(result.glassRadiance, Core::ResourceStates::UnorderedAccess));
        if(resources.scene.csgTraceContextBuffer){
            const auto dispatchResult = reflectionSystem.acquireCsgDispatchState(resources);
            if(!dispatchResult)
                return MakeUnexpected(dispatchResult.error());
            const ReflectionCsgDispatchHandle& dispatch = *dispatchResult;
            if(!dispatch)
                return MakeUnexpected(Failure{});
            const u32 rayCapacity = Min(resources.parameters.maxHardwareRays, resources.parameters.queueCapacity);
            const u32 sliceCount = ResolveReflectionIndirectArgumentCount(rayCapacity, true);
            // Separate native packets let the driver retire bounded work; shared UAV state also orders CPU recording.
            for(u32 slice = 0u; slice < sliceCount; ++slice){
                const auto sliceIdentity = ReflectionCsgHardwareSliceIdentity(slice);
                if(!sliceIdentity){
                    NWB_LOGGER_ERROR(NWB_TEXT("RendererReflectionSystem: failed to format CSG hardware slice identity: {}"), ConvertString(sliceIdentity.error().message()));
                    return MakeUnexpected(Failure{});
                }
                Core::GpuTaskDesc sliceDesc = ReflectionTaskDesc(*sliceIdentity, s_ReflectionHardwareResolveTaskLabel, dependency);
                Core::GpuTaskSchedulingHint scheduling;
                scheduling.forceSubmissionBoundary = true;
                scheduling.allowPacketMerge = false;
                sliceDesc.setScheduling(scheduling).setResourceUses(uses);
                sliceDesc.setResourceSetUses(inputs.hardwareSetReads);
                sliceDesc.setTiming(Core::GpuTaskTimingMetadata{.policy = Core::GpuTaskTimingPolicy::PacketOnly});
                const auto dependencyResult4 = graph.addTask<CsgHardwareSliceTask>(sliceDesc, CsgHardwareSliceTask::Payload{ graphics, inputs.raytracingSystem, dispatch, slice, sliceCount, inputs.hardwarePreparationReady, inputs.hardwareDispatchLogged, });
                if(!dependencyResult4)
                    return MakeUnexpected(Failure{});
                dependency = *dependencyResult4;
            }
        }
        else if(!appendDispatch(s_ReflectionHardwareTaskName, s_ReflectionHardwareResolveTaskLabel, DispatchStage::Hardware))
            return MakeUnexpected(Failure{});
    }

    const auto dependencyResult5 = DeclareReflectionPostprocessTasks(resources, inputs, dependency, graph, graphics, scratchArena, result);
    if(!dependencyResult5)
        return MakeUnexpected(Failure{});
    dependency = *dependencyResult5;

    const auto completionResult = DeclareReflectionStatisticsAndFinalizeTasks(resources, inputs, feedbackReserved, dependency, graph, result);
    if(!completionResult)
        return MakeUnexpected(completionResult.error());
    result.completion = *completionResult;
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<FrameGraphReflectionResolveResult> FrameGraphReflectionResolve::declare(const FrameGraphReflectionResolveInputs& inputs, Core::Alloc::ScratchArena& scratchArena, RayTracingSceneGraphReads& sceneReads){
    FrameGraphReflectionResolveResult result{};
    using namespace RendererTaskGraphDetail;
    DeferredFrameTargets& deferredTargets = inputs.targets;
    const ECSRenderDetail::MeshViewBufferSnapshot& meshViewBufferSnapshot = inputs.meshViewSnapshot;
    const RayTracingSceneGraphResources& sceneResources = inputs.sceneResources;
    const ReflectionSettings& reflectionSettings = inputs.reflectionSettings;
    const CsgFrameState& csgFrameState = inputs.csgFrameState;
    const b8 useLaggedLightingHistory = inputs.useLaggedLightingHistory;
    const Core::GpuGraphResourceId specularRoughness = inputs.specularRoughness;
    const Core::GpuGraphResourceId refractionSpecularRoughness = inputs.refractionSpecularRoughness;
    const Core::GpuGraphResourceId normal = inputs.normal;
    const Core::GpuGraphResourceId depth = inputs.depth;
    const Core::GpuGraphResourceId worldPosition = inputs.worldPosition;
    const Core::GpuGraphResourceId refractionDepth = inputs.refractionDepth;
    const Core::GpuGraphResourceId refractionNormalIor = inputs.refractionNormalIor;
    const Core::GpuGraphResourceId opaqueColor = inputs.opaqueColor;
    const Core::GpuGraphResourceId meshView = inputs.meshView;
    const Core::GpuGraphResourceId currentBindlessSlots = inputs.currentBindlessSlots;
    const Core::GpuGraphResourceId sceneShading = inputs.sceneShading;
    const Core::GpuGraphResourceId lights = inputs.lights;
    const Core::GpuGraphResourceSetId hardwareTraceGeometrySet = inputs.hardwareTraceGeometrySet;
    const Core::GpuGraphResourceSetId traceMaterialSampledTextureSet = inputs.traceMaterialSampledTextureSet;
    ReflectionSceneContentStamp reflectionContentStamp = inputs.contentStamp;
    reflectionContentStamp.geometry = sceneResources.contentStamp.geometry;
    reflectionContentStamp.material = sceneResources.contentStamp.material;
    // CSG evaluation and lagged screen lighting do not have a matching content stamp for this first history policy.
    reflectionContentStamp.trusted = sceneResources.contentStamp.trusted && csgFrameState.empty() && !useLaggedLightingHistory;
    const ReflectionFrameSnapshot reflectionResources = m_reflectionSystem.snapshotFrameResources(
        deferredTargets,
        meshViewBufferSnapshot,
        inputs.reflectionSceneAvailable ? sceneResources : RayTracingSceneGraphResources{},
        reflectionSettings,
        inputs.reflectionFrameIndex,
        reflectionContentStamp
    );
    if(!reflectionResources.valid())
        return MakeUnexpected(Failure{});
    if(!sceneReads.valid() && reflectionResources.hasHardwareWork()){
        auto sceneReadsResult = ImportRayTracingSceneGraphReads(sceneResources, m_raytracingSystem.sceneTlasBackingInitialState(), inputs.traceContextUploadTask, m_graph, scratchArena);
        if(!sceneReadsResult){
            sceneReads = RayTracingSceneGraphReads(scratchArena);
            return MakeUnexpected(Failure{});
        }
        sceneReads = Move(*sceneReadsResult);
    }
    const Array<Core::GpuTaskResourceUse, 9> reflectionSurfaceReads = {
        ReadUse(specularRoughness),
        ReadUse(refractionSpecularRoughness),
        ReadUse(normal),
        ReadUse(depth),
        ReadUse(worldPosition),
        ReadUse(refractionDepth),
        ReadUse(refractionNormalIor),
        ReadUse(meshView, Core::ResourceStates::ConstantBuffer),
        ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer),
    };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> reflectionHardwareReads{scratchArena};
    // Scene shading and lights follow the scene reads.
    reflectionHardwareReads.reserve(sceneReads.uses.size() + sceneReads.csgUses.size() + s_ReflectionExtraHardwareReadCapacity);
    if(sceneReads.valid()){
        for(const Core::GpuTaskResourceUse& read : sceneReads.uses)
            reflectionHardwareReads.push_back(read);
        for(const Core::GpuTaskResourceUse& read : sceneReads.csgUses)
            reflectionHardwareReads.push_back(read);
        reflectionHardwareReads.push_back(ReadUse(sceneShading, Core::ResourceStates::ConstantBuffer));
        reflectionHardwareReads.push_back(ReadUse(lights));
    }
    const RendererFramePipelineDetail::TraceResourceSetUses reflectionSets = RendererFramePipelineDetail::MakeTraceResourceSetUses(hardwareTraceGeometrySet, traceMaterialSampledTextureSet);
    const ReflectionGraphInputs reflectionInputs{
        .raytracingSystem = m_raytracingSystem,
        .opaqueDepth = depth,
        .opaqueColor = opaqueColor,
        .surfaceReads = reflectionSurfaceReads,
        .hardwareReads = reflectionHardwareReads,
        .hardwareSetReads = Span<const Core::GpuTaskResourceSetUse>(reflectionSets.uses.data(), reflectionSets.useCount),
        .hardwarePreparationReady = &inputs.hardwarePreparationReady,
        .hardwareDispatchLogged = &inputs.hardwareDispatchLogged,
        .fallbackDispatchLogged = &inputs.fallbackDispatchLogged,
    };
    auto reflectionGraphResult = DeclareReflectionTasks(reflectionResources, reflectionInputs, inputs.lightingTask, m_graph, m_graphics, m_reflectionSystem, scratchArena);
    if(!reflectionGraphResult)
        return MakeUnexpected(Failure{});
    result.reflectionGraph = Move(*reflectionGraphResult);
    result.reflectionCompositeInputs = ReflectionCompositeInputs{
        .opaqueRadianceSlot = reflectionResources.parameters.opaqueRadianceSlot,
        .glassRadianceSlot = reflectionResources.parameters.glassRadianceSlot,
        .debugView = reflectionSettings.debugView,
    };
    return result;
}


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
