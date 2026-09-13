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
namespace __hidden_caustic_resolve_kernel_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// This narrow native fixture mirrors only the production 56-byte selector ABI, not the wavelet implementation.
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
};

struct HalfPixel{
    u16 r;
    u16 g;
    u16 b;
    u16 a;
};

struct Observation{
    f32 referenceCenterRed = 0.0f;
    f32 candidateCenterRed = 0.0f;
};

namespace Pattern{
    enum Enum : u8{
        Uniform,
        Dense,
        MixedInvalid,
        Zero,
        AllInvalid,
        InvalidRightNear,
        InvalidRightFar,
    };
};

static_assert(sizeof(ResolvePushConstants) == 56u);
static_assert(offsetof(ResolvePushConstants, causticIntensity) == 16u);
static_assert(offsetof(ResolvePushConstants, geometrySlot) == 44u);
static_assert(offsetof(ResolvePushConstants, outputStorageSlot) == 52u);
static_assert(sizeof(HalfPixel) == 8u);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] HalfPixel Pixel(const f32 r, const f32 g, const f32 b, const f32 a){
    return { ConvertFloatToHalf(r), ConvertFloatToHalf(g), ConvertFloatToHalf(b), ConvertFloatToHalf(a) };
}

void RunResolveCase(
    GraphicsBackend::Device& device,
    ComputePipeline& referencePipeline,
    ComputePipeline& candidatePipeline,
    const u32 width,
    const u32 height,
    const u32 step,
    const Pattern::Enum pattern,
    Alloc::ScratchArena& scratchArena,
    Observation& observation){
    SCOPED_TRACE(width);
    SCOPED_TRACE(height);
    SCOPED_TRACE(step);
    SCOPED_TRACE(static_cast<u32>(pattern));
    const usize pixelCount = static_cast<usize>(width) * height;
    const bool invalidRightCase = pattern == Pattern::InvalidRightNear || pattern == Pattern::InvalidRightFar;
    ASSERT_TRUE(!invalidRightCase || width >= step + 2u);
    const u32 centerX = invalidRightCase ? step : width / 2u;
    const u32 centerY = height / 2u;
    Vector<HalfPixel, Alloc::ScratchArena> colors(scratchArena);
    Vector<HalfPixel, Alloc::ScratchArena> geometry(scratchArena);
    colors.reserve(pixelCount);
    geometry.reserve(pixelCount);
    for(u32 y = 0u; y < height; ++y){
        for(u32 x = 0u; x < width; ++x){
            HalfPixel color = (x + 2u * y) % 3u == 0u ? Pixel(0.0f, 0.0f, 0.0f, 7.0f) : Pixel(0.5f, 0.25f, 1.0f, 7.0f);
            HalfPixel world = Pixel(static_cast<f32>(x) * 0.25f, static_cast<f32>(y) * 0.25f, 2.0f, 1.0f);
            if(pattern == Pattern::Uniform){
                color = Pixel(1.0f, 0.5f, 0.25f, 7.0f);
                world = Pixel(2.0f, 1.0f, 3.0f, 1.0f);
            }
            else if(pattern == Pattern::Zero)
                color = Pixel(0.0f, 0.0f, 0.0f, 7.0f);
            else if(pattern == Pattern::AllInvalid || (pattern == Pattern::MixedInvalid && (x + 3u * y) % 5u == 0u)){
                color = Pixel(0.5f, 0.25f, 1.0f, 7.0f);
                world = Pixel(32.0f + static_cast<f32>(x), 16.0f, 8.0f, 0.0f);
            }
            if(invalidRightCase){
                // Only two sites can contribute. The invalid right site's nonzero XYZ changes receiver spacing only.
                color = Pixel(0.0f, 0.0f, 0.0f, 7.0f);
                world = Pixel(32.0f, 16.0f, 8.0f, 0.0f);
                if(y == centerY && x == centerX)
                    world = Pixel(10.0f, 1.0f, 2.0f, 1.0f);
                else if(y == centerY && x == centerX - step){
                    color = Pixel(0.5f, 0.25f, 1.0f, 7.0f);
                    world = Pixel(10.5f, 1.0f, 2.0f, 1.0f);
                }
                else if(y == centerY && x == centerX + 1u){
                    const f32 rightX = pattern == Pattern::InvalidRightNear ? 10.015625f : 10.5f;
                    world = Pixel(rightX, 1.0f, 2.0f, 0.0f);
                }
            }
            colors.push_back(color);
            geometry.push_back(world);
        }
    }

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
    outputDesc.setInUAV(true);
    const TextureHandle outputs[] = { device.createTexture(outputDesc), device.createTexture(outputDesc) };
    const StagingTextureHandle readbacks[] = {
        device.createStagingTexture(outputDesc, CpuAccessMode::Read),
        device.createStagingTexture(outputDesc, CpuAccessMode::Read)
    };
    // Other stages are not dispatched; keep their typed accumulator selector valid without fabricating stage work.
    TextureDesc accumulatorDesc;
    accumulatorDesc
        .setWidth(1u)
        .setHeight(1u)
        .setDimension(TextureDimension::Texture2DArray)
        .setArraySize(NWB_CAUSTIC_ACCUMULATOR_CHANNEL_COUNT)
        .setFormat(Format::R32_UINT)
        .setInitialState(ResourceStates::Common)
        .setKeepInitialState(true)
    ;
    const TextureHandle accumulator = device.createTexture(accumulatorDesc);
    ASSERT_TRUE(accumulator);
    auto& heap = device.getDescriptorHeap();
    GpuDescriptorHandle descriptors[5]{};
    ScopeExit releaseDescriptors([&]()noexcept{
        for(const GpuDescriptorHandle handle : descriptors){
            if(handle.valid())
                heap.free(handle);
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
        ASSERT_TRUE(outputs[index]);
        ASSERT_TRUE(readbacks[index]);
        descriptors[3u + index] = heap.allocate(GpuDescriptorClass::StorageImage);
        ASSERT_TRUE(descriptors[3u + index].valid());
        ASSERT_TRUE(heap.write(descriptors[3u + index], DescriptorWriteItem::Texture_UAV(0u, outputs[index].get())));
    }
    const CommandListHandle commandList = device.createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
    const HalfPixel* const data[] = { colors.data(), geometry.data() };
    for(u32 index = 0u; index < LengthOf(sources); ++index){
        ASSERT_TRUE(commandList->tryWriteTexture(*sources[index], 0u, 0u, data[index], width * sizeof(HalfPixel)));
        commandList->setTextureState(sources[index].get(), s_AllSubresources, ResourceStates::ShaderResource);
    }
    const u32 accumulatorValue = 0u;
    for(u32 layer = 0u; layer < NWB_CAUSTIC_ACCUMULATOR_CHANNEL_COUNT; ++layer)
        ASSERT_TRUE(commandList->tryWriteTexture(*accumulator, layer, 0u, &accumulatorValue, sizeof(accumulatorValue)));
    commandList->setTextureState(accumulator.get(), s_AllSubresources, ResourceStates::ShaderResource);
    ComputePipeline* const pipelines[] = { &referencePipeline, &candidatePipeline };
    for(u32 index = 0u; index < LengthOf(outputs); ++index){
        commandList->clearTextureFloat(*outputs[index], s_AllSubresources, Color(index == 0u ? 31.0f : 19.0f));
        commandList->setTextureState(outputs[index].get(), s_AllSubresources, ResourceStates::UnorderedAccess, true);
        commandList->commitBarriers();
        ComputeState state;
        state.setPipeline(pipelines[index]);
        commandList->setComputeState(state);
        heap.bindCompute(*commandList, *pipelines[index]);
        const ResolvePushConstants push{
            width * 2u, height * 2u, width, height, 1.0f, step, NWB_CAUSTIC_RESOLVE_STAGE_WAVELET, 0u,
            descriptors[1].slot(), descriptors[1].slot(), descriptors[0].slot(), descriptors[1].slot(),
            descriptors[2].slot(), descriptors[3u + index].slot()
        };
        commandList->setPushConstants(&push, sizeof(push));
        commandList->dispatch(
            DivideUp(width, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE)),
            DivideUp(height, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE)), 1u
        );
        commandList->copyTexture(*readbacks[index], TextureSlice{}, *outputs[index], TextureSlice{});
    }
    ASSERT_FALSE(commandList->commandRecordingFailed());
    commandList->close();
    ASSERT_FALSE(commandList->commandRecordingFailed());
    CommandList* const commandLists[] = { commandList.get() };
    const QueueSubmissionToken token = device.executeCommandLists(
        commandLists, LengthOf(commandLists), commandList->getDescription().physicalQueue, QueueSubmissionDesc{}
    );
    ASSERT_TRUE(token.valid());
    ASSERT_TRUE(device.waitForIdle());
    usize pitches[2]{};
    const u8* mapped[2]{};
    ScopeExit unmap([&]()noexcept{
        for(u32 index = 0u; index < LengthOf(readbacks); ++index){
            if(mapped[index] != nullptr)
                device.unmapStagingTexture(*readbacks[index]);
        }
    });
    for(u32 index = 0u; index < LengthOf(readbacks); ++index){
        mapped[index] = static_cast<const u8*>(
            device.mapStagingTexture(*readbacks[index], TextureSlice{}, CpuAccessMode::Read, &pitches[index])
        );
        ASSERT_NE(mapped[index], nullptr);
        ASSERT_GE(pitches[index], width * sizeof(HalfPixel));
    }
    usize brightenedZeroPixels = 0u;
    for(u32 y = 0u; y < height; ++y){
        for(u32 x = 0u; x < width; ++x){
            const usize pixelIndex = static_cast<usize>(y) * width + x;
            HalfPixel reference;
            HalfPixel candidate;
            NWB_MEMCPY(&reference, sizeof(reference), mapped[0] + y * pitches[0] + x * sizeof(HalfPixel), sizeof(reference));
            NWB_MEMCPY(&candidate, sizeof(candidate), mapped[1] + y * pitches[1] + x * sizeof(HalfPixel), sizeof(candidate));
            EXPECT_EQ(candidate.r, reference.r) << "red " << x << "," << y;
            EXPECT_EQ(candidate.g, reference.g) << "green " << x << "," << y;
            EXPECT_EQ(candidate.b, reference.b) << "blue " << x << "," << y;
            EXPECT_EQ(candidate.a, reference.a) << "alpha " << x << "," << y;
            EXPECT_EQ(candidate.a, ConvertFloatToHalf(1.0f));
            EXPECT_TRUE(IsFinite(ConvertHalfToFloat(candidate.r)));
            EXPECT_TRUE(IsFinite(ConvertHalfToFloat(candidate.g)));
            EXPECT_TRUE(IsFinite(ConvertHalfToFloat(candidate.b)));
            if(pattern == Pattern::Zero || geometry[pixelIndex].a == 0u){
                EXPECT_EQ(candidate.r, 0u);
                EXPECT_EQ(candidate.g, 0u);
                EXPECT_EQ(candidate.b, 0u);
            }
            if(pattern == Pattern::Uniform){
                // Bound the auxiliary constant-color check by one adjacent half code after normalization.
                // The production/reference comparison above remains exact for every RGBA component.
                const HalfPixel expected = Pixel(1.0f, 0.5f, 0.25f, 1.0f);
                EXPECT_GE(candidate.r, expected.r - 1u);
                EXPECT_LE(candidate.r, expected.r + 1u);
                EXPECT_GE(candidate.g, expected.g - 1u);
                EXPECT_LE(candidate.g, expected.g + 1u);
                EXPECT_GE(candidate.b, expected.b - 1u);
                EXPECT_LE(candidate.b, expected.b + 1u);
                // Power-of-two channel ratios still have an exact float32 representation.
                EXPECT_EQ(ConvertHalfToFloat(candidate.r), 2.0f * ConvertHalfToFloat(candidate.g));
                EXPECT_EQ(ConvertHalfToFloat(candidate.r), 4.0f * ConvertHalfToFloat(candidate.b));
            }
            if(colors[pixelIndex].r == 0u && candidate.r != 0u)
                ++brightenedZeroPixels;
            if(x == centerX && y == centerY){
                observation.referenceCenterRed = ConvertHalfToFloat(reference.r);
                observation.candidateCenterRed = ConvertHalfToFloat(candidate.r);
            }
        }
    }
    // Independent activity prevents matching zero writes or bypassed filtering from satisfying the paired oracle.
    if(pattern == Pattern::Dense && width > 1u)
        EXPECT_GT(brightenedZeroPixels, 0u);
    if(pattern == Pattern::InvalidRightFar){
        EXPECT_GT(observation.referenceCenterRed, 0.01f);
        EXPECT_GT(observation.candidateCenterRed, 0.01f);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(CausticKernelTest, CookedWaveletMatchesFrozenProductionAcrossTilesAndInvalidGeometry){
    using namespace __hidden_caustic_resolve_kernel_tests;
    Alloc::ScratchArena scratchArena(Name("tests/smoke/caustic_kernel/wavelet"));
    ComputePipelineHandle candidate;
    ComputePipelineHandle reference;
    ASSERT_TRUE(loadResolveKernel(false, scratchArena, candidate));
    ASSERT_TRUE(loadResolveKernel(true, scratchArena, reference));
    const u32 dimensions[][2] = { { 1u, 1u }, { 7u, 9u }, { 8u, 8u }, { 9u, 7u }, { 9u, 9u } };
    const u32 steps[] = { 1u, 2u, 4u };
    for(const auto& dimension : dimensions){
        for(const u32 step : steps){
            for(u32 pattern = Pattern::Uniform; pattern <= Pattern::AllInvalid; ++pattern){
                Observation observation;
                RunResolveCase(
                    device(), *reference, *candidate, dimension[0], dimension[1], step,
                    static_cast<Pattern::Enum>(pattern), scratchArena, observation
                );
            }
        }
    }
}

TEST_F(CausticKernelTest, InvalidRightRawCoordinatesChangeActualSpacingWithoutJoiningTheFilter){
    using namespace __hidden_caustic_resolve_kernel_tests;
    Alloc::ScratchArena scratchArena(Name("tests/smoke/caustic_kernel/invalid_right"));
    ComputePipelineHandle candidate;
    ComputePipelineHandle reference;
    ASSERT_TRUE(loadResolveKernel(false, scratchArena, candidate));
    ASSERT_TRUE(loadResolveKernel(true, scratchArena, reference));
    const u32 dimensions[][2] = { { 7u, 9u }, { 8u, 8u }, { 9u, 7u }, { 9u, 9u } };
    const u32 steps[] = { 1u, 2u, 4u };
    for(const auto& dimension : dimensions){
        for(const u32 step : steps){
            Observation nearResult;
            Observation farResult;
            RunResolveCase(device(), *reference, *candidate, dimension[0], dimension[1], step, Pattern::InvalidRightNear, scratchArena, nearResult);
            RunResolveCase(device(), *reference, *candidate, dimension[0], dimension[1], step, Pattern::InvalidRightFar, scratchArena, farResult);
            // Only an invalid neighbor's stored XYZ differs: replacing it by zero must fail this observation.
            EXPECT_GT(farResult.referenceCenterRed - nearResult.referenceCenterRed, 0.01f);
            EXPECT_GT(farResult.candidateCenterRed - nearResult.candidateCenterRed, 0.01f);
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

