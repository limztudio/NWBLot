// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "assets/shaders/refraction_tile_probe_layout.h"

#include <tests/smoke/scene_types.adhd.h>
#include <impl/assets_scene/asset.h>
#include <impl/assets_graphics/refractive_subsystem.adhd.h>
#include <impl/assets/graphics/refraction/push_constants.h>
#include <impl/ecs_render/raytrace/types.adhd.h>
#include <global/simd/convert_half.h>
#include <global/simd/vector.h>

#include <tests/smoke/presentation_types.adhd.h>

#include <loader/project_entry.h>

#include <impl/assets_graphics/transparent_subsystem.adhd.h>
#include <impl/assets_graphics/components.adhd.h>
#include <impl/assets_shader/loader.h>
#include <impl/ecs_render/avboit/avboit.h>

#include <core/common/log.h>
#include <core/graphics/backend_selection/backend.h>
#include <core/graphics/runtime/runtime.h>
#include <core/graphics/shader_archive.h>
#include <core/task/gpu/task_graph.h>


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
inline constexpr Name s_ProbeName = NWB::Tests::Smoke::s_SmokeNameAvboitIntegrationProbe;

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
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commands, const Core::GpuTaskRecordContext& context){
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
    const auto& payload = *MakeNotNull(static_cast<const ProbeTask::Payload*>(userData));
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
    const auto volume = graph.importTexture(
        payload.volume,
        Core::GpuGraphResourceDesc{}
            .setIdentity(s_ProbeName)
            .setMarkerLabel("Integration probe transmittance volume")
            .setType(Core::GpuGraphResourceType::Texture)
            .setInitialState(payload.volumeInitialState)
            .setExternalFinalState(Core::ResourceStates::CopyDest)
    );
    const auto readback = graph.importHazardDomain(Core::GpuGraphResourceDesc{}.setIdentity(Name(NWB::Tests::Smoke::s_SmokeNameAvboitIntegrationReadback)).setMarkerLabel("Integration probe staging writes").setType(Core::GpuGraphResourceType::HazardDomain));
    if(!volume || !readback)
        return MakeUnexpected(Failure{});
    const Array<Core::GpuTaskResourceUse, 6> uses = {{
        { .resource = *buffers[0], .range = {}, .requiredState = Core::ResourceStates::CopyDest, .access = Core::GpuTaskResourceAccess::ReadWrite },
        { .resource = *buffers[1], .range = {}, .requiredState = Core::ResourceStates::CopyDest, .access = Core::GpuTaskResourceAccess::ReadWrite },
        { .resource = *buffers[2], .range = {}, .requiredState = Core::ResourceStates::CopyDest, .access = Core::GpuTaskResourceAccess::ReadWrite },
        { .resource = *buffers[3], .range = {}, .requiredState = Core::ResourceStates::ConstantBuffer, .access = Core::GpuTaskResourceAccess::Read },
        { .resource = *volume, .range = {}, .requiredState = Core::ResourceStates::CopyDest, .access = Core::GpuTaskResourceAccess::Write },
        { .resource = *readback, .range = {}, .requiredState = Core::ResourceStates::Unknown, .access = Core::GpuTaskResourceAccess::Write },
    }};
    payload.stage = NWB_TEXT("task declaration");
    const auto task = graph.addTask<ProbeTask>(Core::GpuTaskDesc{}.setIdentity(s_ProbeName).setMarkerLabel("AVBOIT sparse integration boundary probe").setResourceUses(uses), ProbeTask::Payload(payload));
    if(task)
        payload.stage = NWB_TEXT("scheduling or initial-state recording admission");
    return task;
}

static void SetExtinction(const u32 pixel, const u32 slice, const u32 value, ProbeTask::Payload& payload)noexcept{
    payload.words[(slice / NWB_AVBOIT_EXTINCTION_SLICES_PER_WORD) * s_PixelCount + pixel] |= value << ((slice % NWB_AVBOIT_EXTINCTION_SLICES_PER_WORD) * NWB_AVBOIT_EXTINCTION_BYTE_BITS);
}

static void PreparePhase(const u32 phase, ProbeTask::Payload& payload)noexcept{
    // Completed phases restore CopyDest before reusing the image.
    payload.volumeInitialState = phase == 0u ? Core::ResourceStates::Unknown : Core::ResourceStates::CopyDest;
    payload.words.fill(0u);
    payload.overflowSlices.fill(NWB_AVBOIT_OVERFLOW_INVALID);
    const u32 active = phase == 3u ? 5u : phase == 4u ? 1u : s_SliceCount;
    payload.controlWords[NWB_AVBOIT_CONTROL_ACTIVE_PHYSICAL_SLICES] = active;
    for(u32 pixel = 0u; pixel < s_PixelCount; ++pixel){
        if(phase == 0u){
            SetExtinction(pixel, 0u, 255u, payload);
            SetExtinction(pixel, 1u, 255u, payload);
            continue;
        }
        if(phase == 1u)
            continue;
        switch(pixel % 7u){
        case 0u: break;
        case 1u: SetExtinction(pixel, active > 12u ? 12u : active - 1u, 16u, payload); break;
        case 2u: SetExtinction(pixel, 0u, 255u, payload); SetExtinction(pixel, 1u, 255u, payload); break;
        case 3u: payload.overflowSlices[pixel] = 0u; break;
        case 4u: payload.overflowSlices[pixel] = active - 1u; break;
        case 5u: SetExtinction(pixel, Min(DivideUp(active, 4u) * 4u - 1u, s_SliceCount - 1u), 255u, payload); break;
        case 6u: SetExtinction(pixel, active - 1u, 16u, payload); break;
        }
    }
}

