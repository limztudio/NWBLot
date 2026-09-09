// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_reflection.h"
#include "timing_names.h"

#include <core/graphics/vulkan/backend.h>

#include <impl/ecs_render/kernel/task_graph_queue_requests.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>

#include <core/common/log.h>
#include <core/graphics/runtime/runtime.h>

#include <global/basic_string.h>


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
    // Reflection producers overwrite every used subresource. Explicit Unknown preserves a fresh native Undefined while
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

struct DepthReduceParameters{
#define NWB_REFLECTION_DEPTH_CPU_FIELD(name) u32 name = 0u;
    NWB_REFLECTION_DEPTH_UINT_FIELDS(NWB_REFLECTION_DEPTH_CPU_FIELD)
#undef NWB_REFLECTION_DEPTH_CPU_FIELD
};
static_assert(sizeof(DepthReduceParameters) == NWB_REFLECTION_DEPTH_PUSH_CONSTANT_BYTES);

struct DepthReduceTask{
    struct Payload{
        Core::GraphicsRuntime& graphics;
        ReflectionDepthPyramidSnapshot resources;
        DepthReduceParameters parameters;
    };

    [[nodiscard]] static bool record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
        if(!payload.resources.valid())
            return false;
        commandList.endRenderPass();
        Core::ComputeState state;
        state.setPipeline(payload.resources.pipeline.get());
        commandList.setComputeState(state);
        payload.graphics.getDevice().getDescriptorHeap().bindCompute(commandList, *payload.resources.pipeline.get());
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

struct DispatchTask{
    struct Payload{
        Core::GraphicsRuntime& graphics;
        ReflectionFrameSnapshot resources;
        DispatchStage::Enum stage;
        const bool* hardwarePreparationReady = nullptr;
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
                const u32 mode = payload.resources.parameters.traceMode;
                const bool screen = mode == NWB_REFLECTION_MODE_SCREEN || mode == NWB_REFLECTION_MODE_HYBRID;
                const tchar* route = screen ? NWB_TEXT("screen-space") : NWB_TEXT("environment");
                if(mode == NWB_REFLECTION_MODE_DISABLED)
                    route = NWB_TEXT("disabled");
                NWB_LOGGER_INFO(NWB_TEXT("Reflection resolve: {}"), route);
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
    const bool screen = resources.parameters.traceMode == NWB_REFLECTION_MODE_SCREEN
        || resources.parameters.traceMode == NWB_REFLECTION_MODE_HYBRID;
    if(screen && (!inputs.opaqueDepth.valid() || !inputs.opaqueColor.valid()))
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
            const auto identity = StringFormat(scratchArena, "render.reflection.depth_reduce_{}", mipIndex);
            Core::GpuTaskDesc depthDesc = TaskDesc(ToName(identity), "Reflection Depth Reduce", dependency);
            depthDesc.setResourceUses(depthUses, LengthOf(depthUses));
            depthDesc.setTimingMetadata(Core::GpuTaskTimingMetadata{.policy = Core::GpuTaskTimingPolicy::PacketOnly});
            dependency = graph.addTask<DepthReduceTask>(
                depthDesc, DepthReduceTask::Payload{graphics, pyramid, parameters}
            );
            if(!dependency.valid())
                return {};
        }
    }

    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> uses{scratchArena};
    uses.reserve(inputs.surfaceReadCount + inputs.hardwareReadCount + 10u);
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
        dependency = graph.addTask<DispatchTask>(
            dispatchDesc,
            DispatchTask::Payload{
                graphics, resources, stage, inputs.hardwarePreparationReady,
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

