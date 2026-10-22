// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "avboit_integration_probe.h"

#include <impl/assets_graphics/avboit/names.h>
#include <impl/assets_shader/loader.h>
#include <impl/ecs_render/avboit/avboit.h>

#include <core/common/log.h>
#include <core/graphics/backend_selection/backend.h>
#include <core/graphics/shader_archive.h>
#include <core/task/gpu/task_graph.h>

#include <global/simd/convert_half.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_avboit_integration_probe{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;

inline constexpr u32 s_Width = 7u;
inline constexpr u32 s_Height = 3u;
inline constexpr u32 s_PixelCount = s_Width * s_Height;
inline constexpr u32 s_SliceCount = 17u;
inline constexpr u32 s_WordCount = DivideUp(s_SliceCount, NWB_AVBOIT_EXTINCTION_SLICES_PER_WORD) * s_PixelCount;
inline constexpr u32 s_PhaseCount = 5u;
inline constexpr u32 s_FixedScale = 16u;
inline constexpr Name s_ProbeName("AvboitIntegrationProbe");

struct ProbeTask{
    struct Payload{
        Core::BufferHandle extinction;
        Core::BufferHandle overflow;
        Core::BufferHandle control;
        Core::BufferHandle slots;
        Core::TextureHandle volume;
        Core::StagingTextureHandle readback;
        Core::ComputePipelineHandle pipeline;
        Core::GpuDescriptorHeap& heap;
        TStringView& stage;
        Core::ResourceStates::Mask volumeInitialState = Core::ResourceStates::Unknown;
        Array<u32, s_WordCount> words{};
        Array<u32, s_PixelCount> overflowSlices{};
        Array<u32, NWB_AVBOIT_CONTROL_WORD_COUNT> controlWords{};
        Impl::RendererAvboitPushConstants push{};
    };


public:
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements{ .requiredCapabilities = Core::GpuQueueCapability::Compute | Core::GpuQueueCapability::Transfer, .requiresPrimaryGraphicsQueue = true, };


public:
    [[nodiscard]] static bool Record(const Payload& payload, Core::CommandList& commands, const Core::GpuTaskRecordContext& context){
        payload.stage = NWB_TEXT("recording entry");
        if(context.commandIrCapture)
            return false;
        payload.stage = NWB_TEXT("buffer uploads");
        if(
            !commands.tryWriteBuffer(*payload.extinction, payload.words.data(), sizeof(payload.words))
            || !commands.tryWriteBuffer(*payload.overflow, payload.overflowSlices.data(), sizeof(payload.overflowSlices))
            || !commands.tryWriteBuffer(*payload.control, payload.controlWords.data(), sizeof(payload.controlWords))
        )
            return false;
        // Reused storage must return to neutral even when the next dispatch skips every XY cell.
        payload.stage = NWB_TEXT("volume clear and integration dispatch");
        commands.clearTextureFloat(*payload.volume, Core::TextureSubresourceSet{}, Core::Color(1.f, 1.f, 1.f, 1.f));
        commands.setBufferState(payload.extinction.get(), Core::ResourceStates::ShaderResource);
        commands.setBufferState(payload.overflow.get(), Core::ResourceStates::ShaderResource);
        commands.setBufferState(payload.control.get(), Core::ResourceStates::ShaderResource);
        commands.setTextureState(payload.volume.get(), Core::TextureSubresourceSet{}, Core::ResourceStates::UnorderedAccess);
        commands.commitBarriers();
        Core::ComputeState state;
        state.setPipeline(payload.pipeline.get());
        commands.setComputeState(state);
        payload.heap.bindCompute(commands, *payload.pipeline);
        commands.setPushConstants(&payload.push, sizeof(payload.push));
        commands.dispatch(DivideUp(s_PixelCount, static_cast<u32>(NWB_AVBOIT_INTEGRATE_GROUP_SIZE_X)), 1u, 1u);
        payload.stage = NWB_TEXT("volume copy and state restoration");
        commands.setTextureState(payload.volume.get(), Core::TextureSubresourceSet{}, Core::ResourceStates::CopySource);
        commands.commitBarriers();
        commands.copyTexture(*payload.readback, Core::TextureSlice{}, *payload.volume, Core::TextureSlice{});
        commands.setBufferState(payload.extinction.get(), Core::ResourceStates::CopyDest);
        commands.setBufferState(payload.overflow.get(), Core::ResourceStates::CopyDest);
        commands.setBufferState(payload.control.get(), Core::ResourceStates::CopyDest);
        commands.setTextureState(payload.volume.get(), Core::TextureSubresourceSet{}, Core::ResourceStates::CopyDest);
        commands.commitBarriers();
        if(commands.commandRecordingFailed())
            return false;
        payload.stage = NWB_TEXT("native close and submission");
        return true;
    }
};

