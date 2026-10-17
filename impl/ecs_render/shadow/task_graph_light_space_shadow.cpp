// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_light_space_shadow.h"

#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/kernel/task_graph_scheduling.h>
#include <impl/ecs_render/kernel/timing_names.h>

#include <core/graphics/runtime/runtime.h>
#include <core/graphics/gpu_timing.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_light_space_shadow{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Capture needs one simultaneous SRV/index state for each canonical buffer. Separate set uses would describe successive states.
[[nodiscard]] bool GatherCaptureReads(const Core::GpuTaskGraph& graph, const LightSpaceShadowGraphInputs& inputs, Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena>& outUses){
    const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
    if(!declarations.valid())
        return false;
    usize capacity = inputs.sceneReadCount + inputs.snapshot.casterCount + 5u;
    for(usize index = 0u; index < inputs.sceneReadSetCount; ++index){
        const Core::GpuTaskResourceSetUse& use = inputs.sceneReadSets[index];
        if(!declarations.validResourceSet(use.resourceSet))
            return false;
        capacity += declarations.resourceSetAt(use.resourceSet.index).memberCount;
    }
    outUses.clear();
    outUses.reserve(capacity);
    const auto appendRead = [&](const Core::GpuTaskResourceUse& use){
        // The light-space scene inputs are whole-resource reads, including material texture sets.
        NWB_ASSERT(use.access == Core::GpuTaskResourceAccess::Read);
        NWB_ASSERT(use.range.bufferRange == Core::s_EntireBuffer && use.range.textureSubresources == Core::s_AllSubresources);
        for(auto& previous : outUses){
            if(previous.resource == use.resource){
                previous.requiredState |= use.requiredState;
                previous.hasIndependentStateSource = previous.hasIndependentStateSource && use.hasIndependentStateSource;
                return;
            }
        }
        outUses.push_back(use);
    };
    for(usize index = 0u; index < inputs.sceneReadCount; ++index)
        appendRead(inputs.sceneReads[index]);
    for(usize index = 0u; index < inputs.sceneReadSetCount; ++index){
        const Core::GpuTaskResourceSetUse& use = inputs.sceneReadSets[index];
        const Core::GpuTaskGraphResourceSetView members = declarations.resourceSetAt(use.resourceSet.index);
        for(usize member = 0u; member < members.memberCount; ++member){
            appendRead(Core::GpuTaskResourceUse{ .resource = members.members[member], .range = use.range, .requiredState = use.requiredState, .access = use.access, .hasIndependentStateSource = use.hasIndependentStateSource, });
        }
    }
    for(usize index = 0u; index < inputs.snapshot.casterCount; ++index){
        const auto& caster = inputs.snapshot.casters[index];
        // The scene geometry importer already retains this exact frozen canonical index buffer.
        const Core::GpuGraphResourceId resource = declarations.findImportedBuffer(caster.triangleIndexBuffer);
        if(!resource.valid())
            return false;
        appendRead(RendererTaskGraphDetail::ReadUse(resource, Core::ResourceStates::ShaderResource | Core::ResourceStates::IndexBuffer));
    }
    return true;
}


struct ViewTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };

    struct Payload{
        Core::GraphicsRuntime& graphics;
        const bool& shadowPrepared;
        LightSpaceShadowSnapshot snapshot;
    };


public:
    [[nodiscard]] static bool Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
        if(context.commandIrCapture)
            return false;
        if(!payload.shadowPrepared)
            return true;
        Core::GpuTimingMeasure timing(payload.graphics.gpuTiming(), RendererGpuTimingScope::s_LightSpaceShadowViews, payload.graphics.getDevice(), commandList);

        return RecordLightSpaceViews(commandList, payload.graphics.getDevice().getDescriptorHeap(), payload.snapshot);
    }
};

struct ShadeTask{
    using Payload = ViewTask::Payload;


public:
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };


