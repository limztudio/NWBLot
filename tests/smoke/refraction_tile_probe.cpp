// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "refraction_tile_probe.h"
#include "assets/shaders/refraction_tile_probe_layout.h"

#include <impl/assets_graphics/refraction/names.h>
#include <impl/assets_shader/loader.h>
#include <impl/assets/graphics/refraction/push_constants.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>

#include <core/common/log.h>
#include <core/graphics/backend_selection/backend.h>
#include <core/graphics/shader_archive.h>
#include <core/task/gpu/task_graph.h>


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
inline constexpr u32 s_GuardWords = NWB_SMOKE_REFRACTION_PROBE_GUARD_WORDS;
inline constexpr u32 s_ArgumentWords = NWB_REFRACTION_TILE_ARGUMENT_BYTES / sizeof(u32) + s_GuardWords;
inline constexpr u32 s_QueueWords = s_TileCount + s_GuardWords;
inline constexpr u32 s_VisitWords = NWB_SMOKE_REFRACTION_PROBE_HEADER_WORDS + s_TileCount + s_PixelCount + s_GuardWords;
inline constexpr u32 s_ReadbackWords = s_ArgumentWords + s_QueueWords + s_VisitWords;
inline constexpr u32 s_Sentinel = 0xa573d91bu;
inline constexpr u32 s_PhaseCount = 6u;
inline constexpr u32 s_MissingTile = (s_TilesY / 2u) * s_TilesX + s_TilesX / 2u;
inline constexpr Array<u32, 6u> s_SparseTiles{ 0u, s_TilesX - 1u, (s_TilesY - 1u) * s_TilesX, s_TileCount - 1u, 65534u, 65535u };
inline constexpr Name s_ProbeName("RefractionTileProbe");
inline constexpr Name s_VerifyShaderName("project/shaders/refraction_tile_probe_cs");

struct ProbePushConstants{
#define NWB_SMOKE_REFRACTION_CPU_FIELD(name, defaultValue) u32 name = defaultValue;
    NWB_REFRACTION_PUSH_CONSTANTS_FIELDS(NWB_SMOKE_REFRACTION_CPU_FIELD)
#undef NWB_SMOKE_REFRACTION_CPU_FIELD
};

static_assert(s_TileCount == 66049u && s_TileCount > NWB_REFRACTION_MAX_GROUP_COUNT_X);
static_assert(NWB_REFRACTION_TILE_ARGUMENT_BYTES == 32u && NWB_REFRACTION_TILE_COUNTER_OFFSET == 12u && NWB_REFRACTION_DENSE_ARGUMENT_OFFSET == 16u);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool TileActive(const u32 phase, const u32 tile)noexcept{
    if(phase == 0u)
        return true;
    if(phase == 1u)
        return false;
    if(phase == 2u){
        for(const u32 sparse : s_SparseTiles){
            if(tile == sparse)
                return true;
        }
        return false;
    }
    if(phase == 5u)
        return tile != s_MissingTile;
    return tile < (phase == 3u ? NWB_REFRACTION_MAX_GROUP_COUNT_X : NWB_REFRACTION_MAX_GROUP_COUNT_X + 1u);
}