[[nodiscard]] static Expected<Core::GpuTaskId> DeclareProbe(void* userData, Core::GpuTaskGraph& graph){
    const auto& payload = *static_cast<const ProbeTask::Payload*>(userData);
    payload.stage = NWB_TEXT("buffer imports");
    const auto import = [&graph](const Core::BufferHandle& buffer, const AStringView label, const Core::ResourceStates::Mask state){
        return graph.importBuffer(buffer, Core::GpuGraphResourceDesc{}.setIdentity(buffer->getCreationDescription().debugName).setMarkerLabel(label).setType(Core::GpuGraphResourceType::Buffer).setInitialState(state).setExternalFinalState(state));
    };
    const Array<Expected<Core::GpuGraphResourceId>, 4u> buffers{
        import(payload.extinction, "Integration probe extinction", Core::ResourceStates::CopyDest),
        import(payload.overflow, "Integration probe overflow", Core::ResourceStates::CopyDest),
        import(payload.control, "Integration probe control", Core::ResourceStates::CopyDest),
        import(payload.slots, "Integration probe heap slots", Core::ResourceStates::ConstantBuffer),
    };
    for(const auto& buffer : buffers){
        if(!buffer)
            return MakeUnexpected(Failure{});
    }
    payload.stage = NWB_TEXT("volume and staging imports");
    const auto volume = graph.importTexture(payload.volume, Core::GpuGraphResourceDesc{}.setIdentity(s_ProbeName).setMarkerLabel("Integration probe transmittance volume").setType(Core::GpuGraphResourceType::Texture).setInitialState(payload.volumeInitialState).setExternalFinalState(Core::ResourceStates::CopyDest));
    const auto readback = graph.importHazardDomain(Core::GpuGraphResourceDesc{}.setIdentity(Name("AvboitIntegrationReadback")).setMarkerLabel("Integration probe staging writes").setType(Core::GpuGraphResourceType::HazardDomain));
    if(!volume || !readback)
        return MakeUnexpected(Failure{});
    const Core::GpuTaskResourceUse uses[]{
        { .resource = *buffers[0], .range = {}, .requiredState = Core::ResourceStates::CopyDest, .access = Core::GpuTaskResourceAccess::ReadWrite },
        { .resource = *buffers[1], .range = {}, .requiredState = Core::ResourceStates::CopyDest, .access = Core::GpuTaskResourceAccess::ReadWrite },
        { .resource = *buffers[2], .range = {}, .requiredState = Core::ResourceStates::CopyDest, .access = Core::GpuTaskResourceAccess::ReadWrite },
        { .resource = *buffers[3], .range = {}, .requiredState = Core::ResourceStates::ConstantBuffer, .access = Core::GpuTaskResourceAccess::Read },
        { .resource = *volume, .range = {}, .requiredState = Core::ResourceStates::CopyDest, .access = Core::GpuTaskResourceAccess::Write },
        { .resource = *readback, .range = {}, .requiredState = Core::ResourceStates::Unknown, .access = Core::GpuTaskResourceAccess::Write },
    };
    payload.stage = NWB_TEXT("task declaration");
    const auto task = graph.addTask<ProbeTask>(Core::GpuTaskDesc{}.setIdentity(s_ProbeName).setMarkerLabel("AVBOIT sparse integration boundary probe").setResourceUses(uses, LengthOf(uses)), ProbeTask::Payload(payload));
    if(task)
        payload.stage = NWB_TEXT("scheduling or initial-state recording admission");
    return task;
}

