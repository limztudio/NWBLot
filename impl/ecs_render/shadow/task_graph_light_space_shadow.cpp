// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_light_space_shadow.h"

#include <impl/ecs_render/kernel/task_graph_queue_requests.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/kernel/timing_names.h>

#include <core/graphics/runtime/runtime.h>
#include <core/graphics/gpu_timing.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_light_space_shadow{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ViewTask{
    struct Payload{
        Core::GraphicsRuntime& graphics;
        const bool& shadowPrepared;
        LightSpaceShadowSnapshot snapshot;
    };

    [[nodiscard]] static bool record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
        if(context.commandIrCapture)
            return false;
        if(!payload.shadowPrepared)
            return true;
        Core::GpuTimingMeasure timing(
            payload.graphics.gpuTiming(), RendererGpuTimingScope::s_LightSpaceShadowViews, payload.graphics.getDevice(), commandList
        );

        return RecordLightSpaceViews(commandList, payload.graphics.getDevice().getDescriptorHeap(), payload.snapshot);
    }
};

struct ShadeTask{
    using Payload = ViewTask::Payload;

    [[nodiscard]] static bool record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
        if(context.commandIrCapture)
            return false;
        if(!payload.shadowPrepared)
            return true;
        Core::GpuTimingMeasure timing(
            payload.graphics.gpuTiming(), RendererGpuTimingScope::s_LightSpaceShadowShade, payload.graphics.getDevice(), commandList
        );

        return RecordLightSpaceShade(commandList, payload.graphics.getDevice().getDescriptorHeap(), payload.snapshot);
    }
};

struct CaptureTask{
    struct Payload{
        Core::GraphicsRuntime& graphics;
        const bool& shadowPrepared;
        LightSpaceShadowSnapshot snapshot;
        Vector<LightSpaceShadowCaster, Core::Alloc::GlobalArena> casters;
        bool transparent;

        Payload(const LightSpaceShadowGraphInputs& inputs, const bool transparent)
            : graphics(inputs.graphics)
            , shadowPrepared(inputs.shadowPrepared)
            , snapshot(inputs.snapshot)
            , casters(inputs.arena)
            , transparent(transparent)
        {
            casters.assign(inputs.snapshot.casters, inputs.snapshot.casters + inputs.snapshot.casterCount);
            snapshot.casters = nullptr;
            snapshot.casterCount = 0u;
        }
    };

    [[nodiscard]] static bool record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
        if(context.commandIrCapture)
            return false;
        if(!payload.shadowPrepared)
            return true;
        LightSpaceShadowSnapshot snapshot = payload.snapshot;
        snapshot.casters = payload.casters.data();
        snapshot.casterCount = payload.casters.size();
        const auto& scope = payload.transparent
            ? RendererGpuTimingScope::s_LightSpaceShadowTransparentCapture : RendererGpuTimingScope::s_LightSpaceShadowOpaqueCapture;
        Core::GpuTimingMeasure timing(payload.graphics.gpuTiming(), scope, payload.graphics.getDevice(), commandList);

