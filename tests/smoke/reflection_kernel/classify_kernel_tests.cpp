// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "reflection_kernel_fixture.h"

#include <impl/assets/graphics/reflection/frame_constants.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>

#include <global/bit.h>
#include <global/math/convert.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_classify_kernel_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct Parameters{
#define NWB_CLASSIFY_TEST_UINT(name, value) u32 name = value;
    NWB_REFLECTION_FRAME_UINT_FIELDS(NWB_CLASSIFY_TEST_UINT)
#undef NWB_CLASSIFY_TEST_UINT
#define NWB_CLASSIFY_TEST_FLOAT(name, value) f32 name = value;
    NWB_REFLECTION_FRAME_FLOAT_FIELDS(NWB_CLASSIFY_TEST_FLOAT)
#undef NWB_CLASSIFY_TEST_FLOAT
};

struct Pixel{
    f32 r;
    f32 g;
    f32 b;
    f32 a;
};

struct View{
    f32 worldToClip[16]{};
    f32 clipToWorld[16]{};
    f32 cameraPosition[4]{};
};

namespace Source{
    enum Enum : u32{
        OpaqueSpecular,
        GlassSpecular,
        OpaqueNormal,
        OpaquePosition,
        OpaqueDepth,
        GlassNormal,
        GlassDepth,
        ScreenColor,
        DepthInterval,
        kCount,
    };
};

namespace Pattern{
    enum Enum : u8{
        Dense,
        Invalid,
        Rough,
        ScreenCrossings,
    };
};

namespace Feedback{
    enum Enum : u8{
        Absent,
        Active,
        FirstMiss,
        Dormant,
        InvalidEntry,
        InvalidHeader,
        StaleFrame,
        WrongTileCount,
        MissingPrevious,
        BoundExceedsBudget,
    };
};

struct Case{
    u32 width = 8u;
    u32 height = 8u;
    u32 mode = NWB_REFLECTION_MODE_HARDWARE;
    u32 hardware = 1u;
    u32 diagnostics = 1u;
    u32 budget = 0xffffffffu;
    u32 capacity = 0xffffffffu;
    u32 screenSteps = 96u;
    u32 depthMips = 1u;
    u32 probeIndex = 1u;
    Pattern::Enum pattern = Pattern::Dense;
    Feedback::Enum feedback = Feedback::Absent;
};

struct Observation{
    Vector<u16, Alloc::ScratchArena> pixels;
    Vector<u32, Alloc::ScratchArena> queue;
    Vector<u32, Alloc::ScratchArena> feedback;
    u32 counters[NWB_REFLECTION_COUNTER_SIZE / sizeof(u32)]{};


    explicit Observation(Alloc::ScratchArena& arena)
        : pixels(arena)
        , queue(arena)
        , feedback(arena)
    {}
};

struct GroupCandidates{
    u32 masks[4]{};
    u32 values[128]{};
    u32 count = 0u;
};