static void SetExtinction(ProbeTask::Payload& payload, const u32 pixel, const u32 slice, const u32 value)noexcept{
    payload.words[(slice / NWB_AVBOIT_EXTINCTION_SLICES_PER_WORD) * s_PixelCount + pixel] |= value << ((slice % NWB_AVBOIT_EXTINCTION_SLICES_PER_WORD) * NWB_AVBOIT_EXTINCTION_BYTE_BITS);
}

static void PreparePhase(ProbeTask::Payload& payload, const u32 phase)noexcept{
    // A fresh image has no established layout; completed phases restore CopyDest before reuse.
    payload.volumeInitialState = phase == 0u ? Core::ResourceStates::Unknown : Core::ResourceStates::CopyDest;
    payload.words.fill(0u);
    payload.overflowSlices.fill(NWB_AVBOIT_OVERFLOW_INVALID);
    const u32 active = phase == 3u ? 5u : phase == 4u ? 1u : s_SliceCount;
    payload.controlWords[NWB_AVBOIT_CONTROL_ACTIVE_PHYSICAL_SLICES] = active;
    for(u32 pixel = 0u; pixel < s_PixelCount; ++pixel){
        if(phase == 0u){
            SetExtinction(payload, pixel, 0u, 255u);
            SetExtinction(payload, pixel, 1u, 255u);
            continue;
        }
        if(phase == 1u)
            continue;
        switch(pixel % 7u){
        case 0u: break;
        case 1u: SetExtinction(payload, pixel, active > 12u ? 12u : active - 1u, 16u); break;
        case 2u: SetExtinction(payload, pixel, 0u, 255u); SetExtinction(payload, pixel, 1u, 255u); break;
        case 3u: payload.overflowSlices[pixel] = 0u; break;
        case 4u: payload.overflowSlices[pixel] = active - 1u; break;
        case 5u: SetExtinction(payload, pixel, Min(DivideUp(active, 4u) * 4u - 1u, s_SliceCount - 1u), 255u); break;
        case 6u: SetExtinction(payload, pixel, active - 1u, 16u); break;
        }
    }
}