[[nodiscard]] static b8 CompareVolume(const ProbeTask::Payload& payload, const u32 phase, Core::Device& device){
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
            const f32 exponent = -static_cast<f32>(extinctionFixed[pixel]) / static_cast<f32>(s_FixedScale);
            const f32 expected = ConvertHalfToFloat(ConvertFloatToHalf(Exp(exponent)));
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


b8 RunAvboitIntegrationProbe(const NWB::Impl::ActiveSceneSubsystems& sceneSubsystems, NWB::ProjectRuntimeContext& context){
    using namespace NWB;
    using namespace __hidden_avboit_integration_probe;
    auto& graphics = context.graphics;
    auto& device = graphics.getDevice();
    auto& heap = device.getDescriptorHeap();
    Core::ShaderHandle shader;
    const Impl::TransparentSubsystem* const transparentSubsystem = sceneSubsystems.findSubsystem<Impl::TransparentSubsystem>();
    const auto integrateShaderRef = transparentSubsystem ? transparentSubsystem->avboitIntegrateCs : Core::Assets::AssetRef<Impl::ComputeShader>{};
    if(
        !integrateShaderRef.valid()
        || !Impl::ShaderAssetLoader::Load<Impl::ComputeShader>(integrateShaderRef.name(), Core::ShaderArchive::s_DefaultVariant, s_ProbeName, NWB_TEXT("AvboitIntegrationProbe"), graphics, context.assetManager, context.shaderPathResolver, nullptr, shader)
    )
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
    const auto createBuffer = [&graphics](const Name name, const u32 byteSize, const b8 constant){
        Core::BufferDesc desc;
        desc.setDebugName(name).setByteSize(byteSize).setStructStride(sizeof(u32)).setCanHaveUAVs(!constant).setIsConstantBuffer(constant).enableAutomaticStateTracking(constant ? Core::ResourceStates::ConstantBuffer : Core::ResourceStates::CopyDest);
        return graphics.createBuffer(desc);
    };
    const auto extinction = createBuffer(Name(NWB::Tests::Smoke::s_SmokeNameAvboitIntegrationExtinction), s_WordCount * sizeof(u32), false);
    const auto overflow = createBuffer(Name(NWB::Tests::Smoke::s_SmokeNameAvboitIntegrationOverflow), s_PixelCount * sizeof(u32), false);
    const auto control = createBuffer(Name(NWB::Tests::Smoke::s_SmokeNameAvboitIntegrationControl), NWB_AVBOIT_CONTROL_WORD_COUNT * sizeof(u32), false);
    Core::TextureDesc textureDesc;
    textureDesc.setWidth(s_Width)
        .setHeight(s_Height)
        .setDepth(s_SliceCount)
        .setDimension(Core::TextureDimension::Texture3D)
        .setFormat(NWB_AVBOIT_TRANSMITTANCE_CORE_FORMAT)
        .setIsUAV(true)
        .setName(s_ProbeName)
        .setInitialState(Core::ResourceStates::CopyDest)
        .setKeepInitialState(true)
    ;
    const auto volume = graphics.createTexture(textureDesc);
    const auto readback = device.createStagingTexture(textureDesc, Core::CpuAccessMode::Read);
    if(!extinction || !overflow || !control || !volume || !readback)
        return false;
    Core::BufferHandle slotBuffer;
    Array<Core::GpuDescriptorHandle, 5u> descriptors{};
    Core::GpuDescriptorRetirementGuard retire(heap, descriptors);
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
    Core::GraphicsStandaloneService::BufferSetupDesc setup;
    setup.bufferDesc.setDebugName(Name(NWB::Tests::Smoke::s_SmokeNameAvboitIntegrationSlots)).setByteSize(sizeof(slots)).setIsConstantBuffer(true).enableAutomaticStateTracking(Core::ResourceStates::ConstantBuffer);
    setup.data = &slots;
    setup.dataSize = sizeof(slots);
    setup.queue = Core::CommandQueue::Graphics;
    const auto slotBufferResult = graphics.standaloneGraph().setupBuffer(setup);
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
        PreparePhase(phase, payload);
        const auto token = graphics.standaloneGraph().submitStandaloneTaskGraph(&payload, DeclareProbe);
        if(!token){
            NWB_LOGGER_ERROR(NWB_TEXT("AvboitIntegrationProbe: phase {} standalone graph rejected at {}"), phase, stage);
            return false;
        }
        if(!device.waitForSubmissionToken(*token)){
            NWB_LOGGER_ERROR(NWB_TEXT("AvboitIntegrationProbe: phase {} submission completion wait failed"), phase);
            return false;
        }
        if(!CompareVolume(payload, phase, device)){
            NWB_LOGGER_ERROR(NWB_TEXT("AvboitIntegrationProbe: phase {} volume readback comparison failed"), phase);
            return false;
        }
    }
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("AvboitIntegrationProbe: passed 5 phases with empty, late, partial, saturation, wrapped overflow and active-to-empty clear checks"));
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_refraction_tile_probe{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;

inline constexpr u32 s_Width = 2049u;
inline constexpr u32 s_Height = 2049u;
inline constexpr u32 s_TilesX = DivideUp(s_Width, static_cast<u32>(NWB_REFRACTION_GROUP_SIZE));
inline constexpr u32 s_TilesY = DivideUp(s_Height, static_cast<u32>(NWB_REFRACTION_GROUP_SIZE));
inline constexpr u32 s_TileCount = s_TilesX * s_TilesY;
inline constexpr u32 s_PixelCount = s_Width * s_Height;
inline constexpr u32 s_ArgumentWords = NWB_REFRACTION_TILE_ARGUMENT_BYTES / sizeof(u32) + NWB_SMOKE_REFRACTION_PROBE_GUARD_WORDS;
inline constexpr u32 s_QueueWords = s_TileCount + NWB_SMOKE_REFRACTION_PROBE_GUARD_WORDS;
inline constexpr u32 s_VisitWords = NWB_SMOKE_REFRACTION_PROBE_HEADER_WORDS + s_TileCount + s_PixelCount + NWB_SMOKE_REFRACTION_PROBE_GUARD_WORDS;
inline constexpr u32 s_ReadbackWords = s_ArgumentWords + s_QueueWords + s_VisitWords;
inline constexpr u32 s_Sentinel = 0xa573d91bu;
inline constexpr u32 s_PhaseCount = 6u;
inline constexpr u32 s_MissingTile = (s_TilesY / 2u) * s_TilesX + s_TilesX / 2u;
inline constexpr Array<u32, 6u> s_SparseTiles{ 0u, s_TilesX - 1u, (s_TilesY - 1u) * s_TilesX, s_TileCount - 1u, 65534u, 65535u };
inline constexpr Name s_ProbeName = NWB::Tests::Smoke::s_SmokeNameRefractionTileProbe;
inline constexpr Name s_VerifyShaderName = NWB::Tests::Smoke::s_SmokeNameRefractionTileProbeCs;

using ProbePushConstants = Impl::RefractionPushConstants;

static_assert(sizeof(ProbePushConstants) == 11u * sizeof(u32));
static_assert(s_TileCount == 66049u && s_TileCount > NWB_REFRACTION_MAX_GROUP_COUNT_X);
static_assert(NWB_REFRACTION_TILE_ARGUMENT_BYTES == 32u && NWB_REFRACTION_TILE_COUNTER_OFFSET == 12u && NWB_REFRACTION_DENSE_ARGUMENT_OFFSET == 16u);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ProbeTask{
    struct Payload{
        Core::TextureHandle depth;
        Core::BufferHandle queue;
        Core::BufferHandle reversedQueue;
        Core::BufferHandle arguments;
        Core::BufferHandle visits;
        Core::BufferHandle slots;
        Core::BufferHandle readback;
        Core::ComputePipelineHandle classify;
        Core::ComputePipelineHandle finalize;
        Core::ComputePipelineHandle verifyCompact;
        Core::ComputePipelineHandle verifyDense;
        Core::GpuDescriptorHeap& heap;
        TStringView& stage;
        ProbePushConstants push;
        u32 denseTileListSlot = 0u;
        u32 phase = 0u;
    };


public:
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements{ .requiredCapabilities = Core::GpuQueueCapability::Compute | Core::GpuQueueCapability::Transfer, .requiresPrimaryGraphicsQueue = true, };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commands, const Core::GpuTaskRecordContext& context){
        payload.stage = NWB_TEXT("resource initialization");
        if(context.commandIrCapture)
            return false;
        commands.clearTextureFloat(*payload.depth, Core::TextureSubresourceSet{}, Core::Color(payload.phase == 0u || payload.phase == 5u ? 0.5f : 1.f));
        commands.setTextureState(payload.depth.get(), Core::TextureSubresourceSet{}, Core::ResourceStates::CopyDest, true);
        commands.commitBarriers();
        if(payload.phase == 2u){
            for(const u32 tile : s_SparseTiles){
                const i32 x = static_cast<i32>((tile % s_TilesX) * NWB_REFRACTION_GROUP_SIZE);
                const i32 y = static_cast<i32>((tile / s_TilesX) * NWB_REFRACTION_GROUP_SIZE);
                commands.clearTextureRectFloat(*payload.depth, Core::TextureSubresourceSet{}, Core::Rect(x, x + 1, y, y + 1), Core::Color(0.5f));
            }
        }
        else if(payload.phase == 3u || payload.phase == 4u){
            const u32 count = Tests::Smoke::ExpectedRefractionProbeTileCount(payload.phase);
            const u32 fullRows = count / s_TilesX;
            commands.clearTextureRectFloat(*payload.depth, Core::TextureSubresourceSet{}, Core::Rect(0, static_cast<i32>(s_Width), 0, static_cast<i32>(fullRows * NWB_REFRACTION_GROUP_SIZE)), Core::Color(0.5f));
            const u32 remainder = count % s_TilesX;
            if(remainder != 0u){
                const i32 y = static_cast<i32>(fullRows * NWB_REFRACTION_GROUP_SIZE);
                commands.clearTextureRectFloat(*payload.depth, Core::TextureSubresourceSet{}, Core::Rect(0, static_cast<i32>(remainder * NWB_REFRACTION_GROUP_SIZE), y, y + NWB_REFRACTION_GROUP_SIZE), Core::Color(0.5f));
            }
        }
        else if(payload.phase == 5u){
            const i32 x = static_cast<i32>((s_MissingTile % s_TilesX) * NWB_REFRACTION_GROUP_SIZE);
            const i32 y = static_cast<i32>((s_MissingTile / s_TilesX) * NWB_REFRACTION_GROUP_SIZE);
            commands.clearTextureRectFloat(*payload.depth, Core::TextureSubresourceSet{}, Core::Rect(x, x + NWB_REFRACTION_GROUP_SIZE, y, y + NWB_REFRACTION_GROUP_SIZE), Core::Color(1.f));
        }
        Array<u32, s_ArgumentWords> arguments;
        arguments.fill(s_Sentinel);
        for(u32 word = 0u; word < NWB_REFRACTION_TILE_ARGUMENT_BYTES / sizeof(u32); ++word)
            arguments[word] = 0u;
        Array<u32, NWB_SMOKE_REFRACTION_PROBE_GUARD_WORDS> guards;
        guards.fill(s_Sentinel);
        commands.clearBufferUInt(*payload.queue, s_Sentinel);
        commands.clearBufferUInt(*payload.visits, 0u);
        commands.setBufferState(payload.visits.get(), Core::ResourceStates::CopyDest, true);
        commands.commitBarriers();
        if(!commands.tryWriteBuffer(*payload.arguments, arguments.data(), sizeof(arguments)) || !commands.tryWriteBuffer(*payload.visits, guards.data(), sizeof(guards), static_cast<u64>(s_VisitWords - NWB_SMOKE_REFRACTION_PROBE_GUARD_WORDS) * sizeof(u32)))
            return false;

        payload.stage = NWB_TEXT("production tile classification");
        commands.setTextureState(payload.depth.get(), Core::TextureSubresourceSet{}, Core::ResourceStates::ShaderResource);
        commands.setBufferState(payload.queue.get(), Core::ResourceStates::UnorderedAccess);
        commands.setBufferState(payload.arguments.get(), Core::ResourceStates::UnorderedAccess);
        commands.setBufferState(payload.visits.get(), Core::ResourceStates::UnorderedAccess);
        commands.commitBarriers();
        Core::ComputeState classify;
        classify.setPipeline(payload.classify.get());
        commands.setComputeState(classify);
        payload.heap.bindCompute(commands, *payload.classify);
        commands.setPushConstants(&payload.push, sizeof(payload.push));
        commands.dispatch(s_TilesX, s_TilesY, 1u);

        payload.stage = NWB_TEXT("production indirect route finalization");
        commands.setBufferState(payload.arguments.get(), Core::ResourceStates::UnorderedAccess, true);
        commands.commitBarriers();
        Core::ComputeState finalize;
        finalize.setPipeline(payload.finalize.get());
        commands.setComputeState(finalize);
        payload.heap.bindCompute(commands, *payload.finalize);
        commands.setPushConstants(&payload.push, sizeof(payload.push));
        commands.dispatch(1u, 1u, 1u);

        payload.stage = NWB_TEXT("compact indirect mapping verification");
        commands.setBufferState(payload.queue.get(), Core::ResourceStates::ShaderResource);
        commands.setBufferState(payload.arguments.get(), Core::ResourceStates::ShaderResource | Core::ResourceStates::IndirectArgument);
        commands.commitBarriers();
        Core::ComputeState verifyCompact;
        verifyCompact.setPipeline(payload.verifyCompact.get()).setIndirectParams(payload.arguments.get());
        commands.setComputeState(verifyCompact);
        payload.heap.bindCompute(commands, *payload.verifyCompact);
        commands.setPushConstants(&payload.push, sizeof(payload.push));
        commands.dispatchIndirect(0u);

        payload.stage = NWB_TEXT("dense indirect mapping verification");
        commands.setBufferState(payload.visits.get(), Core::ResourceStates::UnorderedAccess, true);
        commands.commitBarriers();
        Core::ComputeState verifyDense;
        verifyDense.setPipeline(payload.verifyDense.get()).setIndirectParams(payload.arguments.get());
        commands.setComputeState(verifyDense);
        payload.heap.bindCompute(commands, *payload.verifyDense);
        // A valid reversed list distinguishes direct dense mapping from queue traversal without changing classifier output.
        ProbePushConstants verifyPush = payload.push;
        verifyPush.tileListSlot = payload.denseTileListSlot;
        commands.setPushConstants(&verifyPush, sizeof(verifyPush));
        commands.dispatchIndirect(NWB_REFRACTION_DENSE_ARGUMENT_OFFSET);

        payload.stage = NWB_TEXT("counter and queue readback");
        const Array<Core::BufferHandle, 3u> buffers{ payload.arguments, payload.queue, payload.visits };
        for(const auto& buffer : buffers)
            commands.setBufferState(buffer.get(), Core::ResourceStates::CopySource);
        commands.commitBarriers();
        u64 offset = 0u;
        for(const auto& buffer : buffers){
            const u64 bytes = buffer->getCreationDescription().byteSize;
            commands.copyBuffer(*payload.readback, offset, *buffer, 0u, bytes);
            offset += bytes;
            commands.setBufferState(buffer.get(), Core::ResourceStates::CopyDest);
        }
        commands.setTextureState(payload.depth.get(), Core::TextureSubresourceSet{}, Core::ResourceStates::CopyDest);
        commands.commitBarriers();
        if(commands.commandRecordingFailed())
            return false;
        payload.stage = NWB_TEXT("native close and submission");
        return true;
    }
};