        for(u32 view = 0u; view < snapshot.plan.viewCount; ++view){
            if(!RecordLightSpaceCapture(
                commandList, payload.graphics.getDevice().getDescriptorHeap(), snapshot, view, payload.transparent
            ))
                return false;
        }
        return true;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


LightSpaceShadowGraph DeclareLightSpaceShadowMaps(Core::GpuTaskGraph& graph, const LightSpaceShadowGraphInputs& inputs){
    using namespace RendererTaskGraphDetail;
    const auto& snapshot = inputs.snapshot;
    if(!snapshot.ready || !inputs.dependency.valid() || !snapshot.casters || snapshot.casterCount == 0u)
        return {};
    const auto importBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label){
        return graph.importBuffer(buffer, BufferResourceDesc(identity, label)
            .setInitialState(Core::ResourceStates::Common).setExternalFinalState(Core::ResourceStates::Common));
    };
    LightSpaceShadowGraph result;
    result.counts = importBuffer(snapshot.counts, Name("render.light_space_shadow.counts"), "Light-Space Crossing Counts");
    result.events = importBuffer(snapshot.events, Name("render.light_space_shadow.events"), "Light-Space Crossings");
    result.views = importBuffer(snapshot.views, Name("render.light_space_shadow.views"), "Light-Space Views");
    result.drawArguments = importBuffer(snapshot.drawArguments, Name("render.light_space_shadow.draw_arguments"), "Light-Space Draw Arguments");
    // Opaque capture clears every layer. Unknown preserves fresh Undefined; accepted state sources retain native states.
    result.depth = graph.importTexture(snapshot.depth,
        TextureResourceDesc(Name("render.light_space_shadow.depth"), "Light-Space Opaque Depth")
            .setInitialState(Core::ResourceStates::Unknown).setExternalFinalState(Core::ResourceStates::Common));
    if(!result.counts.valid() || !result.events.valid() || !result.views.valid() || !result.drawArguments.valid() || !result.depth.valid())
        return {};
    const Core::GpuUploadBlobId upload = graph.copyUploadData(
        snapshot.plan.views.data(), snapshot.plan.viewCount * sizeof(LightSpaceViewGpu), alignof(u32)
    );
    if(!upload.valid())
        return {};
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Tiny;
    scheduling.allowPacketMerge = true;
    scheduling.mergeWithPrevious = true;
    scheduling.allowMergeAcrossConsumerFrontier = true;
    Core::GpuTaskDesc uploadDesc;
    uploadDesc
        .setIdentity(Name("render.light_space_shadow.view_upload"))
        .setMarkerLabel("Light-Space View Upload")
        .setQueue(GraphicsUploadQueueRequest())
        .setScheduling(scheduling)
        .setDependencies(&inputs.dependency, 1u)
        .setExternalStateSources(inputs.stateSources, inputs.stateSourceCount)
    ;
    result.viewUpload = graph.addUploadBufferTask(uploadDesc, Core::GpuUploadBufferTaskDesc{
        .source = upload,
        .destination = result.views,
        .finalState = Core::ResourceStates::Common,
    });
    if(!result.viewUpload.valid())
        return {};
    Core::GpuTaskDesc clearDesc;
    clearDesc
        .setIdentity(Name("render.light_space_shadow.counts_clear"))
        .setMarkerLabel("Light-Space Crossing Clear")
        .setQueue(GraphicsUploadQueueRequest())
        .setScheduling(scheduling)
        .setDependencies(&result.viewUpload, 1u)
        .setExternalStateSources(inputs.stateSources, inputs.stateSourceCount)
    ;
    result.countsClear = graph.addClearBufferTask(clearDesc, Core::GpuClearBufferTaskDesc{
        .destination = result.counts,
        .clearValue = 0u,
    });
    if(!result.countsClear.valid())
        return {};

    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> uses(inputs.scratchArena);
    uses.reserve(inputs.sceneReadCount + 5u);
    for(usize index = 0u; index < inputs.sceneReadCount; ++index)
        uses.push_back(inputs.sceneReads[index]);
    uses.push_back(ReadWriteUse(result.views, Core::ResourceStates::UnorderedAccess));
    uses.push_back(WriteUse(result.drawArguments, Core::ResourceStates::UnorderedAccess));
    Core::GpuTaskDesc viewDesc;
    viewDesc
        .setIdentity(Name("render.light_space_shadow.view_fit"))
        .setMarkerLabel("Light-Space View Fit")
        .setQueue(GraphicsPreferredComputeQueueRequest())
        .setScheduling(scheduling)
        .setDependencies(&result.countsClear, 1u)
        .setExternalStateSources(inputs.stateSources, inputs.stateSourceCount)
        .setResourceUses(uses.data(), uses.size())
        .setResourceSetUses(inputs.sceneReadSets, inputs.sceneReadSetCount)
    ;
    result.viewFit = graph.addTask<__hidden_task_graph_light_space_shadow::ViewTask>(viewDesc,
        __hidden_task_graph_light_space_shadow::ViewTask::Payload{
            inputs.graphics, inputs.shadowPrepared, snapshot,
        });
    if(!result.viewFit.valid())
        return {};

