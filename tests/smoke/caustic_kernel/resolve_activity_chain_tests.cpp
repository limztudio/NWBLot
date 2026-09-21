// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "caustic_kernel_fixture.h"

#include <impl/assets/graphics/caustic/resolve_binding_slots.h>

#include <global/math/convert.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


constexpr u32 s_ExpectedDualCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_caustic_activity_chain_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ResolvePushConstants{
    u32 width;
    u32 height;
    u32 halfWidth;
    u32 halfHeight;
    f32 causticIntensity;
    u32 stepWidth;
    u32 stage;
    u32 pad;
    u32 worldPositionSlot;
    u32 depthSlot;
    u32 inputColorSlot;
    u32 geometrySlot;
    u32 accumulatorSlot;
    u32 outputStorageSlot;
    u32 activityInputSlot;
    u32 activityOutputSlot;
};

struct HalfPixel{
    u16 r;
    u16 g;
    u16 b;
    u16 a;
};

namespace Pattern{
    enum Enum : u8{
        Zero,
        Impulse,
        GroupEdges,
        Dense,
        InvalidGeometry,
        SignedAndNonfinite,
        Underflow,
    };
};

static constexpr u32 s_Stages = 3u;
static constexpr u32 s_GuardWords = 4u;
static constexpr u32 s_Guard = 0xbadc0ffeu;
static_assert(sizeof(ResolvePushConstants) == 64u);
static_assert(offsetof(ResolvePushConstants, activityInputSlot) == 56u);
static_assert(offsetof(ResolvePushConstants, activityOutputSlot) == 60u);
static_assert(sizeof(HalfPixel) == 8u);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] HalfPixel Pixel(const f32 r, const f32 g, const f32 b, const f32 a){
    return { ConvertFloatToHalf(r), ConvertFloatToHalf(g), ConvertFloatToHalf(b), ConvertFloatToHalf(a) };
}

[[nodiscard]] bool Active(const HalfPixel value){
    // Both signs of zero are inactive. Every other half encoding, including infinity and NaN, is active.
    return ((value.r | value.g | value.b) & 0x7fffu) != 0u;
}

void FillInputs(
    const u32 width,
    const u32 height,
    const Pattern::Enum pattern,
    Vector<HalfPixel, Alloc::ScratchArena>& colors,
    Vector<HalfPixel, Alloc::ScratchArena>& geometry){
    colors.clear();
    geometry.clear();
    const u32 impulseX = width > 64u ? 64u : width - 1u;
    const u32 impulseY = height > 64u ? 64u : height - 1u;
    for(u32 y = 0u; y < height; ++y){
        for(u32 x = 0u; x < width; ++x){
            HalfPixel color = Pixel(0.0f, 0.0f, 0.0f, 7.0f);
            HalfPixel world = Pixel(2.0f, 1.0f, 3.0f, 1.0f);
            const bool impulse = x == impulseX && y == impulseY;
            const bool boundary = (x == 7u || x == 8u || x + 1u == width)
                && (y == 7u || y == 8u || y + 1u == height);
            if(pattern == Pattern::Dense || pattern == Pattern::InvalidGeometry || pattern == Pattern::SignedAndNonfinite){
                const f32 value = static_cast<f32>(1u + (x + 3u * y) % 7u) * 0.25f;
                color = Pixel(value, value * 0.5f, value * 0.25f, 7.0f);
            }
            else if((pattern == Pattern::Impulse && impulse) || (pattern == Pattern::GroupEdges && boundary))
                color = Pixel(128.0f, 64.0f, 32.0f, 7.0f);
            else if(pattern == Pattern::Underflow && impulse)
                color = { 1u, s_ExpectedDualCount, 3u, ConvertFloatToHalf(7.0f) };
            if(pattern == Pattern::InvalidGeometry && (x + 3u * y) % 5u == 0u){
                world = Pixel(32.0f, 16.0f, 8.0f, 0.0f);
                // Invalid samples retain arbitrary coordinates, which still affect the right-neighbor spacing.
                if(x == 0u && y == 0u)
                    world.r = 0x7e00u;
            }
            if(pattern == Pattern::SignedAndNonfinite){
                if((x + s_ExpectedDualCount * y) % 3u == 0u)
                    color.r = ConvertFloatToHalf(-1.0f);
                if(x == width / s_ExpectedDualCount && y == height / s_ExpectedDualCount){
                    color.r = 0x7e00u;
                    color.g = 0x7c00u;
                    color.b = 0xfc00u;
                }
            }
            colors.push_back(color);
            geometry.push_back(world);
        }
    }
}

