// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_reflection.h"
#include "timing_names.h"

#include <core/graphics/vulkan/backend.h>

#include <impl/ecs_render/kernel/task_graph_queue_requests.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>

#include <core/common/log.h>
#include <core/graphics/runtime/runtime.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_reflection_tasks{


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
    // Classify overwrites every pixel. Explicit Unknown preserves native Undefined on a fresh allocation while
    // accepted retained state/handoffs remain authoritative when recording later frames and consumer packets.
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
    desc.setIdentity(identity).setMarkerLabel(label).setQueue(GraphicsPreferredComputeQueueRequest()).setScheduling(scheduling);
    if(dependency.valid())
        desc.setDependencies(&dependency, 1u);
    return desc;
}

struct UploadParametersTask{
    struct Payload{
        Core::BufferHandle buffer;
        ReflectionFrameParameters parameters;
        const bool* hardwarePreparationReady = nullptr;
    };

    [[nodiscard]] static bool record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
        ReflectionFrameParameters parameters = payload.parameters;
        if(!payload.hardwarePreparationReady || !*payload.hardwarePreparationReady)
            parameters.hardwareEnabled = 0u;
        // The immutable frame value is copied into command-list staging here, after the shared scene preparation
        // outcome is known. An unavailable scene cannot enqueue work or bind its stale TLAS generation.
        commandList.writeBuffer(payload.buffer.get(), &parameters, sizeof(parameters));
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

struct DispatchTask{
    struct Payload{
        Core::GraphicsRuntime& graphics;
        ReflectionFrameSnapshot resources;
        DispatchStage::Enum stage;
        const bool* hardwarePreparationReady = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        bool* hardwareDispatchLogged = nullptr;
        bool* fallbackDispatchLogged = nullptr;
    };

    [[nodiscard]] static bool record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
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
            hardware ? resources.scene.tlasHeapHandle : Core::GpuDescriptorHandle::invalid()
        );
        commandList.setPushConstants(&resources.frameParametersSlot, sizeof(resources.frameParametersSlot));

        Optional<Core::GpuTimingSubmissionTicket::RecordingScope> timingRecording;
        Optional<Core::GpuTimingMeasure> timing;
        if(payload.timingTicket){
            timingRecording.emplace(*payload.timingTicket);
            const Core::GpuTimingScopeDefinition* scope = &ReflectionGpuTimingScope::s_BuildArgs;
            if(hardware)
                scope = &ReflectionGpuTimingScope::s_Hardware;
            else if(payload.stage == DispatchStage::Classify)
                scope = &ReflectionGpuTimingScope::s_Classify;
            timing.emplace(payload.graphics.gpuTiming(), *scope, payload.graphics.getDevice(), commandList);
        }
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

    static void accepted(Payload& payload, const Core::QueueSubmissionToken&){
        const bool hardware = payload.resources.parameters.hardwareEnabled != 0u
            && payload.hardwarePreparationReady && *payload.hardwarePreparationReady;
        if(payload.stage == DispatchStage::Hardware && hardware){
            if(payload.hardwareDispatchLogged && !*payload.hardwareDispatchLogged){
                NWB_LOGGER_INFO(NWB_TEXT("Reflection resolve: hardware"));
                *payload.hardwareDispatchLogged = true;
            }
        }
        else if(payload.stage == DispatchStage::Classify && !hardware){
            if(payload.fallbackDispatchLogged && !*payload.fallbackDispatchLogged){
                NWB_LOGGER_INFO(NWB_TEXT("Reflection resolve: {}")
                    , payload.resources.parameters.traceMode == NWB_REFLECTION_MODE_DISABLED ? NWB_TEXT("disabled") : NWB_TEXT("environment")
                );
                *payload.fallbackDispatchLogged = true;
            }
        }
    }
};