[[nodiscard]] static u32 ExpectedTileCount(const u32 phase)noexcept{
    return phase == 0u ? s_TileCount : phase == 1u ? 0u : phase == 2u ? static_cast<u32>(s_SparseTiles.size()) : phase == 3u ? NWB_REFRACTION_MAX_GROUP_COUNT_X : phase == 4u ? NWB_REFRACTION_MAX_GROUP_COUNT_X + 1u : s_TileCount - 1u;
}

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
    [[nodiscard]] static bool Record(const Payload& payload, Core::CommandList& commands, const Core::GpuTaskRecordContext& context){
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
            const u32 count = ExpectedTileCount(payload.phase);
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
        Array<u32, s_GuardWords> guards;
        guards.fill(s_Sentinel);
        commands.clearBufferUInt(*payload.queue, s_Sentinel);
        commands.clearBufferUInt(*payload.visits, 0u);
        commands.setBufferState(payload.visits.get(), Core::ResourceStates::CopyDest, true);
        commands.commitBarriers();
        if(!commands.tryWriteBuffer(*payload.arguments, arguments.data(), sizeof(arguments)) || !commands.tryWriteBuffer(*payload.visits, guards.data(), sizeof(guards), static_cast<u64>(s_VisitWords - s_GuardWords) * sizeof(u32)))
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
    const auto& payload = *static_cast<const ProbeTask::Payload*>(userData);
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
    const auto depth = graph.importTexture(payload.depth, Core::GpuGraphResourceDesc{}.setIdentity(s_ProbeName).setMarkerLabel("Refraction probe capture depth").setType(Core::GpuGraphResourceType::Texture).setInitialState(initial).setExternalFinalState(Core::ResourceStates::CopyDest));
    if(!depth)
        return MakeUnexpected(Failure{});
    const Core::GpuTaskResourceUse uses[]{
        { .resource = *buffers[0], .range = {}, .requiredState = Core::ResourceStates::CopyDest, .access = Core::GpuTaskResourceAccess::Write },
        { .resource = *buffers[1], .range = {}, .requiredState = Core::ResourceStates::CopyDest, .access = Core::GpuTaskResourceAccess::Write },
        { .resource = *buffers[2], .range = {}, .requiredState = Core::ResourceStates::CopyDest, .access = Core::GpuTaskResourceAccess::Write },
        { .resource = *buffers[3], .range = {}, .requiredState = Core::ResourceStates::ConstantBuffer, .access = Core::GpuTaskResourceAccess::Read },
        { .resource = *buffers[4], .range = {}, .requiredState = Core::ResourceStates::CopyDest, .access = Core::GpuTaskResourceAccess::Write },
        { .resource = *buffers[5], .range = {}, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read },
        { .resource = *depth, .range = {}, .requiredState = Core::ResourceStates::CopyDest, .access = Core::GpuTaskResourceAccess::Write },
    };
    payload.stage = NWB_TEXT("task declaration");
    const auto task = graph.addTask<ProbeTask>(Core::GpuTaskDesc{}.setIdentity(s_ProbeName).setMarkerLabel("Refraction tile queue boundary probe").setResourceUses(uses, LengthOf(uses)), ProbeTask::Payload(payload));
    if(task)
        payload.stage = NWB_TEXT("scheduling or initial-state recording admission");
    return task;
}