void RunChain(
    GraphicsBackend::Device& device,
    ComputePipeline& reference,
    ComputePipeline& tiled,
    ComputePipeline& direct,
    const u32 width,
    const u32 height,
    const bool inputMapAvailable,
    Alloc::ScratchArena& scratchArena){
    SCOPED_TRACE(width);
    SCOPED_TRACE(height);
    SCOPED_TRACE(inputMapAvailable);
    const u32 tilesX = DivideUp(width, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));
    const u32 tilesY = DivideUp(height, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));
    const u32 tileCount = tilesX * tilesY;
    const usize maskBytes = (static_cast<usize>(tileCount) + s_GuardWords) * sizeof(u32);
    const usize pixels = static_cast<usize>(width) * height;
    Vector<HalfPixel, Alloc::ScratchArena> colors(scratchArena);
    Vector<HalfPixel, Alloc::ScratchArena> geometry(scratchArena);
    colors.reserve(pixels);
    geometry.reserve(pixels);
    Vector<u32, Alloc::ScratchArena> initialFlags(scratchArena);
    initialFlags.resize(tileCount + s_GuardWords, s_Guard);
    Vector<u32, Alloc::ScratchArena> expectedFlags(scratchArena);
    expectedFlags.resize(tileCount);

    TextureDesc sourceDesc;
    sourceDesc
        .setWidth(width)
        .setHeight(height)
        .setFormat(Format::RGBA16_FLOAT)
        .setInitialState(ResourceStates::Common)
        .setKeepInitialState(true)
    ;
    const TextureHandle sources[] = { device.createTexture(sourceDesc), device.createTexture(sourceDesc) };
    TextureDesc outputDesc = sourceDesc;
    outputDesc
        .setWidth(width + s_ExpectedDualCount)
        .setHeight(height + s_ExpectedDualCount)
        .setInUAV(true)
    ;
    TextureHandle outputs[4];
    StagingTextureHandle readbacks[2][s_Stages];
    for(auto& output : outputs){
        output = device.createTexture(outputDesc);
        ASSERT_TRUE(output);
    }
    for(auto& arm : readbacks){
        for(auto& readback : arm){
            readback = device.createStagingTexture(outputDesc, CpuAccessMode::Read);
            ASSERT_TRUE(readback);
        }
    }
    TextureDesc accumulatorDesc = sourceDesc;
    accumulatorDesc
        .setWidth(1u)
        .setHeight(1u)
        .setDimension(TextureDimension::Texture2DArray)
        .setArraySize(NWB_CAUSTIC_ACCUMULATOR_CHANNEL_COUNT)
        .setFormat(Format::R32_UINT)
    ;
    const TextureHandle accumulator = device.createTexture(accumulatorDesc);
    ASSERT_TRUE(accumulator);
    BufferDesc maskDesc;
    maskDesc
        .setByteSize(maskBytes)
        .setCanHaveRawViews(true)
        .setCanHaveUAVs(true)
        .setCpuAccess(CpuAccessMode::Read)
        .setInitialState(ResourceStates::Common)
        .setKeepInitialState(true)
    ;
    const BufferHandle masks[] = { device.createBuffer(maskDesc), device.createBuffer(maskDesc) };
    auto& heap = device.getDescriptorHeap();
    // Sources 0..1; accumulator 2; four ping-pong SRVs 3..6 and UAVs 7..10; two activity maps 11..12.
    GpuDescriptorHandle descriptors[13]{};
    ScopeExit releaseDescriptors([&]()noexcept{
        for(const auto descriptor : descriptors){
            if(descriptor.valid())
                heap.free(descriptor);
        }
        heap.collectRetired();
    });
    for(u32 index = 0u; index < LengthOf(sources); ++index){
        ASSERT_TRUE(sources[index]);
        descriptors[index] = heap.allocate(GpuDescriptorClass::SampledImage);
        ASSERT_TRUE(descriptors[index].valid());
        ASSERT_TRUE(heap.write(descriptors[index], DescriptorWriteItem::Texture_SRV(0u, sources[index].get())));
    }
    descriptors[2] = heap.allocate(GpuDescriptorClass::SampledImage2DArrayUint);
    ASSERT_TRUE(descriptors[2].valid());
    ASSERT_TRUE(heap.write(descriptors[2], DescriptorWriteItem::Texture_SRV(0u, accumulator.get())));
    for(u32 index = 0u; index < LengthOf(outputs); ++index){
        descriptors[3u + index] = heap.allocate(GpuDescriptorClass::SampledImage);
        descriptors[7u + index] = heap.allocate(GpuDescriptorClass::StorageImage);
        ASSERT_TRUE(descriptors[3u + index].valid());
        ASSERT_TRUE(descriptors[7u + index].valid());
        ASSERT_TRUE(heap.write(descriptors[3u + index], DescriptorWriteItem::Texture_SRV(0u, outputs[index].get())));
        ASSERT_TRUE(heap.write(descriptors[7u + index], DescriptorWriteItem::Texture_UAV(0u, outputs[index].get())));
    }
    for(u32 index = 0u; index < LengthOf(masks); ++index){
        ASSERT_TRUE(masks[index]);
        descriptors[11u + index] = heap.allocate(GpuDescriptorClass::StorageBuffer);
        ASSERT_TRUE(descriptors[11u + index].valid());
        ASSERT_TRUE(heap.write(descriptors[11u + index], DescriptorWriteItem::RawBuffer_UAV(0u, masks[index].get())));
    }

    // Reuse the same images/maps across frames: no mask clear can conceal stale one or zero flags.
    const Pattern::Enum patterns[] = {
        Pattern::Zero, Pattern::Impulse, Pattern::Zero, Pattern::Dense, Pattern::Zero,
        Pattern::GroupEdges, Pattern::InvalidGeometry, Pattern::SignedAndNonfinite, Pattern::Underflow, Pattern::Zero,
    };
    for(u32 frame = 0u; frame < LengthOf(patterns); ++frame){
        const auto pattern = patterns[frame];
        SCOPED_TRACE(frame);
        SCOPED_TRACE(static_cast<u32>(pattern));
        FillInputs(width, height, pattern, colors, geometry);
        const CommandListHandle commands = device.createCommandList();
        ASSERT_TRUE(commands);
        commands->open();
        ASSERT_TRUE(commands->tryWriteTexture(*sources[0], 0u, 0u, colors.data(), width * sizeof(HalfPixel)));
        ASSERT_TRUE(commands->tryWriteTexture(*sources[1], 0u, 0u, geometry.data(), width * sizeof(HalfPixel)));
        for(const auto& source : sources)
            commands->setTextureState(source.get(), s_AllSubresources, ResourceStates::ShaderResource);
        if(frame == 0u){
            const u32 zero = 0u;
            for(u32 layer = 0u; layer < NWB_CAUSTIC_ACCUMULATOR_CHANNEL_COUNT; ++layer)
                ASSERT_TRUE(commands->tryWriteTexture(*accumulator, layer, 0u, &zero, sizeof(zero)));
            for(const auto& mask : masks)
                ASSERT_TRUE(commands->tryWriteBuffer(*mask, initialFlags.data(), maskBytes, 0u));
        }
        commands->setTextureState(accumulator.get(), s_AllSubresources, ResourceStates::ShaderResource);
        for(const auto& output : outputs)
            commands->clearTextureFloat(*output, s_AllSubresources, Color(19.0f));
        for(u32 arm = 0u; arm < s_ExpectedDualCount; ++arm){
            for(u32 stage = 0u; stage < s_Stages; ++stage){
                const u32 outputIndex = arm * s_ExpectedDualCount + stage % s_ExpectedDualCount;
                const u32 inputIndex = arm * s_ExpectedDualCount + (stage + 1u) % s_ExpectedDualCount;
                if(stage != 0u)
                    commands->setTextureState(outputs[inputIndex].get(), s_AllSubresources, ResourceStates::ShaderResource);
                commands->setTextureState(outputs[outputIndex].get(), s_AllSubresources, ResourceStates::UnorderedAccess, true);
                if(arm == 1u){
                    if(stage != 0u)
                        commands->setBufferState(masks[stage - 1u].get(), ResourceStates::ShaderResource);
                    if(stage != s_ExpectedDualCount)
                        commands->setBufferState(masks[stage].get(), ResourceStates::UnorderedAccess, true);
                }
                commands->commitBarriers();
                ComputePipeline& pipeline = arm == 0u ? reference : stage == 0u ? tiled : direct;
                ComputeState state;
                state.setPipeline(&pipeline);
                commands->setComputeState(state);
                heap.bindCompute(*commands, pipeline);
                const ResolvePushConstants push{
                    width * s_ExpectedDualCount, height * s_ExpectedDualCount, width, height, 1.0f, 4u << stage,
                    NWB_CAUSTIC_RESOLVE_STAGE_WAVELET, 0u,
                    descriptors[1].slot(), descriptors[1].slot(),
                    stage == 0u ? descriptors[0].slot() : descriptors[3u + inputIndex].slot(),
                    descriptors[1].slot(), descriptors[2].slot(), descriptors[7u + outputIndex].slot(),
                    arm == 1u && stage != 0u && inputMapAvailable
                        ? descriptors[10u + stage].slot() : NWB_CAUSTIC_RESOLVE_ACTIVITY_INVALID_SLOT,
                    arm == 1u && stage != s_ExpectedDualCount ? descriptors[11u + stage].slot() : NWB_CAUSTIC_RESOLVE_ACTIVITY_INVALID_SLOT,
                };
                commands->setPushConstants(&push, sizeof(push));
                commands->dispatch(tilesX, tilesY, 1u);
                commands->copyTexture(*readbacks[arm][stage], TextureSlice{}, *outputs[outputIndex], TextureSlice{});
            }
        }
        ASSERT_FALSE(commands->commandRecordingFailed());
        commands->close();
        ASSERT_FALSE(commands->commandRecordingFailed());
        CommandList* const lists[] = { commands.get() };
        const auto token = device.executeCommandLists(lists, LengthOf(lists), commands->getDescription().physicalQueue, QueueSubmissionDesc{});
        ASSERT_TRUE(token.valid());
        ASSERT_TRUE(device.waitForIdle());
        const u32* mappedMasks[2]{};
        ScopeExit unmapMasks([&]()noexcept{
            for(u32 index = 0u; index < LengthOf(masks); ++index){
                if(mappedMasks[index])
                    device.unmapBuffer(*masks[index]);
            }
        });
        for(u32 index = 0u; index < LengthOf(masks); ++index){
            mappedMasks[index] = static_cast<const u32*>(device.mapBuffer(*masks[index], CpuAccessMode::Read));
            ASSERT_NE(mappedMasks[index], nullptr);
            for(u32 guard = 0u; guard < s_GuardWords; ++guard)
                EXPECT_EQ(mappedMasks[index][tileCount + guard], s_Guard);
        }
        for(u32 stage = 0u; stage < s_Stages; ++stage){
            SCOPED_TRACE(stage);
            usize pitches[2]{};
            const u8* mapped[2]{};
            ScopeExit unmapImages([&]()noexcept{
                for(u32 arm = 0u; arm < s_ExpectedDualCount; ++arm){
                    if(mapped[arm])
                        device.unmapStagingTexture(*readbacks[arm][stage]);
                }
            });
            for(u32 arm = 0u; arm < s_ExpectedDualCount; ++arm){
                mapped[arm] = static_cast<const u8*>(device.mapStagingTexture(*readbacks[arm][stage], TextureSlice{}, CpuAccessMode::Read, &pitches[arm]));
                ASSERT_NE(mapped[arm], nullptr);
                ASSERT_GE(pitches[arm], (width + s_ExpectedDualCount) * sizeof(HalfPixel));
            }
            for(auto& flag : expectedFlags)
                flag = 0u;
            usize activePixels = 0u;
            for(u32 y = 0u; y < height + s_ExpectedDualCount; ++y){
                for(u32 x = 0u; x < width + s_ExpectedDualCount; ++x){
                    HalfPixel actual[2];
                    for(u32 arm = 0u; arm < s_ExpectedDualCount; ++arm)
                        NWB_MEMCPY(&actual[arm], sizeof(HalfPixel), mapped[arm] + y * pitches[arm] + x * sizeof(HalfPixel), sizeof(HalfPixel));
                    EXPECT_EQ(actual[1].r, actual[0].r) << x << "," << y;
                    EXPECT_EQ(actual[1].g, actual[0].g) << x << "," << y;
                    EXPECT_EQ(actual[1].b, actual[0].b) << x << "," << y;
                    EXPECT_EQ(actual[1].a, actual[0].a) << x << "," << y;
                    if(x >= width || y >= height){
                        const auto guard = Pixel(19.0f, 19.0f, 19.0f, 19.0f);
                        EXPECT_EQ(NWB_MEMCMP(&actual[1], &guard, sizeof(HalfPixel)), 0);
                        continue;
                    }
                    EXPECT_EQ(actual[1].a, ConvertFloatToHalf(1.0f));
                    if(Active(actual[1])){
                        ++activePixels;
                        expectedFlags[(y / 8u) * tilesX + x / 8u] = 1u;
                    }
                    if(pattern == Pattern::Zero){
                        EXPECT_EQ(actual[1].r, 0u);
                        EXPECT_EQ(actual[1].g, 0u);
                        EXPECT_EQ(actual[1].b, 0u);
                    }
                    // Three maximum-offset taps connect (64,64) to (8,8): 8 + 16 + 32 pixels.
                    if(pattern == Pattern::Impulse && width > 64u && height > 64u && stage == s_ExpectedDualCount && x == 8u && y == 8u)
                        EXPECT_GT(ConvertHalfToFloat(actual[1].r), 0.0f);
                }
            }
            if(pattern == Pattern::Impulse || pattern == Pattern::Dense || pattern == Pattern::GroupEdges)
                EXPECT_GT(activePixels, 0u);
            if(stage != s_ExpectedDualCount){
                usize inactiveTiles = 0u;
                for(u32 index = 0u; index < tileCount; ++index){
                    EXPECT_EQ(mappedMasks[stage][index], expectedFlags[index]) << "tile " << index;
                    inactiveTiles += expectedFlags[index] == 0u ? 1u : 0u;
                }
                if(pattern == Pattern::Zero)
                    EXPECT_EQ(inactiveTiles, tileCount);
                if(pattern == Pattern::Impulse && width > 64u && height > 64u)
                    EXPECT_GT(inactiveTiles, 0u);
            }
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(CausticKernelTest, ActivityMapsPreserveEveryQuantizedPassAndRefreshAcrossFrames){
    using namespace __hidden_caustic_activity_chain_tests;
    Alloc::ScratchArena scratchArena(Name("tests/smoke/caustic_kernel/activity_chain"));
    ComputePipelineHandle reference;
    ComputePipelineHandle tiled;
    ComputePipelineHandle direct;
    ASSERT_TRUE(loadResolveKernel(true, NWB_CAUSTIC_RESOLVE_STAGE_WAVELET, scratchArena, reference));
    ASSERT_TRUE(loadResolveKernel(false, NWB_CAUSTIC_RESOLVE_STAGE_WAVELET, scratchArena, tiled));
    ASSERT_TRUE(loadResolveKernel(false, NWB_CAUSTIC_RESOLVE_STAGE_WAVELET, scratchArena, direct, true));
    const u32 extents[][2] = { { 1u, 1u }, { 9u, 7u }, { 17u, 19u }, { 81u, 73u } };
    for(const auto& extent : extents)
        RunChain(device(), *reference, *tiled, *direct, extent[0], extent[1], true, scratchArena);
    RunChain(device(), *reference, *tiled, *direct, 81u, 73u, false, scratchArena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