public:
    [[nodiscard]] static bool Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
        if(context.commandIrCapture)
            return false;
        if(!payload.shadowPrepared)
            return true;
        Core::GpuTimingMeasure timing(payload.graphics.gpuTiming(), RendererGpuTimingScope::s_LightSpaceShadowShade, payload.graphics.getDevice(), commandList);

        if(!RecordLightSpaceShade(commandList, payload.graphics.getDevice().getDescriptorHeap(), payload.snapshot))
            return false;
        if(payload.snapshot.captureHistory)
            payload.snapshot.captureHistory->recordCapture(payload.snapshot.captureTicket);
        return true;
    }
};

struct DiagnosticTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute | Core::GpuQueueCapability::Transfer, };

    struct Payload{
        const bool& shadowPrepared;
        Core::BufferHandle context;
        Core::BufferHandle readback;
        LightSpaceShadowDiagnostics* diagnostics;
        u32 frameIndex;
    };


public:
    [[nodiscard]] static bool Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
        if(context.commandIrCapture)
            return false;
        if(!payload.shadowPrepared)
            return true;
        // The task's public state stays UAV; only this bounded copy temporarily reads the accumulated diagnostic word.
        commandList.setBufferState(payload.context.get(), Core::ResourceStates::CopySource);
        commandList.commitBarriers();
        commandList.copyBuffer(*payload.readback, 0u, *payload.context, NWB_CSG_SHADOW_RAY_FAILURE_OFFSET, sizeof(u32));
        commandList.setBufferState(payload.context.get(), Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();
        return true;
    }

    static void Accepted(Payload& payload, const Core::QueueSubmissionToken& token)noexcept{
        if(payload.shadowPrepared && payload.diagnostics && payload.diagnostics->readback == payload.readback){
            payload.diagnostics->submission = token;
            payload.diagnostics->acceptedFrame = payload.frameIndex;
        }
    }
};

struct CasterPayload{
    Core::GraphicsRuntime& graphics;
    const bool& shadowPrepared;
    LightSpaceShadowSnapshot snapshot;
    Vector<LightSpaceShadowCaster, Core::Alloc::GlobalArena> casters;


public:
    explicit CasterPayload(const LightSpaceShadowGraphInputs& inputs)
        : graphics(inputs.graphics)
        , shadowPrepared(inputs.shadowPrepared)
        , snapshot(inputs.snapshot)
        , casters(inputs.arena)
    {
        casters.assign(inputs.snapshot.casters, inputs.snapshot.casters + inputs.snapshot.casterCount);
        snapshot.casters = nullptr;
        snapshot.casterCount = 0u;
    }


public:
    [[nodiscard]] LightSpaceShadowSnapshot frozenSnapshot()const noexcept{
        LightSpaceShadowSnapshot result = snapshot;
        result.casters = casters.data();
        result.casterCount = casters.size();
        return result;
    }
};

struct CullTask{
    using Payload = CasterPayload;


public:
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };


public:
    [[nodiscard]] static bool Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
        if(context.commandIrCapture)
            return false;
        if(!payload.shadowPrepared)
            return true;
        Core::GpuTimingMeasure timing(payload.graphics.gpuTiming(), RendererGpuTimingScope::s_LightSpaceShadowCull, payload.graphics.getDevice(), commandList);

        return RecordLightSpaceCull(commandList, payload.graphics.getDevice().getDescriptorHeap(), payload.frozenSnapshot());
    }
};

struct CaptureTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Graphics };

    struct Payload : CasterPayload{
        bool transparent;


    public:
        Payload(const LightSpaceShadowGraphInputs& inputs, const bool transparent)
            : CasterPayload(inputs)
            , transparent(transparent)
        {}
    };