    const Core::TextureSubresourceSet layers{ 0u, 1u, 0u, snapshot.plan.viewCount };
    uses.resize(inputs.sceneReadCount);
    uses.push_back(ReadUse(result.views, Core::ResourceStates::ShaderResource));
    uses.push_back(ReadUse(result.drawArguments, Core::ResourceStates::IndirectArgument));
    uses.push_back(WriteTextureUse(result.depth, layers, Core::ResourceStates::DepthWrite));
    scheduling.cost = Core::GpuTaskCostHint::Large;
    Core::GpuTaskDesc opaqueDesc;
    opaqueDesc
        .setIdentity(Name("render.light_space_shadow.opaque_capture"))
        .setMarkerLabel("Light-Space Opaque Capture")
        .setQueue(GraphicsQueueRequest())
        .setScheduling(scheduling)
        .setDependencies(&result.viewFit, 1u)
        .setExternalStateSources(inputs.stateSources, inputs.stateSourceCount)
        .setResourceUses(uses.data(), uses.size())
        .setResourceSetUses(inputs.sceneReadSets, inputs.sceneReadSetCount)
    ;
    result.opaqueCapture = graph.addTask<__hidden_task_graph_light_space_shadow::CaptureTask>(opaqueDesc,
        __hidden_task_graph_light_space_shadow::CaptureTask::Payload(inputs, false));
    if(!result.opaqueCapture.valid())
        return {};

    uses.pop_back();
    uses.push_back(ReadTextureUse(result.depth, layers, Core::ResourceStates::DepthRead));
    uses.push_back(ReadWriteUse(result.counts, Core::ResourceStates::UnorderedAccess));
    uses.push_back(WriteUse(result.events, Core::ResourceStates::UnorderedAccess));
    Core::GpuTaskDesc transparentDesc;
    transparentDesc
        .setIdentity(Name("render.light_space_shadow.transparent_capture"))
        .setMarkerLabel("Light-Space Transparent Capture")
        .setQueue(GraphicsQueueRequest())
        .setScheduling(scheduling)
        .setDependencies(&result.opaqueCapture, 1u)
        .setExternalStateSources(inputs.stateSources, inputs.stateSourceCount)
        .setResourceUses(uses.data(), uses.size())
        .setResourceSetUses(inputs.sceneReadSets, inputs.sceneReadSetCount)
    ;
    result.transparentCapture = graph.addTask<__hidden_task_graph_light_space_shadow::CaptureTask>(transparentDesc,
        __hidden_task_graph_light_space_shadow::CaptureTask::Payload(inputs, true));
    if(!result.transparentCapture.valid())
        return {};

    uses.resize(inputs.sceneReadCount);
    uses.push_back(ReadUse(result.views, Core::ResourceStates::ShaderResource));
    uses.push_back(ReadUse(result.counts, Core::ResourceStates::ShaderResource));
    uses.push_back(ReadWriteUse(result.events, Core::ResourceStates::UnorderedAccess));
    Core::GpuTaskDesc shadeDesc;
    shadeDesc
        .setIdentity(Name("render.light_space_shadow.shade"))
        .setMarkerLabel("Light-Space Crossing Shade")
        .setQueue(GraphicsPreferredComputeQueueRequest())
        .setScheduling(scheduling)
        .setDependencies(&result.transparentCapture, 1u)
        .setExternalStateSources(inputs.stateSources, inputs.stateSourceCount)
        .setResourceUses(uses.data(), uses.size())
        .setResourceSetUses(inputs.sceneReadSets, inputs.sceneReadSetCount)
    ;
    result.shade = graph.addTask<__hidden_task_graph_light_space_shadow::ShadeTask>(shadeDesc,
        __hidden_task_graph_light_space_shadow::ShadeTask::Payload{
            inputs.graphics, inputs.shadowPrepared, snapshot,
        });
    return result.valid() ? result : LightSpaceShadowGraph{};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