static constexpr u32 s_GuardWords = 8u;
static constexpr u32 s_QueueSentinel = 0xdeadc0deu;
static constexpr u32 s_FeedbackSentinel = 0x5a17b19du;
static_assert(sizeof(Parameters) == 192u);
static_assert(sizeof(View) == 144u);
static_assert(sizeof(Pixel) == 16u);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void FillSource(const Case& testCase, const Source::Enum source, Vector<Pixel, Alloc::ScratchArena>& output){
    output.clear();
    const usize pixelCount = static_cast<usize>(testCase.width) * testCase.height;
    output.reserve(pixelCount);
    for(u32 y = 0u; y < testCase.height; ++y){
        for(u32 x = 0u; x < testCase.width; ++x){
            const u32 index = y * testCase.width + x;
            const f32 worldX = (static_cast<f32>(x) + 0.5f) * 2.0f / testCase.width - 1.0f;
            const f32 worldY = 1.0f - (static_cast<f32>(y) + 0.5f) * 2.0f / testCase.height;
            const bool crossedPanel = testCase.pattern == Pattern::ScreenCrossings && x > 0u;
            Pixel value{};
            switch(source){
            case Source::OpaqueSpecular:
            case Source::GlassSpecular:
                value = { 0.04f, 0.08f, 0.16f, 0.0f };
                if(testCase.pattern == Pattern::Rough && source == Source::OpaqueSpecular)
                    value.a = static_cast<f32>(index % 5u) * 0.25f;
                if(crossedPanel && source == Source::OpaqueSpecular)
                    value = { 0.0f, 0.0f, 0.0f, 0.0f };
                if(testCase.pattern == Pattern::Invalid){
                    if(index % 5u == 0u)
                        value.r = BitCast<f32>(0x7fc00000u);
                    else if(index % 5u == 1u)
                        value = { 0.0f, 0.0f, 0.0f, 0.0f };
                }
                break;
            case Source::OpaqueNormal:
                value = crossedPanel ? Pixel{ 0.0f, 0.5f, 0.5f, 0.0f } : Pixel{ 0.8535534f, 0.5f, 0.8535534f, 0.0f };
                break;
            case Source::OpaquePosition:
                value = { worldX, worldY, 2.0f, 0.0f };
                break;
            case Source::OpaqueDepth:
            case Source::GlassDepth:
                value.r = testCase.pattern == Pattern::Invalid && index % 7u == 0u ? 1.0f : 0.5f;
                break;
            case Source::GlassNormal:
                value = crossedPanel ? Pixel{ -1.0f, 0.0f, 0.0f, 1.5f } : Pixel{ 0.7071068f, 0.0f, 0.7071068f, 1.5f };
                if(testCase.pattern == Pattern::Invalid && index % 3u == 0u)
                    value.a = index % 2u == 0u ? 1.0f : BitCast<f32>(0x7f800000u);
                break;
            case Source::ScreenColor:
                value = { 0.5f + static_cast<f32>(x % 4u), 0.25f, 2.0f, 1.0f };
                break;
            case Source::DepthInterval:
                // A deliberately loose conservative interval forces real plane-hit validation; mip reduction has its own native oracle.
                value = testCase.pattern == Pattern::ScreenCrossings
                    ? Pixel{ 0.0f, 1.0f, 0.0f, 0.0f } : Pixel{ 0.5f, 0.5f, 0.0f, 0.0f };
                break;
            default:
                break;
            }
            output.push_back(value);
        }
    }
}