[[nodiscard]] static Expected<Core::GpuTaskId> DeclareProbe(void* userData, Core::GpuTaskGraph& graph){
    const auto& payload = *MakeNotNull(static_cast<const ProbeTask::Payload*>(userData));
    payload.stage = NWB_TEXT("graph imports");
    const auto importBuffer = [&graph](const Core::BufferHandle& buffer, const AStringView label, const Core::ResourceStates::Mask state){
        return graph.importBuffer(buffer, Core::GpuGraphResourceDesc{}.setIdentity(buffer->getCreationDescription().debugName).setMarkerLabel(label).setType(Core::GpuGraphResourceType::Buffer).setInitialState(state).setExternalFinalState(state));
    };
    const Array<Expected<Core::GpuGraphResourceId>, 6u> buffers{
        importBuffer(payload.queue, "Refraction probe tile queue", Core::ResourceStates::CopyDest),
        importBuffer(payload.arguments, "Refraction probe indirect arguments", Core::ResourceStates::CopyDest),
        importBuffer(payload.visits, "Refraction probe visit counters", Core::ResourceStates::CopyDest),
        importBuffer(payload.slots, "Refraction probe heap selectors", Core::ResourceStates::ConstantBuffer),
        importBuffer(payload.readback, "Refraction probe readback", Core::ResourceStates::CopyDest),
        importBuffer(payload.reversedQueue, "Refraction probe reversed dense tile list", Core::ResourceStates::ShaderResource),
    };
    for(const auto& buffer : buffers){
        if(!buffer)
            return MakeUnexpected(Failure{});
    }
    const auto initial = payload.phase == 0u ? Core::ResourceStates::Unknown : Core::ResourceStates::CopyDest;
    const auto depthDesc = Core::GpuGraphResourceDesc{}.setIdentity(s_ProbeName).setMarkerLabel("Refraction probe capture depth").setType(Core::GpuGraphResourceType::Texture).setInitialState(initial).setExternalFinalState(Core::ResourceStates::CopyDest);
    const auto depth = graph.importTexture(payload.depth, depthDesc);
    if(!depth)
        return MakeUnexpected(Failure{});
    const Array<Core::GpuTaskResourceUse, 7> uses = {{
        { .resource = *buffers[0], .range = {}, .requiredState = Core::ResourceStates::CopyDest, .access = Core::GpuTaskResourceAccess::Write },
        { .resource = *buffers[1], .range = {}, .requiredState = Core::ResourceStates::CopyDest, .access = Core::GpuTaskResourceAccess::Write },
        { .resource = *buffers[2], .range = {}, .requiredState = Core::ResourceStates::CopyDest, .access = Core::GpuTaskResourceAccess::Write },
        { .resource = *buffers[3], .range = {}, .requiredState = Core::ResourceStates::ConstantBuffer, .access = Core::GpuTaskResourceAccess::Read },
        { .resource = *buffers[4], .range = {}, .requiredState = Core::ResourceStates::CopyDest, .access = Core::GpuTaskResourceAccess::Write },
        { .resource = *buffers[5], .range = {}, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read },
        { .resource = *depth, .range = {}, .requiredState = Core::ResourceStates::CopyDest, .access = Core::GpuTaskResourceAccess::Write },
    }};
    payload.stage = NWB_TEXT("task declaration");
    const auto task = graph.addTask<ProbeTask>(Core::GpuTaskDesc{}.setIdentity(s_ProbeName).setMarkerLabel("Refraction tile queue boundary probe").setResourceUses(uses), ProbeTask::Payload(payload));
    if(task)
        payload.stage = NWB_TEXT("scheduling or initial-state recording admission");
    return task;
}