struct FinalizeTask{
    struct Payload{};
    [[nodiscard]] static bool record(const Payload&, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
        commandList.endRenderPass();
        return true;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ReflectionGraphResult DeclareReflectionTasks(
    Core::GpuTaskGraph& graph,
    Core::GraphicsRuntime& graphics,
    Core::Alloc::ScratchArena& scratchArena,
    const ReflectionFrameSnapshot& resources,
    const ReflectionGraphInputs& inputs,
    Core::GpuTaskId dependency){
    using namespace __hidden_reflection_tasks;
    if(
        !resources.valid() || !inputs.surfaceReads || inputs.surfaceReadCount == 0u
        || (inputs.hardwareReadCount > 0u && !inputs.hardwareReads)
        || (inputs.hardwareSetReadCount > 0u && !inputs.hardwareSetReads)
    )
        return {};
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
    desc.setQueue(GraphicsUploadQueueRequest());
    const Core::GpuTaskResourceUse parameterWrite = WriteUse(result.frameParameters, Core::ResourceStates::CopyDest);
    desc.setResourceUses(&parameterWrite, 1u);
    dependency = graph.addTask<UploadParametersTask>(
        desc,
        UploadParametersTask::Payload{
            resources.frameParameters, resources.parameters, inputs.hardwarePreparationReady,
        }
    );
    if(!dependency.valid())
        return {};

    desc = TaskDesc(Name("render.reflection.clear_counters"), "Reflection Clear Counters", dependency);
    desc.setQueue(GraphicsUploadQueueRequest());
    dependency = graph.addClearBufferTask(desc, Core::GpuClearBufferTaskDesc{.destination = result.counters, .clearValue = 0u});
    if(!dependency.valid())
        return {};

    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> uses{scratchArena};
    uses.reserve(inputs.surfaceReadCount + inputs.hardwareReadCount + 8u);
    const auto appendSurfaceReads = [&](){
        uses.assign(inputs.surfaceReads, inputs.surfaceReads + inputs.surfaceReadCount);
        uses.push_back(ReadUse(result.frameParameters, Core::ResourceStates::ConstantBuffer));
    };
    const auto appendDispatch = [&](const Name identity, const AStringView label, const DispatchStage::Enum stage){
        Core::GpuTaskDesc dispatchDesc = TaskDesc(identity, label, dependency);
        dispatchDesc.setResourceUses(uses.data(), uses.size());
        if(stage == DispatchStage::Hardware)
            dispatchDesc.setResourceSetUses(inputs.hardwareSetReads, inputs.hardwareSetReadCount);
        dependency = graph.addTask<DispatchTask>(
            dispatchDesc,
            DispatchTask::Payload{
                graphics, resources, stage, inputs.hardwarePreparationReady, inputs.timingTicket,
                inputs.hardwareDispatchLogged, inputs.fallbackDispatchLogged,
            }
        );
        return dependency.valid();
    };
    appendSurfaceReads();
    uses.push_back(WriteUse(result.opaqueRadiance, Core::ResourceStates::UnorderedAccess));
    uses.push_back(WriteUse(result.glassRadiance, Core::ResourceStates::UnorderedAccess));
    uses.push_back(WriteUse(queue, Core::ResourceStates::UnorderedAccess));
    uses.push_back(ReadWriteUse(result.counters, Core::ResourceStates::UnorderedAccess));
    if(!appendDispatch(Name("render.reflection.classify"), "Reflection Classify", DispatchStage::Classify))
        return {};

    uses.clear();
    uses.push_back(ReadUse(result.frameParameters, Core::ResourceStates::ConstantBuffer));
    uses.push_back(ReadUse(result.counters));
    uses.push_back(WriteUse(args, Core::ResourceStates::UnorderedAccess));
    if(!appendDispatch(Name("render.reflection.build_args"), "Reflection Build Arguments", DispatchStage::BuildArgs))
        return {};

    if(resources.parameters.hardwareEnabled != 0u){
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
        if(!appendDispatch(Name("render.reflection.hardware"), "Reflection Hardware Resolve", DispatchStage::Hardware))
            return {};
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