static void RunClassify(
    GraphicsBackend::Device& device,
    ComputePipeline& pipeline,
    const Case& testCase,
    const bool fullQueue,
    const f32 outputSentinel,
    Alloc::ScratchArena& scratchArena,
    Observation& observation){
    const u32 width = testCase.width;
    const u32 height = testCase.height;
    const u32 pixelCount = width * height;
    const u32 tilesX = DivideUp<u32>(width, NWB_REFLECTION_CLASSIFY_GROUP_SIZE);
    const u32 tilesY = DivideUp<u32>(height, NWB_REFLECTION_CLASSIFY_GROUP_SIZE);
    const u32 tileCount = tilesX * tilesY;
    constexpr u32 outputDescriptorBase = Source::kCount;
    constexpr u32 slotsDescriptor = outputDescriptorBase + 2u;
    constexpr u32 viewDescriptor = slotsDescriptor + 1u;
    constexpr u32 parametersDescriptor = viewDescriptor + 1u;
    constexpr u32 queueDescriptor = parametersDescriptor + 1u;
    constexpr u32 countersDescriptor = queueDescriptor + 1u;
    constexpr u32 feedbackReadDescriptor = countersDescriptor + 1u;
    constexpr u32 feedbackWriteDescriptor = feedbackReadDescriptor + 1u;
    GpuDescriptorHandle descriptors[feedbackWriteDescriptor + 1u]{};
    auto& heap = device.getDescriptorHeap();
    ScopeExit releaseDescriptors([&]()noexcept{
        for(const GpuDescriptorHandle handle : descriptors){
            if(handle.valid())
                heap.free(handle);
        }
        heap.collectRetired();
    });
    TextureDesc sourceDesc;
    sourceDesc
        .setWidth(width).setHeight(height).setFormat(Format::RGBA32_FLOAT)
        .setInitialState(ResourceStates::Common).setKeepInitialState(true)
    ;
    TextureHandle sources[Source::kCount];
    for(u32 index = 0u; index < Source::kCount; ++index){
        sources[index] = device.createTexture(sourceDesc);
        ASSERT_TRUE(sources[index]);
        descriptors[index] = heap.allocate(GpuDescriptorClass::SampledImage);
        ASSERT_TRUE(descriptors[index].valid());
        ASSERT_TRUE(heap.write(descriptors[index], DescriptorWriteItem::Texture_SRV(0u, sources[index].get())));
    }
    TextureDesc outputDesc = sourceDesc;
    outputDesc.setFormat(Format::RGBA16_FLOAT).setInUAV(true);
    TextureHandle outputs[2];
    StagingTextureHandle readbacks[2];
    for(u32 surface = 0u; surface < 2u; ++surface){
        outputs[surface] = device.createTexture(outputDesc);
        readbacks[surface] = device.createStagingTexture(outputDesc, CpuAccessMode::Read);
        ASSERT_TRUE(outputs[surface]);
        ASSERT_TRUE(readbacks[surface]);
        descriptors[outputDescriptorBase + surface] = heap.allocate(GpuDescriptorClass::StorageImage);
        ASSERT_TRUE(descriptors[outputDescriptorBase + surface].valid());
        ASSERT_TRUE(heap.write(descriptors[outputDescriptorBase + surface], DescriptorWriteItem::Texture_UAV(0u, outputs[surface].get())));
    }
    const BufferHandle constants[] = {
        device.createBuffer(BufferDesc().setByteSize(sizeof(Impl::DeferredBindlessResourceSlots)).setIsConstantBuffer(true)
            .setInitialState(ResourceStates::ConstantBuffer).setKeepInitialState(true)),
        device.createBuffer(BufferDesc().setByteSize(sizeof(View)).setIsConstantBuffer(true)
            .setInitialState(ResourceStates::ConstantBuffer).setKeepInitialState(true)),
        device.createBuffer(BufferDesc().setByteSize(sizeof(Parameters)).setIsConstantBuffer(true)
            .setInitialState(ResourceStates::ConstantBuffer).setKeepInitialState(true)),
    };
    for(u32 index = 0u; index < LengthOf(constants); ++index){
        ASSERT_TRUE(constants[index]);
        descriptors[slotsDescriptor + index] = heap.allocate(GpuDescriptorClass::UniformBuffer);
        ASSERT_TRUE(descriptors[slotsDescriptor + index].valid());
        ASSERT_TRUE(heap.write(descriptors[slotsDescriptor + index], DescriptorWriteItem::ConstantBuffer(0u, constants[index].get())));
    }
    const u32 rawWordCounts[] = {
        pixelCount * 2u + s_GuardWords, NWB_REFLECTION_COUNTER_SIZE / sizeof(u32),
        4u + tileCount * 2u + s_GuardWords, 4u + tileCount * 2u + s_GuardWords,
    };
    BufferHandle raw[4];
    for(u32 index = 0u; index < LengthOf(raw); ++index){
        BufferDesc desc;
        desc
            .setByteSize(rawWordCounts[index] * sizeof(u32)).setCanHaveRawViews(true).setCanHaveUAVs(true)
            .setCpuAccess(CpuAccessMode::Read).setInitialState(ResourceStates::Common).setKeepInitialState(true)
        ;
        raw[index] = device.createBuffer(desc);
        ASSERT_TRUE(raw[index]);
        descriptors[queueDescriptor + index] = heap.allocate(GpuDescriptorClass::StorageBuffer);
        ASSERT_TRUE(descriptors[queueDescriptor + index].valid());
        const DescriptorWriteItem item = index == 2u
            ? DescriptorWriteItem::RawBuffer_SRV(0u, raw[index].get())
            : DescriptorWriteItem::RawBuffer_UAV(0u, raw[index].get());
        ASSERT_TRUE(heap.write(descriptors[queueDescriptor + index], item));
    }
    Impl::DeferredBindlessResourceSlots slots;
    slots.gbufferNormal = descriptors[Source::OpaqueNormal].slot();
    slots.gbufferWorldPosition = descriptors[Source::OpaquePosition].slot();
    slots.gbufferDepth = descriptors[Source::OpaqueDepth].slot();
    slots.refractionDepth = descriptors[Source::GlassDepth].slot();
    slots.refractionNormalIor = descriptors[Source::GlassNormal].slot();
    slots.opaqueColor = descriptors[Source::ScreenColor].slot();
    View view;
    for(u32 diagonal = 0u; diagonal < 16u; diagonal += 5u){
        view.worldToClip[diagonal] = 1.0f;
        view.clipToWorld[diagonal] = 1.0f;
    }
    view.worldToClip[10] = -0.0625f;
    view.worldToClip[11] = 0.625f;
    view.clipToWorld[10] = -16.0f;
    view.clipToWorld[11] = 10.0f;
    view.cameraPosition[2] = 10.0f;
    view.cameraPosition[3] = 1.0f;
    Parameters parameters;
    parameters.width = width;
    parameters.height = height;
    parameters.traceMode = testCase.mode;
    parameters.hardwareEnabled = testCase.hardware;
    parameters.diagnosticsEnabled = testCase.diagnostics;
    parameters.maxHardwareRays = fullQueue ? pixelCount * 2u : Min(testCase.budget, pixelCount * 2u);
    parameters.queueCapacity = fullQueue ? pixelCount * 2u : Min(testCase.capacity, pixelCount * 2u);
    parameters.opaqueSpecularSlot = descriptors[Source::OpaqueSpecular].slot();
    parameters.glassSpecularSlot = descriptors[Source::GlassSpecular].slot();
    parameters.opaqueOutputSlot = descriptors[outputDescriptorBase].slot();
    parameters.glassOutputSlot = descriptors[outputDescriptorBase + 1u].slot();
    parameters.queueSlot = descriptors[queueDescriptor].slot();
    parameters.counterSlot = descriptors[countersDescriptor].slot();
    parameters.deferredResourcesSlot = descriptors[slotsDescriptor].slot();
    parameters.viewSlot = descriptors[viewDescriptor].slot();
    parameters.depthPyramidSlot = descriptors[Source::DepthInterval].slot();
    parameters.depthMipCount = testCase.depthMips;
    parameters.screenMaxSteps = testCase.screenSteps;
    parameters.samplingSeed = 0x73b4a81du;
    parameters.sampleBaseX = 0x163894f7u;
    parameters.sampleBaseY = 0xabcdef31u;
    parameters.feedbackProbeIndex = testCase.probeIndex;
    parameters.feedbackReadSlot = descriptors[feedbackReadDescriptor].slot();
    parameters.feedbackWriteSlot = descriptors[feedbackWriteDescriptor].slot();
    if(testCase.feedback != Feedback::Absent)
        parameters.feedbackFlags = NWB_REFLECTION_FEEDBACK_WRITE_ENABLED | NWB_REFLECTION_FEEDBACK_PREVIOUS_VALID;
    if(testCase.feedback == Feedback::MissingPrevious){
        parameters.feedbackFlags = NWB_REFLECTION_FEEDBACK_WRITE_ENABLED;
        parameters.feedbackReadSlot = 0xffffffffu;
    }
    Vector<u32, Alloc::ScratchArena> initialRaw(scratchArena);
    const CommandListHandle commandList = device.createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
    Vector<Pixel, Alloc::ScratchArena> sourcePixels(scratchArena);
    for(u32 index = 0u; index < Source::kCount; ++index){
        FillSource(testCase, static_cast<Source::Enum>(index), sourcePixels);
        ASSERT_TRUE(commandList->tryWriteTexture(*sources[index], 0u, 0u, sourcePixels.data(), width * sizeof(Pixel)));
        commandList->setTextureState(sources[index].get(), s_AllSubresources, ResourceStates::ShaderResource);
    }
    ASSERT_TRUE(commandList->tryWriteBuffer(*constants[0], &slots, sizeof(slots)));
    ASSERT_TRUE(commandList->tryWriteBuffer(*constants[1], &view, sizeof(view)));
    ASSERT_TRUE(commandList->tryWriteBuffer(*constants[2], &parameters, sizeof(parameters)));
    for(const BufferHandle& buffer : constants)
        commandList->setBufferState(buffer.get(), ResourceStates::ConstantBuffer);
    for(u32 index = 0u; index < LengthOf(raw); ++index){
        initialRaw.resize(rawWordCounts[index]);
        for(u32& value : initialRaw)
            value = index == 0u ? s_QueueSentinel : (index == 1u ? 0u : s_FeedbackSentinel);
        if(index == 2u){
            initialRaw[0] = testCase.feedback == Feedback::BoundExceedsBudget ? pixelCount * 2u + 1u : pixelCount * 2u;
            initialRaw[1] = testCase.probeIndex - (testCase.feedback == Feedback::StaleFrame ? 2u : 1u);
            initialRaw[2] = tileCount + (testCase.feedback == Feedback::WrongTileCount ? 1u : 0u);
            initialRaw[3] = testCase.feedback == Feedback::InvalidHeader ? 0u : NWB_REFLECTION_FEEDBACK_HEADER_VALID;
            for(u32 entry = 0u; entry < tileCount * 2u; ++entry){
                initialRaw[4u + entry] = testCase.feedback == Feedback::FirstMiss ? NWB_REFLECTION_FEEDBACK_FIRST_MISS
                    : (testCase.feedback == Feedback::Active ? NWB_REFLECTION_FEEDBACK_ACTIVE : NWB_REFLECTION_FEEDBACK_DORMANT);
                if(testCase.feedback == Feedback::InvalidEntry && entry % 2u == 0u)
                    initialRaw[4u + entry] = 0xffffffffu;
            }
        }
        ASSERT_TRUE(commandList->tryWriteBuffer(*raw[index], initialRaw.data(), initialRaw.size() * sizeof(u32)));
        commandList->setBufferState(raw[index].get(), index == 2u ? ResourceStates::ShaderResource : ResourceStates::UnorderedAccess);
    }
    for(const TextureHandle& output : outputs){
        commandList->clearTextureFloat(*output, s_AllSubresources, Color(outputSentinel));
        commandList->setTextureState(output.get(), s_AllSubresources, ResourceStates::UnorderedAccess, true);
    }
    commandList->commitBarriers();
    ComputeState state;
    state.setPipeline(&pipeline);
    commandList->setComputeState(state);
    heap.bindCompute(*commandList, pipeline);
    const u32 push = descriptors[parametersDescriptor].slot();
    commandList->setPushConstants(&push, sizeof(push));
    commandList->dispatch(tilesX, tilesY, 1u);
    for(u32 surface = 0u; surface < 2u; ++surface)
        commandList->copyTexture(*readbacks[surface], TextureSlice{}, *outputs[surface], TextureSlice{});
    ASSERT_FALSE(commandList->commandRecordingFailed());
    commandList->close();
    ASSERT_FALSE(commandList->commandRecordingFailed());
    CommandList* const submitted[] = { commandList.get() };
    const QueueSubmissionToken token = device.executeCommandLists(
        submitted, LengthOf(submitted), commandList->getDescription().physicalQueue, QueueSubmissionDesc{}
    );
    ASSERT_TRUE(token.valid());
    ASSERT_TRUE(device.waitForIdle());
    observation.pixels.resize(pixelCount * 8u);
    for(u32 surface = 0u; surface < 2u; ++surface){
        usize pitch = 0u;
        const u8* mapped = static_cast<const u8*>(device.mapStagingTexture(*readbacks[surface], TextureSlice{}, CpuAccessMode::Read, &pitch));
        ASSERT_NE(mapped, nullptr);
        ScopeExit unmap([&]()noexcept{ device.unmapStagingTexture(*readbacks[surface]); });
        ASSERT_GE(pitch, width * sizeof(u16) * 4u);
        for(u32 y = 0u; y < height; ++y){
            u16* destination = observation.pixels.data() + (surface * pixelCount + y * width) * 4u;
            NWB_MEMCPY(destination, width * sizeof(u16) * 4u, mapped + y * pitch, width * sizeof(u16) * 4u);
        }
    }
    for(u32 index = 0u; index < LengthOf(raw); ++index){
        if(index == 2u)
            continue;
        const u32* mapped = static_cast<const u32*>(device.mapBuffer(*raw[index], CpuAccessMode::Read));
        ASSERT_NE(mapped, nullptr);
        ScopeExit unmap([&]()noexcept{ device.unmapBuffer(*raw[index]); });
        if(index == 1u)
            NWB_MEMCPY(observation.counters, sizeof(observation.counters), mapped, sizeof(observation.counters));
        else{
            auto& destination = index == 0u ? observation.queue : observation.feedback;
            destination.assign(mapped, mapped + rawWordCounts[index]);
        }
    }
}