public:
    [[nodiscard]] static bool Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
        if(context.commandIrCapture)
            return false;
        if(!payload.shadowPrepared)
            return true;
        const LightSpaceShadowSnapshot snapshot = payload.frozenSnapshot();
        const auto& scope = payload.transparent ? RendererGpuTimingScope::s_LightSpaceShadowTransparentCapture : RendererGpuTimingScope::s_LightSpaceShadowOpaqueCapture;
        Core::GpuTimingMeasure timing(payload.graphics.gpuTiming(), scope, payload.graphics.getDevice(), commandList);

        for(u32 view = 0u; view < snapshot.plan.viewCount; ++view){
            if(!RecordLightSpaceCapture(commandList, payload.graphics.getDevice().getDescriptorHeap(), snapshot, view, payload.transparent))
                return false;
        }
        return true;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Core::GpuTaskId> DeclareLightSpaceShadowDiagnostics(Core::GpuTaskGraph& graph, const LightSpaceShadowGraphInputs& inputs, const Core::GpuGraphResourceId context){
    using namespace RendererTaskGraphDetail;
    const auto& snapshot = inputs.snapshot;
    if(!snapshot.ready || !snapshot.diagnostics || !snapshot.diagnosticReadback || !inputs.dependency.valid() || !context.valid())
        return MakeUnexpected(Failure{});
    const auto readbackResult = graph.importBuffer(snapshot.diagnosticReadback,
        BufferResourceDesc(Name("render.light_space_shadow.diagnostics"), "CSG Shadow Diagnostics")
            .setInitialState(Core::ResourceStates::CopyDest).setExternalFinalState(Core::ResourceStates::CopyDest)
    );
    if(!readbackResult)
        return MakeUnexpected(Failure{});
    const Core::GpuGraphResourceId readback = *readbackResult;
    const Core::GpuTaskResourceUse uses[]{ ReadWriteUse(context, Core::ResourceStates::UnorderedAccess), WriteUse(readback, Core::ResourceStates::CopyDest), };
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Tiny;
    scheduling.allowPacketMerge = true;
    scheduling.mergeWithPrevious = true;
    scheduling.allowMergeAcrossConsumerFrontier = true;
    EnableSameFamilyComputeEffectRouting(scheduling);
    EnableCrossFamilyComputeEffectRouting(scheduling);
    Core::GpuTaskDesc desc;
    desc
        .setIdentity(Name("render.light_space_shadow.diagnostics"))
        .setMarkerLabel("CSG Shadow Diagnostics")
        .setScheduling(scheduling).setDependencies(&inputs.dependency, 1u)
        .setExternalStateSources(inputs.stateSources, inputs.stateSourceCount)
        .setResourceUses(uses, LengthOf(uses))
    ;
    return graph.addTask<__hidden_task_graph_light_space_shadow::DiagnosticTask>(desc,
        __hidden_task_graph_light_space_shadow::DiagnosticTask::Payload{ inputs.shadowPrepared, snapshot.csgContext, snapshot.diagnosticReadback, snapshot.diagnostics, snapshot.diagnosticFrame, }
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<LightSpaceShadowGraph> DeclareLightSpaceShadowMaps(Core::GpuTaskGraph& graph, const LightSpaceShadowGraphInputs& inputs){
    using namespace RendererTaskGraphDetail;
    const auto& snapshot = inputs.snapshot;
    if(!snapshot.ready || !inputs.dependency.valid() || !snapshot.casters || snapshot.casterCount == 0u)
        return MakeUnexpected(Failure{});
    const auto importBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label){
        return graph.importBuffer(buffer, BufferResourceDesc(identity, label).setInitialState(Core::ResourceStates::Common).setExternalFinalState(Core::ResourceStates::Common));
    };
    LightSpaceShadowGraph result;
    const auto resultCountsResult = importBuffer(snapshot.counts, Name("render.light_space_shadow.counts"), "Light-Space Crossing Counts");
    const auto resultEventsResult = importBuffer(snapshot.events, Name("render.light_space_shadow.events"), "Light-Space Crossings");
    const auto resultViewsResult = importBuffer(snapshot.views, Name("render.light_space_shadow.views"), "Light-Space Views");
    // Opaque capture clears every layer. Unknown preserves fresh Undefined; accepted state sources retain native states.
    const auto resultDepthResult = graph.importTexture(snapshot.depth, TextureResourceDesc(Name("render.light_space_shadow.depth"), "Light-Space Opaque Depth").setInitialState(Core::ResourceStates::Unknown).setExternalFinalState(Core::ResourceStates::Common));
    if(!resultCountsResult || !resultEventsResult || !resultViewsResult || !resultDepthResult){
        if(resultCountsResult)
            result.counts = *resultCountsResult;
        else
            result.counts = {};
        if(resultEventsResult)
            result.events = *resultEventsResult;
        else
            result.events = {};
        if(resultViewsResult)
            result.views = *resultViewsResult;
        else
            result.views = {};
        if(resultDepthResult)
            result.depth = *resultDepthResult;
        else
            result.depth = {};
        return MakeUnexpected(Failure{});
    }
    result.counts = *resultCountsResult;
    result.events = *resultEventsResult;
    result.views = *resultViewsResult;
    result.depth = *resultDepthResult;
    const bool csg = (snapshot.push.csgFlags & NWB_CSG_SHADOW_FLAG_ENABLED) != 0u;
    if(csg){
        const auto resultCsgContextResult = importBuffer(snapshot.csgContext, Name("render.light_space_shadow.csg_context"), "CSG Shadow Context");
        const auto resultCsgOpaqueDepthResult = importBuffer(snapshot.csgOpaqueDepth, Name("render.light_space_shadow.csg_depth"), "CSG Shadow Depth");
        if(!resultCsgContextResult || !resultCsgOpaqueDepthResult){
            if(resultCsgContextResult)
                result.csgContext = *resultCsgContextResult;
            else
                result.csgContext = {};
            if(resultCsgOpaqueDepthResult)
                result.csgOpaqueDepth = *resultCsgOpaqueDepthResult;
            else
                result.csgOpaqueDepth = {};
            return MakeUnexpected(Failure{});
        }
        result.csgContext = *resultCsgContextResult;
        result.csgOpaqueDepth = *resultCsgOpaqueDepthResult;
    }
    if(snapshot.captureTicket.reuse){
        // Receiver tasks retain all current scene/BVH reads and the accepted native map state sources.
        result.ready = inputs.dependency;
        return result;
    }
    // A retained draw buffer is untouched during reuse; importing its final state would promise an unowned graph export.
    const auto resultDrawArgumentsResult = importBuffer(snapshot.drawArguments, Name("render.light_space_shadow.draw_arguments"), "Light-Space Draw Arguments");
    if(!resultDrawArgumentsResult){
        result.drawArguments = {};
        return MakeUnexpected(Failure{});
    }
    result.drawArguments = *resultDrawArgumentsResult;
    const auto uploadResult = graph.copyUploadData(snapshot.plan.views.data(), snapshot.plan.viewCount * sizeof(LightSpaceViewGpu), alignof(u32));
    if(!uploadResult)
        return MakeUnexpected(Failure{});
    const Core::GpuUploadBlobId upload = *uploadResult;
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Tiny;
    scheduling.allowPacketMerge = true;
    scheduling.mergeWithPrevious = true;
    scheduling.allowMergeAcrossConsumerFrontier = true;
    Core::GpuTaskDesc uploadDesc;
    uploadDesc.setIdentity(Name("render.light_space_shadow.view_upload")).setMarkerLabel("Light-Space View Upload").setScheduling(scheduling).setDependencies(&inputs.dependency, 1u).setExternalStateSources(inputs.stateSources, inputs.stateSourceCount);
    Core::GpuTaskId csgUpload;
    if(csg){
        if(!snapshot.diagnostics || snapshot.csgContextByteCount < NWB_CSG_RAY_CONTEXT_BYTES)
            return MakeUnexpected(Failure{});
        const bool initialize = !snapshot.diagnostics->contextInitialized.valid();
        const usize prefixBytes = initialize ? snapshot.csgContextByteCount : NWB_CSG_SHADOW_RAY_FAILURE_OFFSET;
        const auto csgBytesResult = graph.copyUploadData(snapshot.csgContextBytes, prefixBytes, alignof(u32));
        if(!csgBytesResult)
            return MakeUnexpected(Failure{});
        const Core::GpuUploadBlobId csgBytes = *csgBytesResult;
        uploadDesc.setIdentity(Name("render.light_space_shadow.csg_upload")).setMarkerLabel("CSG Shadow Context Upload");
        const auto csgUploadResult = graph.addUploadBufferTask(uploadDesc, Core::GpuUploadBufferTaskDesc{
            .source = csgBytes, .destination = result.csgContext, .finalState = Core::ResourceStates::Common,
            .acceptedToken = initialize ? &snapshot.diagnostics->contextInitialized : nullptr,
        }
        );
        if(!csgUploadResult){
            csgUpload = {};
            return MakeUnexpected(Failure{});
        }
        csgUpload = *csgUploadResult;
        if(!initialize){
            // Retain accumulated failures across new captures until the bounded asynchronous readback observes them.
            constexpr u32 s_PayloadOffset = NWB_CSG_SHADOW_RAY_FAILURE_OFFSET + sizeof(u32);
            const auto payloadResult = graph.copyUploadData(snapshot.csgContextBytes + s_PayloadOffset, snapshot.csgContextByteCount - s_PayloadOffset, alignof(u32));
            if(!payloadResult)
                return MakeUnexpected(Failure{});
            const Core::GpuUploadBlobId payload = *payloadResult;
            uploadDesc.setIdentity(Name("render.light_space_shadow.csg_payload_upload")).setMarkerLabel("CSG Shadow Payload Upload").setDependencies(&csgUpload, 1u);
            const auto csgUploadResult2 = graph.addUploadBufferTask(uploadDesc, Core::GpuUploadBufferTaskDesc{ .source = payload, .destination = result.csgContext, .destinationOffsetBytes = s_PayloadOffset, .finalState = Core::ResourceStates::Common, });
            if(!csgUploadResult2){
                csgUpload = {};
                return MakeUnexpected(Failure{});
            }
            csgUpload = *csgUploadResult2;
        }
        uploadDesc.setIdentity(Name("render.light_space_shadow.view_upload")).setMarkerLabel("Light-Space View Upload").setDependencies(&csgUpload, 1u);
    }
    const auto resultViewUploadResult = graph.addUploadBufferTask(uploadDesc, Core::GpuUploadBufferTaskDesc{ .source = upload, .destination = result.views, .finalState = Core::ResourceStates::Common, });
    if(!resultViewUploadResult){
        result.viewUpload = {};
        return MakeUnexpected(Failure{});
    }
    result.viewUpload = *resultViewUploadResult;
    Core::GpuTaskDesc clearDesc;
    clearDesc.setIdentity(Name("render.light_space_shadow.counts_clear")).setMarkerLabel("Light-Space Crossing Clear").setScheduling(scheduling).setDependencies(&result.viewUpload, 1u).setExternalStateSources(inputs.stateSources, inputs.stateSourceCount);
    const auto resultCountsClearResult = graph.addClearBufferTask(clearDesc, Core::GpuClearBufferTaskDesc{ .destination = result.counts, .clearValue = 0u, });
    if(!resultCountsClearResult){
        result.countsClear = {};
        return MakeUnexpected(Failure{});
    }
    result.countsClear = *resultCountsClearResult;

    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> uses(inputs.scratchArena);
    uses.reserve(inputs.sceneReadCount + 5u);
    for(usize index = 0u; index < inputs.sceneReadCount; ++index)
        uses.push_back(inputs.sceneReads[index]);
    if(csg)
        uses.push_back(ReadUse(result.csgContext, Core::ResourceStates::ShaderResource));
    uses.push_back(ReadWriteUse(result.views, Core::ResourceStates::UnorderedAccess));
    Core::GpuTaskDesc viewDesc;
    viewDesc
        .setIdentity(Name("render.light_space_shadow.view_fit"))
        .setMarkerLabel("Light-Space View Fit")
        .setScheduling(scheduling)
        .setDependencies(&result.countsClear, 1u)
        .setExternalStateSources(inputs.stateSources, inputs.stateSourceCount)
        .setResourceUses(uses.data(), uses.size())
        .setResourceSetUses(inputs.sceneReadSets, inputs.sceneReadSetCount)
    ;
    const auto resultViewFitResult = graph.addTask<__hidden_task_graph_light_space_shadow::ViewTask>(viewDesc, __hidden_task_graph_light_space_shadow::ViewTask::Payload{ inputs.graphics, inputs.shadowPrepared, snapshot, });
    if(!resultViewFitResult){
        result.viewFit = {};
        return MakeUnexpected(Failure{});
    }
    result.viewFit = *resultViewFitResult;

    uses.pop_back();
    uses.push_back(ReadUse(result.views, Core::ResourceStates::ShaderResource));
    uses.push_back(WriteUse(result.drawArguments, Core::ResourceStates::UnorderedAccess));
    for(usize index = 0u; index < snapshot.casterCount; ++index){
        const auto& caster = snapshot.casters[index];
        if(caster.meshletCount == 0u)
            continue;
        const ReferenceWrapper<const Core::BufferHandle> buffers[] = { caster.meshletDescBuffer, caster.meshletBoundsBuffer };
        for(const Core::BufferHandle& buffer : buffers){
            if(!buffer)
                return MakeUnexpected(Failure{});
            Core::GpuGraphResourceId resource;
            {
                const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
                resource = declarations.findImportedBuffer(buffer);
            }
            if(!resource.valid()){
                const auto importedResource = importBuffer(buffer, buffer->getCreationDescription().debugName, "Light-Space Meshlet Bounds");
                if(!importedResource)
                    return MakeUnexpected(Failure{});
                resource = *importedResource;
            }
            if(!resource.valid())
                return MakeUnexpected(Failure{});
            if(FindIf(uses.begin(), uses.end(), [&](const auto& use)noexcept{ return use.resource == resource; }) == uses.end())
                uses.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
        }
    }
    Core::GpuTaskDesc cullDesc;
    cullDesc
        .setIdentity(Name("render.light_space_shadow.draw_cull"))
        .setMarkerLabel("Light-Space Meshlet Cull")
        .setScheduling(scheduling)
        .setDependencies(&result.viewFit, 1u)
        .setExternalStateSources(inputs.stateSources, inputs.stateSourceCount)
        .setResourceUses(uses.data(), uses.size())
        .setResourceSetUses(inputs.sceneReadSets, inputs.sceneReadSetCount)
    ;
    const auto resultDrawCullResult = graph.addTask<__hidden_task_graph_light_space_shadow::CullTask>(cullDesc, __hidden_task_graph_light_space_shadow::CullTask::Payload(inputs));
    if(!resultDrawCullResult){
        result.drawCull = {};
        return MakeUnexpected(Failure{});
    }
    result.drawCull = *resultDrawCullResult;

    const Core::TextureSubresourceSet layers{ 0u, 1u, 0u, snapshot.plan.viewCount };
    if(!__hidden_task_graph_light_space_shadow::GatherCaptureReads(graph, inputs, uses))
        return MakeUnexpected(Failure{});
    if(csg)
        uses.push_back(ReadUse(result.csgContext, Core::ResourceStates::ShaderResource));
    uses.push_back(ReadUse(result.views, Core::ResourceStates::ShaderResource));
    uses.push_back(ReadUse(result.drawArguments, Core::ResourceStates::IndirectArgument));
    uses.push_back(WriteTextureUse(result.depth, layers, Core::ResourceStates::DepthWrite));
    scheduling.cost = Core::GpuTaskCostHint::Large;
    Core::GpuTaskDesc opaqueDesc;
    opaqueDesc
        .setIdentity(Name("render.light_space_shadow.opaque_capture"))
        .setMarkerLabel("Light-Space Opaque Capture")
        .setScheduling(scheduling)
        .setDependencies(&result.drawCull, 1u)
        .setExternalStateSources(inputs.stateSources, inputs.stateSourceCount)
        .setResourceUses(uses.data(), uses.size())
    ;
    const auto resultOpaqueCaptureResult = graph.addTask<__hidden_task_graph_light_space_shadow::CaptureTask>(opaqueDesc, __hidden_task_graph_light_space_shadow::CaptureTask::Payload(inputs, false));
    if(!resultOpaqueCaptureResult){
        result.opaqueCapture = {};
        return MakeUnexpected(Failure{});
    }
    result.opaqueCapture = *resultOpaqueCaptureResult;

    uses.pop_back();
    uses.push_back(ReadTextureUse(result.depth, layers, Core::ResourceStates::DepthRead));
    uses.push_back(ReadWriteUse(result.counts, Core::ResourceStates::UnorderedAccess));
    uses.push_back(WriteUse(result.events, Core::ResourceStates::UnorderedAccess));
    Core::GpuTaskDesc transparentDesc;
    transparentDesc
        .setIdentity(Name("render.light_space_shadow.transparent_capture"))
        .setMarkerLabel("Light-Space Transparent Capture")
        .setScheduling(scheduling)
        .setDependencies(&result.opaqueCapture, 1u)
        .setExternalStateSources(inputs.stateSources, inputs.stateSourceCount)
        .setResourceUses(uses.data(), uses.size())
    ;
    const auto resultTransparentCaptureResult = graph.addTask<__hidden_task_graph_light_space_shadow::CaptureTask>(transparentDesc, __hidden_task_graph_light_space_shadow::CaptureTask::Payload(inputs, true));
    if(!resultTransparentCaptureResult){
        result.transparentCapture = {};
        return MakeUnexpected(Failure{});
    }
    result.transparentCapture = *resultTransparentCaptureResult;

    uses.clear();
    for(usize index = 0u; index < inputs.sceneReadCount; ++index)
        uses.push_back(inputs.sceneReads[index]);
    if(csg)
        uses.push_back(ReadUse(result.csgContext, Core::ResourceStates::ShaderResource));
    uses.push_back(ReadUse(result.views, Core::ResourceStates::ShaderResource));
    uses.push_back(csg ? ReadWriteUse(result.counts, Core::ResourceStates::UnorderedAccess) : ReadUse(result.counts, Core::ResourceStates::ShaderResource));
    if(csg)
        uses.push_back(WriteUse(result.csgOpaqueDepth, Core::ResourceStates::UnorderedAccess));
    uses.push_back(ReadWriteUse(result.events, Core::ResourceStates::UnorderedAccess));
    Core::GpuTaskDesc shadeDesc;
    shadeDesc
        .setIdentity(Name("render.light_space_shadow.shade"))
        .setMarkerLabel("Light-Space Crossing Shade")
        .setScheduling(scheduling)
        .setDependencies(&result.transparentCapture, 1u)
        .setExternalStateSources(inputs.stateSources, inputs.stateSourceCount)
        .setResourceUses(uses.data(), uses.size())
        .setResourceSetUses(inputs.sceneReadSets, inputs.sceneReadSetCount)
    ;
    const auto shadeResult = graph.addTask<__hidden_task_graph_light_space_shadow::ShadeTask>(shadeDesc, __hidden_task_graph_light_space_shadow::ShadeTask::Payload{ inputs.graphics, inputs.shadowPrepared, snapshot, });
    if(!shadeResult)
        return MakeUnexpected(shadeResult.error());
    result.shade = *shadeResult;
    result.ready = result.shade;
    if(!result.valid())
        return MakeUnexpected(Failure{});
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