[[nodiscard]] static bool CompareVolume(Core::Device& device, const ProbeTask::Payload& payload, const u32 phase){
    Array<u32, s_PixelCount> extinctionFixed{};
    for(u32 slice = 0u; slice < s_SliceCount; ++slice){
        const auto mapping = device.mapStagingTexture(*payload.readback, Core::TextureSlice{}.setOrigin(0u, 0u, slice).setSize(s_Width, s_Height, 1u), Core::CpuAccessMode::Read);
        if(!mapping)
            return false;
        ScopeExit unmap([&]()noexcept{ device.unmapStagingTexture(*payload.readback); });
        if(!mapping->data || mapping->rowPitch < s_Width * sizeof(Half))
            return false;
        for(u32 pixel = 0u; pixel < s_PixelCount; ++pixel){
            Half observed;
            NWB_MEMCPY(&observed, sizeof(observed), static_cast<const u8*>(mapping->data) + (pixel / s_Width) * mapping->rowPitch + (pixel % s_Width) * sizeof(Half), sizeof(observed));
            const f32 expected = ConvertHalfToFloat(ConvertFloatToHalf(Exp(-static_cast<f32>(extinctionFixed[pixel]) / static_cast<f32>(s_FixedScale))));
            const f32 actual = ConvertHalfToFloat(observed);
            if(!IsFinite(actual) || (extinctionFixed[pixel] == 0u ? actual != 1.f : Abs(actual - expected) > Max(0.0000002f, expected * 0.002f))){
                NWB_LOGGER_ERROR(NWB_TEXT("AvboitIntegrationProbe: mismatch phase {} pixel {} slice {} expected {} observed {}"), phase, pixel, slice, expected, actual);
                return false;
            }
            if(slice < payload.controlWords[NWB_AVBOIT_CONTROL_ACTIVE_PHYSICAL_SLICES]){
                if(slice >= payload.overflowSlices[pixel])
                    extinctionFixed[pixel] = 16u * s_FixedScale;
                else{
                    const u32 packed = payload.words[(slice / 4u) * s_PixelCount + pixel];
                    extinctionFixed[pixel] = Min(extinctionFixed[pixel] + ((packed >> ((slice % 4u) * 8u)) & 255u), 16u * s_FixedScale);
                }
            }
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RunAvboitIntegrationProbe(NWB::ProjectRuntimeContext& context){
    using namespace NWB;
    using namespace __hidden_avboit_integration_probe;
    auto& graphics = context.graphics;
    auto& device = graphics.getDevice();
    auto& heap = device.getDescriptorHeap();
    Core::ShaderHandle shader;
    if(!Impl::ShaderAssetLoader::Load<Impl::ComputeShader>(shader, Impl::AssetsGraphicsAvboit::s_IntegrateComputeShaderName, Core::ShaderArchive::s_DefaultVariant, s_ProbeName, graphics, context.assetManager, context.shaderPathResolver, NWB_TEXT("AvboitIntegrationProbe")))
        return false;
    Core::BindingLayoutDesc layoutDesc(context.objectArena);
    layoutDesc.setVisibility(Core::ShaderType::Compute).addItem(Core::BindingLayoutItem::PushConstants(0u, NWB_AVBOIT_PUSH_CONSTANT_BYTE_SIZE));
    const auto layout = device.createBindingLayout(layoutDesc);
    if(!layout)
        return false;
    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc.setComputeShader(shader).addBindingLayout(*layout).addBindingLayout(heap.getResourceLayout()).addBindingLayout(heap.getSamplerLayout());
    const auto pipeline = device.createComputePipeline(pipelineDesc);
    if(!pipeline)
        return false;
    const auto createBuffer = [&graphics](const Name name, const u32 byteSize, const bool constant){
        Core::BufferDesc desc;
        desc.setDebugName(name).setByteSize(byteSize).setStructStride(sizeof(u32)).setCanHaveUAVs(!constant).setIsConstantBuffer(constant).enableAutomaticStateTracking(constant ? Core::ResourceStates::ConstantBuffer : Core::ResourceStates::CopyDest);
        return graphics.createBuffer(desc);
    };
    const auto extinction = createBuffer(Name("AvboitIntegrationExtinction"), s_WordCount * sizeof(u32), false);
    const auto overflow = createBuffer(Name("AvboitIntegrationOverflow"), s_PixelCount * sizeof(u32), false);
    const auto control = createBuffer(Name("AvboitIntegrationControl"), NWB_AVBOIT_CONTROL_WORD_COUNT * sizeof(u32), false);
    Core::TextureDesc textureDesc;
    textureDesc.setWidth(s_Width).setHeight(s_Height).setDepth(s_SliceCount).setDimension(Core::TextureDimension::Texture3D).setFormat(NWB_AVBOIT_TRANSMITTANCE_CORE_FORMAT).setInUAV(true).setName(s_ProbeName).setInitialState(Core::ResourceStates::CopyDest).setKeepInitialState(true);
    const auto volume = graphics.createTexture(textureDesc);
    const auto readback = device.createStagingTexture(textureDesc, Core::CpuAccessMode::Read);
    if(!extinction || !overflow || !control || !volume || !readback)
        return false;
    Core::BufferHandle slotBuffer;
    Array<Core::GpuDescriptorHandle, 5u> descriptors{};
    Core::GpuDescriptorRetirementGuard retire(heap, Span<const Core::GpuDescriptorHandle>(descriptors.data(), descriptors.size()));
    const Array<Core::BufferHandle, 3u> buffers{ *extinction, *overflow, *control };
    for(u32 index = 0u; index < buffers.size(); ++index){
        const auto descriptor = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
        if(!descriptor)
            return false;
        descriptors[index] = *descriptor;
        if(!heap.write(*descriptor, Core::DescriptorWriteItem::StructuredBufferUav(0u, buffers[index].get())))
            return false;
    }
    const auto storage = heap.allocate(Core::GpuDescriptorClass::StorageImage);
    if(!storage)
        return false;
    descriptors[3] = *storage;
    if(!heap.write(*storage, Core::DescriptorWriteItem::TextureUav(0u, volume->get(), textureDesc.format, Core::TextureSubresourceSet{}, Core::TextureDimension::Texture3D)))
        return false;
    Impl::DeferredBindlessResourceSlots slots;
    slots.avboitExtinction = descriptors[0].slot();
    slots.avboitExtinctionOverflow = descriptors[1].slot();
    slots.avboitControl = descriptors[2].slot();
    slots.avboitTransmittanceStorage = descriptors[3].slot();
    Core::GraphicsRuntime::BufferSetupDesc setup;
    setup.bufferDesc.setDebugName(Name("AvboitIntegrationSlots")).setByteSize(sizeof(slots)).setIsConstantBuffer(true).enableAutomaticStateTracking(Core::ResourceStates::ConstantBuffer);
    setup.data = &slots;
    setup.dataSize = sizeof(slots);
    setup.queue = Core::CommandQueue::Graphics;
    const auto slotBufferResult = graphics.setupBuffer(setup);
    if(!slotBufferResult)
        return false;
    slotBuffer = *slotBufferResult;
    const auto uniform = heap.allocate(Core::GpuDescriptorClass::UniformBuffer);
    if(!uniform)
        return false;
    descriptors[4] = *uniform;
    if(!heap.write(*uniform, Core::DescriptorWriteItem::ConstantBuffer(0u, slotBuffer.get())))
        return false;
    TStringView stage = NWB_TEXT("phase preparation");
    ProbeTask::Payload payload{ *extinction, *overflow, *control, slotBuffer, *volume, *readback, *pipeline, heap, stage };
    payload.push.frame[0] = s_Width * 4u;
    payload.push.frame[1] = s_Height * 4u;
    payload.push.frame[2] = s_Width;
    payload.push.frame[3] = s_Height;
    payload.push.volume[1] = s_SliceCount;
    payload.push.volume[2] = s_WordCount;
    payload.push.params.raw[NWB_AVBOIT_PUSH_PARAMS_EXTINCTION_FIXED_SCALE] = static_cast<f32>(s_FixedScale);
    payload.push.heapSlots[0] = uniform->slot();
    for(u32 phase = 0u; phase < s_PhaseCount; ++phase){
        PreparePhase(payload, phase);
        const auto token = graphics.submitStandaloneTaskGraph(&payload, DeclareProbe);
        if(!token){
            NWB_LOGGER_ERROR(NWB_TEXT("AvboitIntegrationProbe: phase {} standalone graph rejected at {}"), phase, stage);
            return false;
        }
        if(!device.waitForSubmissionToken(*token)){
            NWB_LOGGER_ERROR(NWB_TEXT("AvboitIntegrationProbe: phase {} submission completion wait failed"), phase);
            return false;
        }
        if(!CompareVolume(device, payload, phase)){
            NWB_LOGGER_ERROR(NWB_TEXT("AvboitIntegrationProbe: phase {} volume readback comparison failed"), phase);
            return false;
        }
    }
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("AvboitIntegrationProbe: passed 5 phases with empty, late, partial, saturation, wrapped overflow and active-to-empty clear checks"));
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

