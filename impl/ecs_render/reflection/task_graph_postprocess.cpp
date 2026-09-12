// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_postprocess.h"
#include "timing_names.h"

#include <core/graphics/vulkan/backend.h>

#include <impl/assets/graphics/reflection/temporal_constants.h>
#include <impl/assets/graphics/reflection/spatial_constants.h>
#include <impl/ecs_render/kernel/task_graph_queue_requests.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>

#include <core/graphics/runtime/runtime.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_reflection_postprocess_tasks{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

[[nodiscard]] Core::GpuGraphResourceId ImportImage(Core::GpuTaskGraph& graph, const Core::TextureHandle& texture, const bool firstWrite){
    {
        const Core::GpuTaskGraph::DeclarationReadView view(graph);
        if(!view.valid())
            return {};
        const Core::GpuGraphResourceId existing = view.findImportedTexture(texture);
        if(existing.valid())
            return existing;
    }
    return graph.importTexture(
        texture,
        TextureResourceDesc(texture->getCreationDescription().name, "Reflection History Image")
            .setInitialState(firstWrite ? Core::ResourceStates::Unknown : Core::ResourceStates::Common)
    );
}

[[nodiscard]] Core::GpuTaskDesc TaskDesc(const Name name, const AStringView label, const Core::GpuTaskId& dependency){
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Tiny;
    scheduling.allowPacketMerge = true;
    scheduling.mergeWithPrevious = true;
    scheduling.allowMergeAcrossConsumerFrontier = true;
    Core::GpuTaskDesc desc;
    desc
        .setIdentity(name)
        .setMarkerLabel(label)
        .setQueue(GraphicsPreferredComputeQueueRequest())
        .setScheduling(scheduling)
        .setDependencies(&dependency, 1u)
    ;
    return desc;
}

struct TemporalTask{
    struct Payload{
        Core::GraphicsRuntime& graphics;
        ReflectionPostprocessSnapshot snapshot;
        ReflectionHistoryReservation reservation;
        u32 width = 0u;
        u32 height = 0u;
        const bool* hardwarePreparationReady = nullptr;
        bool hardwareEnabled = false;
    };

    [[nodiscard]] static bool record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
        if(!payload.snapshot.history.eligible)
            return true;
        const bool ready = payload.hardwareEnabled && payload.hardwarePreparationReady && *payload.hardwarePreparationReady;
        const ReflectionHistoryOutcome outcome = ResolveReflectionHistoryOutcome(payload.snapshot.history, ready);
        // Classification and tracing already produced this bank; the accepted task still publishes its history state.
        if(!outcome.reused || payload.snapshot.history.settings.temporalMaxSamples <= 1u)
            return true;
        commandList.endRenderPass();
        const TemporalParameters parameters{
            payload.width, payload.height, payload.snapshot.current.storageSlot, payload.snapshot.previous.sampledSlot,
            payload.snapshot.opaqueSpecularSlot, outcome.reused ? payload.snapshot.history.previousSampleCount : 0u,
            payload.snapshot.history.settings.temporalMaxSamples, outcome.reused ? 1u : 0u,
            outcome.sampleIndex, payload.snapshot.history.settings.samplingSeed, 0u, 0u,
        };
        Core::ComputeState state;
        state.setPipeline(payload.snapshot.temporalPipeline.get());
        commandList.setComputeState(state);
        payload.graphics.getDevice().getDescriptorHeap().bindCompute(commandList, *payload.snapshot.temporalPipeline.get());
        commandList.setPushConstants(&parameters, sizeof(parameters));
        Core::GpuTimingMeasure timing(payload.graphics.gpuTiming(), ReflectionGpuTimingScope::s_Temporal, payload.graphics.getDevice(), commandList);

        commandList.dispatch(
            (payload.width + NWB_REFLECTION_TEMPORAL_GROUP_SIZE - 1u) / NWB_REFLECTION_TEMPORAL_GROUP_SIZE,
            (payload.height + NWB_REFLECTION_TEMPORAL_GROUP_SIZE - 1u) / NWB_REFLECTION_TEMPORAL_GROUP_SIZE,
            1u
        );
        return !commandList.commandRecordingFailed();
    }

    static void accepted(Payload& payload, const Core::QueueSubmissionToken& token){
        payload.reservation.accept(token, payload.hardwareEnabled && payload.hardwarePreparationReady && *payload.hardwarePreparationReady);
    }

    static void discarded(Payload& payload){
        payload.reservation.discard();
    }
};

struct SpatialTask{
    struct Payload{
        Core::GraphicsRuntime& graphics;
        Core::ComputePipelineHandle pipeline;
        SpatialParameters parameters;
    };