[[nodiscard]] static b8 CompareReadback(const ProbeTask::Payload& payload, Core::Device& device, Core::Alloc::ScratchArena& scratch){
    const auto mapping = device.mapBuffer(*payload.readback, Core::CpuAccessMode::Read);
    if(!mapping || !*mapping)
        return false;
    ScopeExit unmap([&]()noexcept{ device.unmapBuffer(*payload.readback); });
    const auto words = MakeNotNull(static_cast<const u32*>(*mapping));
    const u32 count = Tests::Smoke::ExpectedRefractionProbeTileCount(payload.phase);
    const b8 dense = count == s_TileCount;
    const u32 compactX = dense ? 0u : Min(count, NWB_REFRACTION_MAX_GROUP_COUNT_X);
    const u32 compactY = dense ? 1u : Max(1u, DivideUp(count, NWB_REFRACTION_MAX_GROUP_COUNT_X));
    const u32 denseX = dense ? s_TilesX : 0u;
    const u32 denseY = dense ? s_TilesY : 1u;
    const Array<u32, 8u> expectedArguments{ compactX, compactY, 1u, count, denseX, denseY, 1u, 0u };
    for(u32 word = 0u; word < s_ArgumentWords; ++word){
        const u32 expected = word < expectedArguments.size() ? expectedArguments[word] : s_Sentinel;
        if(words.get()[word] != expected){
            NWB_LOGGER_ERROR(NWB_TEXT("RefractionTileProbe: phase {} argument word {} expected {} observed {}"), payload.phase, word, expected, words.get()[word]);
            return false;
        }
    }
    Vector<u8, Core::Alloc::ScratchArena> seen{ scratch };
    seen.resize(s_TileCount, 0u);
    const auto queue = MakeNotNull(words.get() + s_ArgumentWords);
    for(u32 index = 0u; index < count; ++index){
        const u32 tile = queue.get()[index];
        if(tile >= s_TileCount || !Tests::Smoke::IsRefractionProbeTileActive(payload.phase, tile) || seen[tile] != 0u){
            NWB_LOGGER_ERROR(NWB_TEXT("RefractionTileProbe: phase {} invalid or duplicate queue tile {} at {}"), payload.phase, tile, index);
            return false;
        }
        seen[tile] = 1u;
    }
    for(u32 index = count; index < s_QueueWords; ++index){
        const u32 tile = queue.get()[index];
        if(tile != s_Sentinel){
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RefractionTileProbe: readback layout argument_bytes={} queue_bytes={} visit_bytes={} readback_bytes={} expected_queue_words={} queue_offset={} visit_offset={}")
                , payload.arguments->getCreationDescription().byteSize, payload.queue->getCreationDescription().byteSize, payload.visits->getCreationDescription().byteSize, payload.readback->getCreationDescription().byteSize
                , s_QueueWords, s_ArgumentWords * sizeof(u32), (s_ArgumentWords + s_QueueWords) * sizeof(u32)
            );
            for(u32 neighbor = count > 4u ? count - 4u : 0u; neighbor < Min(count + NWB_SMOKE_REFRACTION_PROBE_GUARD_WORDS, s_QueueWords); ++neighbor)
                NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RefractionTileProbe: queue[{}]=0x{:08x}"), neighbor, queue.get()[neighbor]);
            const auto counters = MakeNotNull(queue.get() + s_QueueWords);
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RefractionTileProbe: counters groups={} tails={} errors={} edge_lanes={}"), counters.get()[0], counters.get()[1], counters.get()[2], counters.get()[3]);
            NWB_LOGGER_ERROR(NWB_TEXT("RefractionTileProbe: phase {} queue tail overwritten at {} expected 0x{:08x} observed 0x{:08x}"), payload.phase, index, s_Sentinel, tile);
            return false;
        }
    }
    const auto visits = MakeNotNull(queue.get() + s_QueueWords);
    u32 pixelCount = 0u;
    for(u32 tile = 0u; tile < s_TileCount; ++tile){
        const u32 expected = Tests::Smoke::IsRefractionProbeTileActive(payload.phase, tile) ? 1u : 0u;
        if(seen[tile] != expected || visits.get()[NWB_SMOKE_REFRACTION_PROBE_HEADER_WORDS + tile] != expected){
            NWB_LOGGER_ERROR(NWB_TEXT("RefractionTileProbe: phase {} queue/resolve tile {} expected {} queue {} visits {}"), payload.phase, tile, expected, seen[tile], visits.get()[NWB_SMOKE_REFRACTION_PROBE_HEADER_WORDS + tile]);
            return false;
        }
        const u32 width = Min(static_cast<u32>(NWB_REFRACTION_GROUP_SIZE), s_Width - (tile % s_TilesX) * NWB_REFRACTION_GROUP_SIZE);
        const u32 height = Min(static_cast<u32>(NWB_REFRACTION_GROUP_SIZE), s_Height - (tile / s_TilesX) * NWB_REFRACTION_GROUP_SIZE);
        pixelCount += width * height * expected;
    }
    const u32 compactGroups = compactX * compactY;
    const u32 denseGroups = denseX * denseY;
    const u32 groups = compactGroups + denseGroups;
    const Array<u32, NWB_SMOKE_REFRACTION_PROBE_HEADER_WORDS> expectedHeader{ groups, groups - count, 0u, count * NWB_REFRACTION_GROUP_SIZE * NWB_REFRACTION_GROUP_SIZE - pixelCount, compactGroups, denseGroups };
    for(u32 word = 0u; word < expectedHeader.size(); ++word){
        if(visits.get()[word] != expectedHeader[word]){
            NWB_LOGGER_ERROR(NWB_TEXT("RefractionTileProbe: phase {} counter {} expected {} observed {}"), payload.phase, word, expectedHeader[word], visits.get()[word]);
            return false;
        }
    }
    for(u32 pixel = 0u; pixel < s_PixelCount; ++pixel){
        const u32 tile = ((pixel / s_Width) / NWB_REFRACTION_GROUP_SIZE) * s_TilesX + (pixel % s_Width) / NWB_REFRACTION_GROUP_SIZE;
        const u32 actual = visits.get()[NWB_SMOKE_REFRACTION_PROBE_HEADER_WORDS + s_TileCount + pixel];
        if(actual != seen[tile]){
            NWB_LOGGER_ERROR(NWB_TEXT("RefractionTileProbe: phase {} pixel {} expected {} observed {}"), payload.phase, pixel, seen[tile], actual);
            return false;
        }
    }
    for(u32 word = s_VisitWords - NWB_SMOKE_REFRACTION_PROBE_GUARD_WORDS; word < s_VisitWords; ++word){
        if(visits.get()[word] != s_Sentinel){
            NWB_LOGGER_ERROR(NWB_TEXT("RefractionTileProbe: phase {} visit guard overwritten at {}"), payload.phase, word);
            return false;
        }
    }
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RefractionTileProbe: phase {} active_tiles={} compact={}x{} dense={}x{} pixels={} tail_groups={} edge_lanes={} argument_bytes={}")
        , payload.phase, count, compactX, compactY, denseX, denseY, pixelCount, expectedHeader[1], expectedHeader[3], NWB_REFRACTION_TILE_ARGUMENT_BYTES
    );
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RunRefractionTileProbe(const NWB::Impl::ActiveSceneSubsystems& sceneSubsystems, NWB::ProjectRuntimeContext& context){
    using namespace NWB;
    using namespace __hidden_refraction_tile_probe;
    auto& graphics = context.graphics;
    auto& device = graphics.getDevice();
    auto& heap = device.getDescriptorHeap();
    Core::Alloc::ScratchArena scratch("tests/smoke/refraction_tile_probe");
    Core::ShaderHandle classifyShader;
    Core::ShaderHandle finalizeShader;
    Core::ShaderHandle verifyCompactShader;
    Core::ShaderHandle verifyDenseShader;
    const Impl::RefractiveSubsystem* const refractiveSubsystem = sceneSubsystems.findSubsystem<Impl::RefractiveSubsystem>();
    const auto classifyShaderRef = refractiveSubsystem ? refractiveSubsystem->refractionClassifyTilesCs : Core::Assets::AssetRef<Impl::ComputeShader>{};
    const auto finalizeShaderRef = refractiveSubsystem ? refractiveSubsystem->refractionFinalizeTilesCs : Core::Assets::AssetRef<Impl::ComputeShader>{};
    const auto verifyShaderRef = sceneSubsystems.findSceneAsset<Impl::ComputeShader>(s_VerifyShaderName);
    if(!classifyShaderRef.valid() || !finalizeShaderRef.valid() || !verifyShaderRef.valid())
        return false;
    const auto loadProbeShader = [&](const Name& shaderName, const AStringView variantName, Core::ShaderHandle& outShader){
        return Impl::ShaderAssetLoader::Load<Impl::ComputeShader>(shaderName, variantName, s_ProbeName, NWB_TEXT("RefractionTileProbe"), graphics, context.assetManager, context.shaderPathResolver, nullptr, outShader);
    };
    if(
        !loadProbeShader(classifyShaderRef.name(), Core::ShaderArchive::s_DefaultVariant, classifyShader)
        || !loadProbeShader(finalizeShaderRef.name(), Core::ShaderArchive::s_DefaultVariant, finalizeShader)
        || !loadProbeShader(verifyShaderRef.name(), AStringView("NWB_REFRACTION_DENSE_GRID=0"), verifyCompactShader)
        || !loadProbeShader(verifyShaderRef.name(), AStringView("NWB_REFRACTION_DENSE_GRID=1"), verifyDenseShader)
    )
        return false;
    Core::BindingLayoutDesc layoutDesc(context.objectArena);
    layoutDesc.setVisibility(Core::ShaderType::Compute).addItem(Core::BindingLayoutItem::PushConstants(0u, sizeof(ProbePushConstants)));
    const auto layout = device.createBindingLayout(layoutDesc);
    if(!layout)
        return false;
    Core::ComputePipelineDesc classifyDesc;
    classifyDesc.setComputeShader(classifyShader).addBindingLayout(*layout).addBindingLayout(heap.getResourceLayout()).addBindingLayout(heap.getSamplerLayout());
    Core::ComputePipelineDesc finalizeDesc;
    finalizeDesc.setComputeShader(finalizeShader).addBindingLayout(*layout).addBindingLayout(heap.getResourceLayout()).addBindingLayout(heap.getSamplerLayout());
    Core::ComputePipelineDesc verifyCompactDesc;
    verifyCompactDesc.setComputeShader(verifyCompactShader).addBindingLayout(*layout).addBindingLayout(heap.getResourceLayout()).addBindingLayout(heap.getSamplerLayout());
    Core::ComputePipelineDesc verifyDenseDesc;
    verifyDenseDesc.setComputeShader(verifyDenseShader).addBindingLayout(*layout).addBindingLayout(heap.getResourceLayout()).addBindingLayout(heap.getSamplerLayout());
    const auto classify = device.createComputePipeline(classifyDesc);
    const auto finalize = device.createComputePipeline(finalizeDesc);
    const auto verifyCompact = device.createComputePipeline(verifyCompactDesc);
    const auto verifyDense = device.createComputePipeline(verifyDenseDesc);
    const auto createBuffer = [&graphics](const Name name, const u32 words, const b8 indirect, const b8 readback){
        Core::BufferDesc desc;
        desc.setDebugName(name).setByteSize(static_cast<u64>(words) * sizeof(u32)).setStructStride(sizeof(u32)).setCanHaveUAVs(!readback).setCanHaveRawViews(!readback).setIsDrawIndirectArgs(indirect);
        desc.setCpuAccess(readback ? Core::CpuAccessMode::Read : Core::CpuAccessMode::None).enableAutomaticStateTracking(Core::ResourceStates::CopyDest);
        return graphics.createBuffer(desc);
    };
    Vector<u32, Core::Alloc::ScratchArena> reversedTiles{ scratch };
    reversedTiles.resize(s_QueueWords, s_Sentinel);
    for(u32 tile = 0u; tile < s_TileCount; ++tile)
        reversedTiles[tile] = s_TileCount - 1u - tile;
    Core::GraphicsStandaloneService::BufferSetupDesc reversedSetup;
    reversedSetup.bufferDesc.setDebugName(Name(NWB::Tests::Smoke::s_SmokeNameRefractionTileProbeReversedQueue)).setByteSize(static_cast<u64>(s_QueueWords) * sizeof(u32)).setStructStride(sizeof(u32));
    reversedSetup.bufferDesc.setCanHaveUAVs(true).setCanHaveRawViews(true).enableAutomaticStateTracking(Core::ResourceStates::ShaderResource);
    reversedSetup.data = reversedTiles.data();
    reversedSetup.dataSize = static_cast<u64>(s_QueueWords) * sizeof(u32);
    reversedSetup.queue = Core::CommandQueue::Graphics;
    const auto reversedQueue = graphics.standaloneGraph().setupBuffer(reversedSetup);
    const auto queue = createBuffer(Name(NWB::Tests::Smoke::s_SmokeNameRefractionTileProbeQueue), s_QueueWords, false, false);
    const auto arguments = createBuffer(Name(NWB::Tests::Smoke::s_SmokeNameRefractionTileProbeArguments), s_ArgumentWords, true, false);
    const auto visits = createBuffer(Name(NWB::Tests::Smoke::s_SmokeNameRefractionTileProbeVisits), s_VisitWords, false, false);
    const auto readback = createBuffer(Name(NWB::Tests::Smoke::s_SmokeNameRefractionTileProbeReadback), s_ReadbackWords, false, true);
    Core::TextureDesc depthDesc;
    depthDesc.setWidth(s_Width).setHeight(s_Height).setFormat(Core::Format::R32_FLOAT).setName(s_ProbeName).setInitialState(Core::ResourceStates::CopyDest).setKeepInitialState(true);
    const auto depth = graphics.createTexture(depthDesc);
    if(!classify || !finalize || !verifyCompact || !verifyDense || !queue || !reversedQueue || !arguments || !visits || !readback || !depth)
        return false;
    Core::BufferHandle slotsBuffer;
    Array<Core::GpuDescriptorHandle, 6u> descriptors{};
    Core::GpuDescriptorRetirementGuard retire(heap, descriptors);
    const Array<Core::BufferHandle, 4u> storageBuffers{ *queue, *arguments, *visits, *reversedQueue };
    for(u32 index = 0u; index < storageBuffers.size(); ++index){
        const auto descriptor = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
        if(!descriptor)
            return false;
        descriptors[index] = *descriptor;
        if(!heap.write(*descriptor, Core::DescriptorWriteItem::RawBufferUav(0u, storageBuffers[index].get())))
            return false;
    }
    const auto sampled = heap.allocate(Core::GpuDescriptorClass::SampledImage);
    if(!sampled)
        return false;
    descriptors[4] = *sampled;
    if(!heap.write(*sampled, Core::DescriptorWriteItem::TextureSrv(0u, depth->get(), depthDesc.format, Core::TextureSubresourceSet{}, Core::TextureDimension::Texture2D)))
        return false;
    Impl::DeferredBindlessResourceSlots slots;
    slots.refractionDepth = sampled->slot();
    Core::GraphicsStandaloneService::BufferSetupDesc setup;
    setup.bufferDesc.setDebugName(Name(NWB::Tests::Smoke::s_SmokeNameRefractionTileProbeSlots)).setByteSize(sizeof(slots)).setIsConstantBuffer(true).enableAutomaticStateTracking(Core::ResourceStates::ConstantBuffer);
    setup.data = &slots;
    setup.dataSize = sizeof(slots);
    setup.queue = Core::CommandQueue::Graphics;
    const auto uploadedSlots = graphics.standaloneGraph().setupBuffer(setup);
    if(!uploadedSlots)
        return false;
    slotsBuffer = *uploadedSlots;
    const auto uniform = heap.allocate(Core::GpuDescriptorClass::UniformBuffer);
    if(!uniform)
        return false;
    descriptors[5] = *uniform;
    if(!heap.write(*uniform, Core::DescriptorWriteItem::ConstantBuffer(0u, slotsBuffer.get())))
        return false;
    TStringView stage = NWB_TEXT("probe initialization");
    ProbeTask::Payload payload{ *depth, *queue, *reversedQueue, *arguments, *visits, slotsBuffer, *readback, *classify, *finalize, *verifyCompact, *verifyDense, heap, stage, {} };
    payload.denseTileListSlot = descriptors[3].slot();
    payload.push.width = s_Width;
    payload.push.height = s_Height;
    payload.push.deferredResourcesHeapSlot = uniform->slot();
    payload.push.tileListSlot = descriptors[0].slot();
    payload.push.tileArgumentsSlot = descriptors[1].slot();
    payload.push.materialContextSlotsHeapSlot = descriptors[2].slot();
    for(u32 phase = 0u; phase < s_PhaseCount; ++phase){
        payload.phase = phase;
        const auto token = graphics.standaloneGraph().submitStandaloneTaskGraph(&payload, DeclareProbe);
        if(!token){
            NWB_LOGGER_ERROR(NWB_TEXT("RefractionTileProbe: phase {} standalone graph rejected at {}"), phase, stage);
            return false;
        }
        if(!device.waitForSubmissionToken(*token)){
            NWB_LOGGER_ERROR(NWB_TEXT("RefractionTileProbe: phase {} completion wait failed"), phase);
            return false;
        }
        if(!CompareReadback(payload, device, scratch))
            return false;
    }
    NWB_LOGGER_ESSENTIAL_INFO(
        NWB_TEXT("RefractionTileProbe: passed 6 phases at 2049x2049 with 66049 tiles, production finalizer, specialized dense/compact mapping, "
            "all 32 argument bytes, all-but-one sparse boundary, queue uniqueness, 65535/65536 boundary, indirect Y tail, 25190406 pixel checks and guards")
    );
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_avboit_depth_warp_probe{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Tests::Smoke;

inline constexpr u32 s_MaxVirtualCount = s_AvboitMaxVirtualCount;
inline constexpr u32 s_CoverageWords = s_AvboitCoverageWords;
inline constexpr u32 s_GuardWords = 4u;
inline constexpr u32 s_WarpWords = s_MaxVirtualCount + s_GuardWords;
inline constexpr u32 s_ControlWords = NWB_AVBOIT_CONTROL_WORD_COUNT + s_GuardWords;
inline constexpr u32 s_ResultWords = s_WarpWords + s_ControlWords;
inline constexpr u32 s_BatchSize = 64u;
inline constexpr u32 s_Sentinel = 0xa5f07c39u;
inline constexpr u32 s_BoundaryCaseCount = s_AvboitBoundaryCaseCount;
inline constexpr u32 s_ExhaustiveCaseCount = s_AvboitExhaustiveCaseCount;
inline constexpr u32 s_RandomCaseCount = s_AvboitRandomCaseCount;
inline constexpr u32 s_TotalCaseCount = s_AvboitTotalCaseCount;
inline constexpr Name s_ProbeName = NWB::Tests::Smoke::s_SmokeNameAvboitDepthWarpProbe;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using ProbeCase = AvboitDepthWarpCase;

[[nodiscard]] static Impl::RendererAvboitPushConstants BuildAvboitDepthWarpPush(const ProbeCase& probe, const u32 ordinal, const u32 slots)noexcept{
    Impl::RendererAvboitPushConstants push;
    push.frame[0] = 1000u + ordinal % 997u;
    push.frame[1] = 300u + ordinal % 251u;
    push.frame[2] = (push.frame[0] + 2u) / 3u;
    push.frame[3] = (push.frame[1] + 3u) / 4u;
    push.volume[0] = probe.virtualCount;
    push.volume[1] = probe.physicalCount;
    push.volume[3] = (probe.virtualCount + 31u) / 32u;
    push.heapSlots[0] = slots;
    return push;
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ProbeBatchTask{
    struct Payload{
        Core::BufferHandle coverage;
        Core::BufferHandle warp;
        Core::BufferHandle control;
        Core::BufferHandle slots;
        Core::BufferHandle readback;
        Core::ComputePipelineHandle pipeline;
        Core::GpuDescriptorHeap& heap;
        // The project keeps this case storage immutable until the accepted batch token completes.
        const ProbeCase* cases = nullptr;
        u32 firstCase = 0u;
        u32 caseCount = 0u;
        u32 slotsIndex = 0u;
    };


public:
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements{ .requiredCapabilities = Core::GpuQueueCapability::Compute | Core::GpuQueueCapability::Transfer, .requiresPrimaryGraphicsQueue = true, };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
        for(u32 index = 0u; index < payload.caseCount; ++index){
            const ProbeCase& probe = payload.cases[index];
            commandList.setBufferState(payload.coverage.get(), Core::ResourceStates::CopyDest);
            commandList.setBufferState(payload.warp.get(), Core::ResourceStates::CopyDest);
            commandList.setBufferState(payload.control.get(), Core::ResourceStates::CopyDest);
            commandList.commitBarriers();
            if(!commandList.tryWriteBuffer(*payload.coverage, probe.coverage.data(), sizeof(probe.coverage))){
                NWB_LOGGER_ERROR(NWB_TEXT("AvboitDepthWarpProbe: coverage upload failed at case {}"), payload.firstCase + index);
                return false;
            }
            commandList.clearBufferUInt(*payload.warp, s_Sentinel);
            commandList.clearBufferUInt(*payload.control, s_Sentinel);
            commandList.setBufferState(payload.coverage.get(), Core::ResourceStates::UnorderedAccess);
            commandList.setBufferState(payload.warp.get(), Core::ResourceStates::UnorderedAccess);
            commandList.setBufferState(payload.control.get(), Core::ResourceStates::UnorderedAccess);
            commandList.commitBarriers();

            Core::ComputeState state;
            state.setPipeline(payload.pipeline.get());
            commandList.setComputeState(state);
            payload.heap.bindCompute(commandList, *payload.pipeline);
            const auto push = BuildAvboitDepthWarpPush(probe, payload.firstCase + index, payload.slotsIndex);
            commandList.setPushConstants(&push, sizeof(push));
            commandList.dispatch(NWB_AVBOIT_DEPTH_WARP_DISPATCH_GROUP_COUNT_X, 1u, 1u);

            commandList.setBufferState(payload.warp.get(), Core::ResourceStates::CopySource);
            commandList.setBufferState(payload.control.get(), Core::ResourceStates::CopySource);
            commandList.commitBarriers();
            const u64 offset = static_cast<u64>(index) * s_ResultWords * sizeof(u32);
            commandList.copyBuffer(*payload.readback, offset, *payload.warp, 0u, s_WarpWords * sizeof(u32));
            commandList.copyBuffer(*payload.readback, offset + s_WarpWords * sizeof(u32), *payload.control, 0u, s_ControlWords * sizeof(u32));
            if(commandList.commandRecordingFailed()){
                NWB_LOGGER_ERROR(NWB_TEXT("AvboitDepthWarpProbe: command recording failed at case {}"), payload.firstCase + index);
                return false;
            }
        }
        commandList.setBufferState(payload.coverage.get(), Core::ResourceStates::CopyDest);
        commandList.setBufferState(payload.warp.get(), Core::ResourceStates::CopyDest);
        commandList.setBufferState(payload.control.get(), Core::ResourceStates::CopyDest);
        commandList.commitBarriers();
        return !commandList.commandRecordingFailed();
    }
};

[[nodiscard]] static Expected<Core::GpuTaskId> DeclareBatch(void* userData, Core::GpuTaskGraph& graph){
    const auto& payload = *MakeNotNull(static_cast<const ProbeBatchTask::Payload*>(userData));
    const auto import = [&graph](const Core::BufferHandle& buffer, const AStringView label, const Core::ResourceStates::Mask state){
        Core::GpuGraphResourceDesc desc;
        desc.setIdentity(buffer->getCreationDescription().debugName).setMarkerLabel(label).setType(Core::GpuGraphResourceType::Buffer).setInitialState(state).setExternalFinalState(state);
        return graph.importBuffer(buffer, desc);
    };
    const Array<Expected<Core::GpuGraphResourceId>, 5u> imported{
        import(payload.coverage, "Depth warp probe coverage", Core::ResourceStates::CopyDest),
        import(payload.warp, "Depth warp probe LUT", Core::ResourceStates::CopyDest),
        import(payload.control, "Depth warp probe control", Core::ResourceStates::CopyDest),
        import(payload.slots, "Depth warp probe heap slots", Core::ResourceStates::ConstantBuffer),
        import(payload.readback, "Depth warp probe readback", Core::ResourceStates::CopyDest),
    };
    for(u32 index = 0u; index < imported.size(); ++index){
        if(!imported[index]){
            NWB_LOGGER_ERROR(NWB_TEXT("AvboitDepthWarpProbe: graph buffer import failed at index {}"), index);
            return MakeUnexpected(Failure{});
        }
    }
    // One task owns the transfer/compute transitions within its batch and restores the declared external states.
    const Array<Core::GpuTaskResourceUse, 5u> uses{
        Core::GpuTaskResourceUse{ .resource = *imported[0u], .range = {}, .requiredState = Core::ResourceStates::CopyDest, .access = Core::GpuTaskResourceAccess::ReadWrite, },
        Core::GpuTaskResourceUse{ .resource = *imported[1u], .range = {}, .requiredState = Core::ResourceStates::CopyDest, .access = Core::GpuTaskResourceAccess::ReadWrite, },
        Core::GpuTaskResourceUse{ .resource = *imported[2u], .range = {}, .requiredState = Core::ResourceStates::CopyDest, .access = Core::GpuTaskResourceAccess::ReadWrite, },
        Core::GpuTaskResourceUse{ .resource = *imported[3u], .range = {}, .requiredState = Core::ResourceStates::ConstantBuffer, .access = Core::GpuTaskResourceAccess::Read, },
        Core::GpuTaskResourceUse{ .resource = *imported[4u], .range = {}, .requiredState = Core::ResourceStates::CopyDest, .access = Core::GpuTaskResourceAccess::Write, },
    };
    for(u32 index = 0u; index < uses.size(); ++index){
        if(!uses[index].resource.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("AvboitDepthWarpProbe: graph buffer import failed at index {}"), index);
            return MakeUnexpected(Failure{});
        }
    }
    Core::GpuTaskDesc desc;
    desc.setIdentity(s_ProbeName).setMarkerLabel("AVBOIT depth warp boundary probe").setResourceUses(uses);
    const auto taskResult = graph.addTask<ProbeBatchTask>(desc, ProbeBatchTask::Payload(payload));
    if(!taskResult){
        NWB_LOGGER_ERROR(NWB_TEXT("AvboitDepthWarpProbe: graph task declaration failed at case {}"), payload.firstCase);
        return MakeUnexpected(Failure{});
    }
    const auto task = *taskResult;
    if(!task.valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("AvboitDepthWarpProbe: graph task declaration failed at case {}"), payload.firstCase);
        return MakeUnexpected(Failure{});
    }
    return task;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class DepthWarpUnfocusedPass final : public Core::IRenderPass{
public:
    explicit DepthWarpUnfocusedPass(Core::GraphicsRuntime& graphics)noexcept : IRenderPass(graphics){}


public:
    virtual b8 shouldRenderUnfocused()noexcept override{ return true; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class DepthWarpProbeProject final : public IProjectEntryCallbacks{
private:
    [[nodiscard]] static Expected<Core::BufferHandle> CreateBuffer(const Name& name, const u32 words, const b8 readback, Core::GraphicsRuntime& graphics){
        Core::BufferDesc desc;
        desc.setDebugName(name).setByteSize(words * sizeof(u32)).setStructStride(sizeof(u32)).setCanHaveUAVs(!readback).setCpuAccess(readback ? Core::CpuAccessMode::Read : Core::CpuAccessMode::None).enableAutomaticStateTracking(Core::ResourceStates::CopyDest);
        return graphics.createBuffer(desc);
    }


public:
    explicit DepthWarpProbeProject(ProjectRuntimeContext& context)noexcept
        : m_sceneSubsystems(context.objectArena)
        , m_cases(context.objectArena)
        , m_context(context)
        , m_unfocusedPass(context.graphics)
    {}


public:
    virtual b8 onStartup()override{
        BuildAvboitDepthWarpCases(m_cases);
        if(m_cases.size() != s_TotalCaseCount){
            NWB_LOGGER_ERROR(NWB_TEXT("AvboitDepthWarpProbe: case manifest count mismatch {}"), m_cases.size());
            return false;
        }
        auto& graphics = m_context.graphics;
        auto& device = graphics.getDevice();
        auto& heap = device.getDescriptorHeap();
        if(!heap.isInitialized()){
            NWB_LOGGER_ERROR(NWB_TEXT("AvboitDepthWarpProbe: bindless heap is unavailable"));
            return false;
        }
        if(!m_sceneSubsystems.loadProjectStartScene(m_context.assetManager))
            return false;
        const Impl::TransparentSubsystem* const transparentSubsystem = m_sceneSubsystems.findSubsystem<Impl::TransparentSubsystem>();
        const auto depthWarpShaderRef = transparentSubsystem ? transparentSubsystem->avboitDepthWarpCs : Core::Assets::AssetRef<Impl::ComputeShader>{};
        if(!depthWarpShaderRef.valid())
            return false;
        if(!Impl::ShaderAssetLoader::Load<Impl::ComputeShader>(
            depthWarpShaderRef.name(),
            Core::ShaderArchive::s_DefaultVariant,
            s_ProbeName,
            NWB_TEXT("AvboitDepthWarpProbe"),
            graphics,
            m_context.assetManager,
            m_context.shaderPathResolver,
            nullptr,
            m_shader
        ))
            return false;
        Core::BindingLayoutDesc layoutDesc(m_context.objectArena);
        layoutDesc.setVisibility(Core::ShaderType::Compute).addItem(Core::BindingLayoutItem::PushConstants(0u, NWB_AVBOIT_PUSH_CONSTANT_BYTE_SIZE));
        auto pushLayoutResult = device.createBindingLayout(layoutDesc);
        if(pushLayoutResult)
            m_pushLayout = Move(*pushLayoutResult);
        else
            m_pushLayout.reset();
        if(!pushLayoutResult){
            NWB_LOGGER_ERROR(NWB_TEXT("AvboitDepthWarpProbe: push layout creation failed"));
            return false;
        }
        Core::ComputePipelineDesc pipelineDesc;
        pipelineDesc.setComputeShader(m_shader).addBindingLayout(m_pushLayout).addBindingLayout(heap.getResourceLayout()).addBindingLayout(heap.getSamplerLayout());
        auto pipelineResult = device.createComputePipeline(pipelineDesc);
        if(pipelineResult)
            m_pipeline = Move(*pipelineResult);
        else
            m_pipeline.reset();
        auto coverageResult = CreateBuffer(Name(NWB::Tests::Smoke::s_SmokeNameDepthWarpProbeCoverage), s_CoverageWords, false, graphics);
        if(coverageResult)
            m_coverage = Move(*coverageResult);
        else
            m_coverage.reset();
        auto warpResult = CreateBuffer(Name(NWB::Tests::Smoke::s_SmokeNameDepthWarpProbeWarp), s_WarpWords, false, graphics);
        if(warpResult)
            m_warp = Move(*warpResult);
        else
            m_warp.reset();
        auto controlResult = CreateBuffer(Name(NWB::Tests::Smoke::s_SmokeNameDepthWarpProbeControl), s_ControlWords, false, graphics);
        if(controlResult)
            m_control = Move(*controlResult);
        else
            m_control.reset();
        auto readbackResult = CreateBuffer(Name(NWB::Tests::Smoke::s_SmokeNameDepthWarpProbeReadback), s_BatchSize * s_ResultWords, true, graphics);
        if(readbackResult)
            m_readback = Move(*readbackResult);
        else
            m_readback.reset();
        if(!pipelineResult || !coverageResult || !warpResult || !controlResult || !readbackResult){
            NWB_LOGGER_ERROR(NWB_TEXT("AvboitDepthWarpProbe: pipeline or buffer creation failed"));
            return false;
        }
        const Array<Core::BufferHandle, 3u> buffers{ m_coverage, m_warp, m_control };
        for(u32 index = 0u; index < buffers.size(); ++index){
            const Expected<Core::GpuDescriptorHandle> descriptor = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
            if(descriptor)
                m_descriptors[index] = *descriptor;
            if(!descriptor || !heap.write(m_descriptors[index], Core::DescriptorWriteItem::StructuredBufferUav(0u, buffers[index].get()))){
                NWB_LOGGER_ERROR(NWB_TEXT("AvboitDepthWarpProbe: storage descriptor setup failed at index {}"), index);
                return false;
            }
        }
        Impl::DeferredBindlessResourceSlots slots;
        slots.avboitCoverage = m_descriptors[0].slot();
        slots.avboitDepthWarp = m_descriptors[1].slot();
        slots.avboitControl = m_descriptors[2].slot();
        Core::GraphicsStandaloneService::BufferSetupDesc slotsDesc;
        slotsDesc.bufferDesc.setDebugName(Name(NWB::Tests::Smoke::s_SmokeNameDepthWarpProbeSlots)).setByteSize(sizeof(slots)).setIsConstantBuffer(true).enableAutomaticStateTracking(Core::ResourceStates::ConstantBuffer);
        slotsDesc.data = &slots;
        slotsDesc.dataSize = sizeof(slots);
        slotsDesc.queue = Core::CommandQueue::Graphics;
        auto slotsResult = graphics.standaloneGraph().setupBuffer(slotsDesc);
        if(slotsResult)
            m_slots = Move(*slotsResult);
        else
            m_slots.reset();
        const Expected<Core::GpuDescriptorHandle> slotsDescriptor = heap.allocate(Core::GpuDescriptorClass::UniformBuffer);
        if(slotsDescriptor)
            m_descriptors[3] = *slotsDescriptor;
        if(!slotsResult || !slotsDescriptor || !heap.write(m_descriptors[3], Core::DescriptorWriteItem::ConstantBuffer(0u, m_slots.get()))){
            NWB_LOGGER_ERROR(NWB_TEXT("AvboitDepthWarpProbe: slot payload setup failed"));
            return false;
        }
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("AvboitDepthWarpProbe: manifest boundary={} exhaustive={} randomized={} total={} seed=0x6b47c2d1"), s_BoundaryCaseCount, s_ExhaustiveCaseCount, s_RandomCaseCount, s_TotalCaseCount);
        m_context.graphics.addRenderPassToBack(m_unfocusedPass);
        m_unfocusedPassRegistered = true;
        return true;
    }

    virtual b8 onUpdate(const f32)override{
        if(m_completed)
            return true;
        ProbeBatchTask::Payload payload{ m_coverage, m_warp, m_control, m_slots, m_readback, m_pipeline, m_context.graphics.getDevice().getDescriptorHeap(), nullptr, 0u, 0u, m_descriptors[3].slot() };
        for(u32 firstCase = 0u; firstCase < m_cases.size(); firstCase += s_BatchSize){
            payload.cases = m_cases.data() + firstCase;
            payload.firstCase = firstCase;
            payload.caseCount = Min(s_BatchSize, static_cast<u32>(m_cases.size()) - firstCase);
            const auto token = m_context.graphics.standaloneGraph().submitStandaloneTaskGraph(&payload, DeclareBatch);
            if(!token){
                NWB_LOGGER_ERROR(NWB_TEXT("AvboitDepthWarpProbe: batch submission failed at case {}"), firstCase);
                return false;
            }
            if(!m_context.graphics.getDevice().waitForSubmissionToken(*token)){
                NWB_LOGGER_ERROR(NWB_TEXT("AvboitDepthWarpProbe: batch completion wait failed at case {}"), firstCase);
                return false;
            }
            if(!compareBatch(payload))
                return false;
        }
        if(!RunAvboitIntegrationProbe(m_sceneSubsystems, m_context))
            return false;
        if(!RunRefractionTileProbe(m_sceneSubsystems, m_context))
            return false;
        m_completed = true;
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("AvboitDepthWarpProbe: passed {} cases with full LUT/control comparison and sentinel tails"), s_TotalCaseCount);
        m_context.requestQuit();
        return true;
    }

    virtual void onShutdown()override{
        if(m_unfocusedPassRegistered){
            m_context.graphics.removeRenderPass(m_unfocusedPass);
            m_unfocusedPassRegistered = false;
        }
        if(!m_context.graphics.waitForIdle())
            NWB_LOGGER_ERROR(NWB_TEXT("AvboitDepthWarpProbe: shutdown completion wait failed"));
        auto& heap = m_context.graphics.getDevice().getDescriptorHeap();
        for(auto& descriptor : m_descriptors){
            if(descriptor.valid()){
                heap.free(descriptor);
                descriptor = Core::GpuDescriptorHandle::Invalid();
            }
        }
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("AvboitDepthWarpProbe: shutdown completed={}"), m_completed ? 1u : 0u);
    }


private:
    [[nodiscard]] b8 compareBatch(const ProbeBatchTask::Payload& payload){
        auto& device = m_context.graphics.getDevice();
        const Expected<void*> mappedResult = device.mapBuffer(*m_readback, Core::CpuAccessMode::Read);
        if(!mappedResult){
            NWB_LOGGER_ERROR(NWB_TEXT("AvboitDepthWarpProbe: readback mapping failed at case {}"), payload.firstCase);
            return false;
        }
        const auto mapped = MakeNotNull(static_cast<const u32*>(*mappedResult));
        b8 passed = true;
        for(u32 index = 0u; passed && index < payload.caseCount; ++index){
            const ProbeCase& probe = payload.cases[index];
            const u32 ordinal = payload.firstCase + index;
            const auto push = BuildAvboitDepthWarpPush(probe, ordinal, payload.slotsIndex);
            const auto oracle = BuildAvboitDepthWarpOracle(probe, Array<u32, 4u>{ push.frame[0], push.frame[1], push.frame[2], push.frame[3] });
            const auto result = MakeNotNull(mapped.get() + index * s_ResultWords);
            for(u32 word = 0u; word < s_ResultWords; ++word){
                const u32 expected = word < probe.virtualCount ? oracle.warp[word] : word >= s_WarpWords && word < s_WarpWords + NWB_AVBOIT_CONTROL_WORD_COUNT ? oracle.control[word - s_WarpWords] : s_Sentinel;
                if(result.get()[word] != expected){
                    NWB_LOGGER_ERROR(NWB_TEXT("AvboitDepthWarpProbe: mismatch case {} scenario {} virtual {} physical {} word {} expected {} observed {}"), ordinal, probe.scenario, probe.virtualCount, probe.physicalCount, word, expected, result.get()[word]);
                    passed = false;
                    break;
                }
            }
        }
        device.unmapBuffer(*m_readback);
        return passed;
    }


private:
    Impl::ActiveSceneSubsystems m_sceneSubsystems;
    Array<Core::GpuDescriptorHandle, 4u> m_descriptors{};
    Vector<ProbeCase, Core::Alloc::GlobalArena> m_cases;
    Core::ShaderHandle m_shader;
    Core::BindingLayoutHandle m_pushLayout;
    Core::ComputePipelineHandle m_pipeline;
    Core::BufferHandle m_coverage;
    Core::BufferHandle m_warp;
    Core::BufferHandle m_control;
    Core::BufferHandle m_slots;
    Core::BufferHandle m_readback;
    ProjectRuntimeContext& m_context;
    DepthWarpUnfocusedPass m_unfocusedPass;
    b8 m_unfocusedPassRegistered = false;
    b8 m_completed = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::ProjectFrameClientSize NWB::QueryProjectFrameClientSize(){
    return { 320u, 240u };
}

TStringView NWB::QueryProjectWindowTitle()noexcept{
    return NWB_TEXT("NWB AVBOIT Depth Warp Probe");
}

UniquePtr<NWB::IProjectEntryCallbacks> NWB::CreateProjectEntryCallbacks(NWB::ProjectRuntimeContext& context){
    return MakeUnique<__hidden_avboit_depth_warp_probe::DepthWarpProbeProject>(context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