static void CheckQueue(
    const Case& testCase,
    const Observation& fullReference,
    const Observation& observed,
    Alloc::ScratchArena& scratchArena){
    const u32 pixelCount = testCase.width * testCase.height;
    const u32 tilesX = DivideUp<u32>(testCase.width, NWB_REFLECTION_CLASSIFY_GROUP_SIZE);
    const u32 tileCount = tilesX * DivideUp<u32>(testCase.height, NWB_REFLECTION_CLASSIFY_GROUP_SIZE);
    const u32 candidates = fullReference.counters[NWB_REFLECTION_COUNTER_CANDIDATES / sizeof(u32)];
    ASSERT_LE(candidates, pixelCount * 2u);
    ASSERT_EQ(observed.counters[NWB_REFLECTION_COUNTER_CANDIDATES / sizeof(u32)], candidates);
    Vector<GroupCandidates, Alloc::ScratchArena> groups(scratchArena);
    groups.resize(tileCount);
    for(u32 index = 0u; index < candidates; ++index){
        const u32 packed = fullReference.queue[index];
        ASSERT_LT(packed, pixelCount * 2u);
        const u32 surface = packed / pixelCount;
        const u32 pixel = packed % pixelCount;
        const u32 x = pixel % testCase.width;
        const u32 y = pixel / testCase.width;
        const u32 tile = y / 8u * tilesX + x / 8u;
        const u32 rank = surface * 64u + y % 8u * 8u + x % 8u;
        const u32 mask = 1u << (rank % 32u);
        ASSERT_EQ(groups[tile].masks[rank / 32u] & mask, 0u);
        groups[tile].masks[rank / 32u] |= mask;
    }
    for(u32 tile = 0u; tile < tileCount; ++tile){
        for(u32 rank = 0u; rank < 128u; ++rank){
            if((groups[tile].masks[rank / 32u] & (1u << (rank % 32u))) == 0u)
                continue;
            const u32 x = tile % tilesX * 8u + rank % 8u;
            const u32 y = tile / tilesX * 8u + rank % 64u / 8u;
            groups[tile].values[groups[tile].count++] = rank / 64u * pixelCount + y * testCase.width + x;
        }
    }
    const u32 count = Min(candidates, Min(testCase.budget, testCase.capacity));
    Vector<u8, Alloc::ScratchArena> seen(scratchArena);
    seen.resize(tileCount, 0u);
    u32 cursor = 0u;
    while(cursor < count){
        const u32 packed = observed.queue[cursor];
        ASSERT_LT(packed, pixelCount * 2u);
        const u32 pixel = packed % pixelCount;
        const u32 tile = pixel / testCase.width / 8u * tilesX + pixel % testCase.width / 8u;
        ASSERT_EQ(seen[tile], 0u) << "A group reservation may move, but its canonical block cannot be interleaved or duplicated";
        seen[tile] = 1u;
        ASSERT_GT(groups[tile].count, 0u);
        const u32 length = Min(groups[tile].count, count - cursor);
        for(u32 index = 0u; index < length; ++index)
            EXPECT_EQ(observed.queue[cursor + index], groups[tile].values[index]);
        cursor += length;
    }
    for(usize index = count; index < observed.queue.size(); ++index)
        EXPECT_EQ(observed.queue[index], s_QueueSentinel) << "Queue budget or trailing guard overwritten at " << index;
}