[[nodiscard]] static bool CompareReadback(Core::Device& device, Core::Alloc::ScratchArena& scratch, const ProbeTask::Payload& payload){
    const auto mapping = device.mapBuffer(*payload.readback, Core::CpuAccessMode::Read);
    if(!mapping || !*mapping)
        return false;
    ScopeExit unmap([&]()noexcept{ device.unmapBuffer(*payload.readback); });
    const auto words = MakeNotNull(static_cast<const u32*>(*mapping));
    const u32 count = ExpectedTileCount(payload.phase);
    const bool dense = count == s_TileCount;
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
        if(tile >= s_TileCount || !TileActive(payload.phase, tile) || seen[tile] != 0u){
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
            for(u32 neighbor = count > 4u ? count - 4u : 0u; neighbor < Min(count + s_GuardWords, s_QueueWords); ++neighbor)
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
        const u32 expected = TileActive(payload.phase, tile) ? 1u : 0u;
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
    for(u32 word = s_VisitWords - s_GuardWords; word < s_VisitWords; ++word){
        if(visits.get()[word] != s_Sentinel){
            NWB_LOGGER_ERROR(NWB_TEXT("RefractionTileProbe: phase {} visit guard overwritten at {}"), payload.phase, word);
            return false;
        }
    }
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RefractionTileProbe: phase {} active_tiles={} compact={}x{} dense={}x{} pixels={} tail_groups={} edge_lanes={} argument_bytes={}"), payload.phase, count, compactX, compactY, denseX, denseY, pixelCount, expectedHeader[1], expectedHeader[3], NWB_REFRACTION_TILE_ARGUMENT_BYTES);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RunRefractionTileProbe(NWB::ProjectRuntimeContext& context){
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
    if(
        !Impl::ShaderAssetLoader::Load<Impl::ComputeShader>(classifyShader, Impl::AssetsGraphicsRefraction::s_ClassifyTilesShaderName, Core::ShaderArchive::s_DefaultVariant, s_ProbeName, graphics, context.assetManager, context.shaderPathResolver, NWB_TEXT("RefractionTileProbe"))
        || !Impl::ShaderAssetLoader::Load<Impl::ComputeShader>(finalizeShader, Impl::AssetsGraphicsRefraction::s_FinalizeTilesShaderName, Core::ShaderArchive::s_DefaultVariant, s_ProbeName, graphics, context.assetManager, context.shaderPathResolver, NWB_TEXT("RefractionTileProbe"))
        || !Impl::ShaderAssetLoader::Load<Impl::ComputeShader>(verifyCompactShader, s_VerifyShaderName, AStringView("NWB_REFRACTION_DENSE_GRID=0"), s_ProbeName, graphics, context.assetManager, context.shaderPathResolver, NWB_TEXT("RefractionTileProbe"))
        || !Impl::ShaderAssetLoader::Load<Impl::ComputeShader>(verifyDenseShader, s_VerifyShaderName, AStringView("NWB_REFRACTION_DENSE_GRID=1"), s_ProbeName, graphics, context.assetManager, context.shaderPathResolver, NWB_TEXT("RefractionTileProbe"))
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
    const auto createBuffer = [&graphics](const Name name, const u32 words, const bool indirect, const bool readback){
        Core::BufferDesc desc;
        desc.setDebugName(name).setByteSize(static_cast<u64>(words) * sizeof(u32)).setStructStride(sizeof(u32)).setCanHaveUAVs(!readback).setCanHaveRawViews(!readback).setIsDrawIndirectArgs(indirect).setCpuAccess(readback ? Core::CpuAccessMode::Read : Core::CpuAccessMode::None).enableAutomaticStateTracking(Core::ResourceStates::CopyDest);
        return graphics.createBuffer(desc);
    };
    Vector<u32, Core::Alloc::ScratchArena> reversedTiles{ scratch };
    reversedTiles.resize(s_QueueWords, s_Sentinel);
    for(u32 tile = 0u; tile < s_TileCount; ++tile)
        reversedTiles[tile] = s_TileCount - 1u - tile;
    Core::GraphicsRuntime::BufferSetupDesc reversedSetup;
    reversedSetup.bufferDesc.setDebugName(Name("RefractionTileProbeReversedQueue")).setByteSize(static_cast<u64>(s_QueueWords) * sizeof(u32)).setStructStride(sizeof(u32)).setCanHaveUAVs(true).setCanHaveRawViews(true).enableAutomaticStateTracking(Core::ResourceStates::ShaderResource);
    reversedSetup.data = reversedTiles.data();
    reversedSetup.dataSize = static_cast<u64>(s_QueueWords) * sizeof(u32);
    reversedSetup.queue = Core::CommandQueue::Graphics;
    const auto reversedQueue = graphics.setupBuffer(reversedSetup);
    const auto queue = createBuffer(Name("RefractionTileProbeQueue"), s_QueueWords, false, false);
    const auto arguments = createBuffer(Name("RefractionTileProbeArguments"), s_ArgumentWords, true, false);
    const auto visits = createBuffer(Name("RefractionTileProbeVisits"), s_VisitWords, false, false);
    const auto readback = createBuffer(Name("RefractionTileProbeReadback"), s_ReadbackWords, false, true);
    Core::TextureDesc depthDesc;
    depthDesc.setWidth(s_Width).setHeight(s_Height).setFormat(Core::Format::R32_FLOAT).setName(s_ProbeName).setInitialState(Core::ResourceStates::CopyDest).setKeepInitialState(true);
    const auto depth = graphics.createTexture(depthDesc);
    if(!classify || !finalize || !verifyCompact || !verifyDense || !queue || !reversedQueue || !arguments || !visits || !readback || !depth)
        return false;
    Core::BufferHandle slotsBuffer;
    Array<Core::GpuDescriptorHandle, 6u> descriptors{};
    Core::GpuDescriptorRetirementGuard retire(heap, Span<const Core::GpuDescriptorHandle>(descriptors.data(), descriptors.size()));
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
    Core::GraphicsRuntime::BufferSetupDesc setup;
    setup.bufferDesc.setDebugName(Name("RefractionTileProbeSlots")).setByteSize(sizeof(slots)).setIsConstantBuffer(true).enableAutomaticStateTracking(Core::ResourceStates::ConstantBuffer);
    setup.data = &slots;
    setup.dataSize = sizeof(slots);
    setup.queue = Core::CommandQueue::Graphics;
    const auto uploadedSlots = graphics.setupBuffer(setup);
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
        const auto token = graphics.submitStandaloneTaskGraph(&payload, DeclareProbe);
        if(!token){
            NWB_LOGGER_ERROR(NWB_TEXT("RefractionTileProbe: phase {} standalone graph rejected at {}"), phase, stage);
            return false;
        }
        if(!device.waitForSubmissionToken(*token)){
            NWB_LOGGER_ERROR(NWB_TEXT("RefractionTileProbe: phase {} completion wait failed"), phase);
            return false;
        }
        if(!CompareReadback(device, scratch, payload))
            return false;
    }
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RefractionTileProbe: passed 6 phases at 2049x2049 with 66049 tiles, production finalizer, specialized dense/compact mapping, all 32 argument bytes, all-but-one sparse boundary, queue uniqueness, 65535/65536 boundary, indirect Y tail, 25190406 pixel checks and guards"));
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