    [[nodiscard]] static bool record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
        commandList.endRenderPass();
        Core::ComputeState state;
        state.setPipeline(payload.pipeline.get());
        commandList.setComputeState(state);
        payload.graphics.getDevice().getDescriptorHeap().bindCompute(commandList, *payload.pipeline.get());
        commandList.setPushConstants(&payload.parameters, sizeof(payload.parameters));
        Core::GpuTimingMeasure timing(payload.graphics.gpuTiming(), ReflectionGpuTimingScope::s_Spatial, payload.graphics.getDevice(), commandList);

        commandList.dispatch(
            (payload.parameters.width + NWB_REFLECTION_SPATIAL_GROUP_SIZE - 1u) / NWB_REFLECTION_SPATIAL_GROUP_SIZE,
            (payload.parameters.height + NWB_REFLECTION_SPATIAL_GROUP_SIZE - 1u) / NWB_REFLECTION_SPATIAL_GROUP_SIZE,
            1u
        );
        return !commandList.commandRecordingFailed();
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Core::GpuTaskId DeclareReflectionPostprocessTasks(
    Core::GpuTaskGraph& graph,
    Core::GraphicsRuntime& graphics,
    Core::Alloc::ScratchArena& scratchArena,
    const ReflectionFrameSnapshot& resources,
    const ReflectionGraphInputs& inputs,
    ReflectionGraphResult& result,
    Core::GpuTaskId dependency){
    using namespace __hidden_reflection_postprocess_tasks;
    const ReflectionPostprocessSnapshot& snapshot = resources.postprocess;
    ReflectionHistoryReservation reservation(snapshot.control, snapshot.history);
    if(!reservation.valid() || (snapshot.history.eligible && !snapshot.temporalPipeline))
        return {};
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> uses{scratchArena};
    uses.reserve(inputs.surfaceReadCount + 2u);
    if(snapshot.history.eligible){
        uses.assign(inputs.surfaceReads, inputs.surfaceReads + inputs.surfaceReadCount);
        uses.push_back(ReadWriteUse(result.opaqueRadiance, Core::ResourceStates::UnorderedAccess));
        if(snapshot.history.reused){
            const Core::GpuGraphResourceId previous = ImportImage(graph, snapshot.previous.texture, false);
            if(!previous.valid())
                return {};
            uses.push_back(ReadUse(previous));
        }
    }
    Core::GpuTaskDesc desc = TaskDesc(Name("render.reflection.temporal"), "Reflection Temporal Accumulation", dependency);
    desc.setResourceUses(uses.data(), uses.size());
    if(snapshot.history.eligible)
        desc.setTimingMetadata(Core::GpuTaskTimingMetadata{.policy = Core::GpuTaskTimingPolicy::PacketOnly});
    dependency = graph.addTask<TemporalTask>(
        desc,
        TemporalTask::Payload{
            graphics, snapshot, Move(reservation), resources.parameters.width, resources.parameters.height,
            inputs.hardwarePreparationReady, resources.parameters.hardwareEnabled != 0u,
        }
    );
    if(!dependency.valid() || !snapshot.spatialEnabled)
        return dependency;
    if(!snapshot.spatialPipeline || !snapshot.spatial.texture)
        return {};
    const Core::GpuGraphResourceId spatial = ImportImage(graph, snapshot.spatial.texture, true);
    if(!spatial.valid())
        return {};
    uses.assign(inputs.surfaceReads, inputs.surfaceReads + inputs.surfaceReadCount);
    uses.push_back(ReadUse(result.opaqueRadiance));
    uses.push_back(WriteUse(spatial, Core::ResourceStates::UnorderedAccess));
    desc = TaskDesc(Name("render.reflection.spatial"), "Reflection Spatial Filter", dependency);
    desc.setResourceUses(uses.data(), uses.size());
    desc.setTimingMetadata(Core::GpuTaskTimingMetadata{.policy = Core::GpuTaskTimingPolicy::PacketOnly});
    dependency = graph.addTask<SpatialTask>(
        desc,
        SpatialTask::Payload{
            graphics, snapshot.spatialPipeline,
            SpatialParameters{
                resources.parameters.width, resources.parameters.height, snapshot.current.sampledSlot, snapshot.spatial.storageSlot,
                snapshot.deferredResourcesSlot, snapshot.opaqueSpecularSlot, snapshot.viewSlot, snapshot.history.settings.spatialRadius,
            },
        }
    );
    if(dependency.valid())
        result.opaqueRadiance = spatial;
    return dependency;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