static void CompareCase(
    GraphicsBackend::Device& device,
    ComputePipeline& reference,
    ComputePipeline& candidate,
    const Case& testCase,
    Alloc::ScratchArena& scratchArena){
    SCOPED_TRACE(testCase.width);
    SCOPED_TRACE(testCase.height);
    SCOPED_TRACE(testCase.mode);
    SCOPED_TRACE(testCase.budget);
    SCOPED_TRACE(static_cast<u32>(testCase.feedback));
    SCOPED_TRACE(testCase.probeIndex);
    Observation full(scratchArena);
    Observation before(scratchArena);
    Observation after(scratchArena);
    RunClassify(device, reference, testCase, true, 23.0f, scratchArena, full);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    RunClassify(device, reference, testCase, false, 31.0f, scratchArena, before);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    RunClassify(device, candidate, testCase, false, 19.0f, scratchArena, after);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    ASSERT_EQ(before.pixels.size(), after.pixels.size());
    for(usize index = 0u; index < after.pixels.size(); ++index)
        EXPECT_EQ(after.pixels[index], before.pixels[index]) << "Surface/pixel/channel word " << index;
    for(u32 index = 0u; index < LengthOf(after.counters); ++index)
        EXPECT_EQ(after.counters[index], before.counters[index]) << "Counter byte offset " << index * sizeof(u32);
    ASSERT_EQ(before.feedback.size(), after.feedback.size());
    for(usize index = 0u; index < after.feedback.size(); ++index)
        EXPECT_EQ(after.feedback[index], before.feedback[index]) << "Feedback word " << index;
    CheckQueue(testCase, full, before, scratchArena);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    CheckQueue(testCase, full, after, scratchArena);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    for(u32 word = 0u; word < 4u; ++word)
        EXPECT_EQ(after.feedback[word], s_FeedbackSentinel);
    for(usize word = after.feedback.size() - s_GuardWords; word < after.feedback.size(); ++word)
        EXPECT_EQ(after.feedback[word], s_FeedbackSentinel);
    const u32 pixelCount = testCase.width * testCase.height;
    if(testCase.mode == NWB_REFLECTION_MODE_DISABLED){
        for(const u16 value : after.pixels)
            EXPECT_EQ(value, 0u);
        EXPECT_EQ(after.counters[NWB_REFLECTION_COUNTER_CANDIDATES / sizeof(u32)], 0u);
    }
    if(testCase.mode == NWB_REFLECTION_MODE_HARDWARE && testCase.hardware != 0u && testCase.pattern == Pattern::Dense){
        EXPECT_EQ(after.counters[NWB_REFLECTION_COUNTER_CANDIDATES / sizeof(u32)], pixelCount * 2u);
        for(u32 pixel = 0u; pixel < pixelCount * 2u; ++pixel)
            EXPECT_EQ(after.pixels[pixel * 4u + 3u], ConvertFloatToHalf(1.0f));
    }
    if(testCase.diagnostics != 0u && testCase.mode == NWB_REFLECTION_MODE_SCREEN
        && testCase.pattern == Pattern::ScreenCrossings && testCase.width > 8u){
        EXPECT_GT(after.counters[NWB_REFLECTION_COUNTER_SCREEN_ATTEMPTS / sizeof(u32)], 0u);
        EXPECT_GT(after.counters[NWB_REFLECTION_COUNTER_SCREEN_ITERATIONS_LOW / sizeof(u32)], 0u);
        EXPECT_GT(after.counters[NWB_REFLECTION_COUNTER_SCREEN_RETURNS / sizeof(u32)], 0u);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(ReflectionKernelTest, ClassifyPreservesOutputsFeedbackAndCanonicalQueueBlocks){
    using namespace __hidden_classify_kernel_tests;
    Alloc::ScratchArena scratchArena(Name("tests/smoke/reflection_kernel/classify"));
    ComputePipelineHandle candidate;
    ComputePipelineHandle reference;
    const Path sourceRoot(arena(), NWB_REFLECTION_KERNEL_SOURCE_ROOT);
    const Path referenceSource = sourceRoot / "tests/smoke/reflection_kernel/reference/classify_cs.slang";
    ASSERT_TRUE(loadKernel("classify_cs", sizeof(u32), scratchArena, candidate));
    ASSERT_TRUE(loadKernel("classify_cs", sizeof(u32), scratchArena, reference, &referenceSource));
    const u32 dimensions[][2] = { { 1u, 1u }, { 8u, 8u }, { 13u, 9u }, { 24u, 16u } };
    for(const auto& extent : dimensions){
        for(u32 mode = NWB_REFLECTION_MODE_DISABLED; mode <= NWB_REFLECTION_MODE_HYBRID; ++mode){
            Case testCase;
            testCase.width = extent[0];
            testCase.height = extent[1];
            testCase.mode = mode;
            testCase.pattern = mode == NWB_REFLECTION_MODE_SCREEN ? Pattern::ScreenCrossings : Pattern::Dense;
            CompareCase(device(), *reference, *candidate, testCase, scratchArena);
            ASSERT_FALSE(HasFatalFailure());
        }
    }
    constexpr u32 budgets[] = { 0u, 1u, 31u, 32u, 63u, 64u, 65u, 127u, 128u, 129u, 233u };
    for(const u32 budget : budgets){
        Case testCase;
        testCase.width = 24u;
        testCase.height = 16u;
        testCase.budget = budget;
        CompareCase(device(), *reference, *candidate, testCase, scratchArena);
        ASSERT_FALSE(HasFatalFailure());
        testCase.capacity = budget;
        testCase.budget = 0xffffffffu;
        CompareCase(device(), *reference, *candidate, testCase, scratchArena);
        ASSERT_FALSE(HasFatalFailure());
    }
    for(u32 feedback = Feedback::Active; feedback <= Feedback::BoundExceedsBudget; ++feedback){
        Case testCase;
        testCase.width = 13u;
        testCase.height = 9u;
        testCase.mode = NWB_REFLECTION_MODE_HYBRID;
        testCase.feedback = static_cast<Feedback::Enum>(feedback);
        testCase.screenSteps = 0u;
        CompareCase(device(), *reference, *candidate, testCase, scratchArena);
        ASSERT_FALSE(HasFatalFailure());
    }
    for(u32 probe = 0u; probe <= NWB_REFLECTION_FEEDBACK_PROBE_PERIOD; ++probe){
        Case testCase;
        testCase.width = 13u;
        testCase.height = 9u;
        testCase.mode = NWB_REFLECTION_MODE_HYBRID;
        testCase.feedback = Feedback::Dormant;
        testCase.probeIndex = probe;
        CompareCase(device(), *reference, *candidate, testCase, scratchArena);
        ASSERT_FALSE(HasFatalFailure());
    }
    for(u32 mode = NWB_REFLECTION_MODE_DISABLED; mode <= NWB_REFLECTION_MODE_HYBRID; ++mode){
        Case testCase;
        testCase.width = 13u;
        testCase.height = 9u;
        testCase.mode = mode;
        testCase.hardware = 0u;
        testCase.diagnostics = 0u;
        CompareCase(device(), *reference, *candidate, testCase, scratchArena);
        ASSERT_FALSE(HasFatalFailure());
    }
    for(u32 capacityLimited = 0u; capacityLimited <= 1u; ++capacityLimited){
        Case testCase;
        testCase.width = 13u;
        testCase.height = 9u;
        testCase.mode = NWB_REFLECTION_MODE_HYBRID;
        testCase.feedback = Feedback::MissingPrevious;
        testCase.screenSteps = 0u;
        testCase.budget = capacityLimited == 0u ? 0u : 0xffffffffu;
        testCase.capacity = capacityLimited != 0u ? 0u : 0xffffffffu;
        CompareCase(device(), *reference, *candidate, testCase, scratchArena);
        ASSERT_FALSE(HasFatalFailure());
    }
    for(u32 pattern = Pattern::Invalid; pattern <= Pattern::Rough; ++pattern){
        for(u32 diagnostics = 0u; diagnostics <= 1u; ++diagnostics){
            Case testCase;
            testCase.width = 13u;
            testCase.height = 9u;
            testCase.pattern = static_cast<Pattern::Enum>(pattern);
            testCase.diagnostics = diagnostics;
            CompareCase(device(), *reference, *candidate, testCase, scratchArena);
            ASSERT_FALSE(HasFatalFailure());
            testCase.hardware = 0u;
            CompareCase(device(), *reference, *candidate, testCase, scratchArena);
            ASSERT_FALSE(HasFatalFailure());
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

