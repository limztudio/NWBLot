// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "caustic_kernel_fixture.h"

#include <impl/assets/graphics/caustic/resolve_binding_slots.h>

#include <global/algorithm.h>
#include <global/math/convert.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{
namespace __hidden_caustic_resolve_stage_tests{


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
};

struct HalfPixel{
    u16 r;
    u16 g;
    u16 b;
    u16 a;
};

namespace Pattern{
    enum Enum : u8{
        Uniform,
        Varying,
        MixedInvalid,
        AllInvalid,
        Zero,
    };
};

static_assert(sizeof(ResolvePushConstants) == 56u);
static_assert(offsetof(ResolvePushConstants, outputStorageSlot) == 52u);
static_assert(sizeof(HalfPixel) == 8u);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] HalfPixel Pixel(const f32 r, const f32 g, const f32 b, const f32 a){
    return { ConvertFloatToHalf(r), ConvertFloatToHalf(g), ConvertFloatToHalf(b), ConvertFloatToHalf(a) };
}

void RunStageCase(
    GraphicsBackend::Device& device,
    ComputePipeline& referencePipeline,
    ComputePipeline& candidatePipeline,
    const u32 width,
    const u32 height,
    const u32 stage,
    const Pattern::Enum pattern,
    Alloc::ScratchArena& scratchArena){
    SCOPED_TRACE(width);
    SCOPED_TRACE(height);
    SCOPED_TRACE(stage);
    SCOPED_TRACE(static_cast<u32>(pattern));
    const bool prepare = stage == NWB_CAUSTIC_RESOLVE_STAGE_PREPARE_DOWNSAMPLE;
    const u32 halfWidth = (width + 1u) / 2u;
    const u32 halfHeight = (height + 1u) / 2u;
    const u32 outputWidth = prepare ? halfWidth : width;
    const u32 outputHeight = prepare ? halfHeight : height;
    const usize fullPixels = static_cast<usize>(width) * height;
    const usize halfPixels = static_cast<usize>(halfWidth) * halfHeight;
    Vector<HalfPixel, Alloc::ScratchArena> world(scratchArena);
    Vector<f32, Alloc::ScratchArena> depth(scratchArena);
    Vector<HalfPixel, Alloc::ScratchArena> colors(scratchArena);
    Vector<HalfPixel, Alloc::ScratchArena> geometry(scratchArena);
    Vector<u32, Alloc::ScratchArena> flux(scratchArena);
    world.reserve(fullPixels);
    depth.reserve(fullPixels);
    colors.reserve(halfPixels);
    geometry.reserve(halfPixels);
    flux.reserve(fullPixels * NWB_CAUSTIC_ACCUMULATOR_CHANNEL_COUNT);
    for(u32 y = 0u; y < height; ++y){
        for(u32 x = 0u; x < width; ++x){
            world.push_back(Pixel(static_cast<f32>(x), static_cast<f32>(y), 2.0f, 1.0f));
            depth.push_back(pattern == Pattern::MixedInvalid && (x + 3u * y) % 5u == 0u ? 1.0f : 0.5f);
        }
    }
    for(u32 y = 0u; y < halfHeight; ++y){
        for(u32 x = 0u; x < halfWidth; ++x){
            const bool valid = pattern != Pattern::AllInvalid && (pattern != Pattern::MixedInvalid || (x + y) % 3u != 0u);
            const f32 value = pattern == Pattern::Zero ? 0.0f : pattern == Pattern::Uniform ? 1.0f : 0.25f * static_cast<f32>(1u + (x + 2u * y) % 5u);
            colors.push_back(Pixel(value, value * 0.5f, value * 0.25f, 7.0f));
            geometry.push_back(Pixel(static_cast<f32>(x * 2u), static_cast<f32>(y * 2u), 2.0f, valid ? 1.0f : 0.0f));
        }
    }
    for(u32 layer = 0u; layer < NWB_CAUSTIC_ACCUMULATOR_CHANNEL_COUNT; ++layer){
        for(u32 y = 0u; y < height; ++y){
            for(u32 x = 0u; x < width; ++x){
                const u32 value = pattern == Pattern::Zero ? 0u : 256u * (1u + (x + 2u * y) % 4u);
                flux.push_back(value * (1u << layer));
            }
        }
    }

    TextureDesc fullDesc;
    fullDesc.setWidth(width).setHeight(height).setFormat(Format::RGBA16_FLOAT)
        .setInitialState(ResourceStates::Common).setKeepInitialState(true);
    TextureDesc depthDesc = fullDesc;
    depthDesc.setFormat(Format::R32_FLOAT);
    TextureDesc halfDesc = fullDesc;
    halfDesc.setWidth(halfWidth).setHeight(halfHeight);
    const TextureHandle sources[] = {
        device.createTexture(fullDesc), device.createTexture(depthDesc),
        device.createTexture(halfDesc), device.createTexture(halfDesc)
    };
    TextureDesc accumulatorDesc = fullDesc;
    accumulatorDesc.setDimension(TextureDimension::Texture2DArray)
        .setArraySize(NWB_CAUSTIC_ACCUMULATOR_CHANNEL_COUNT).setFormat(Format::R32_UINT);
    const TextureHandle accumulator = device.createTexture(accumulatorDesc);
    ASSERT_TRUE(accumulator);
    TextureDesc outputDesc = fullDesc;
    outputDesc.setWidth(outputWidth + 2u).setHeight(outputHeight + 2u).setInUAV(true);
    const TextureHandle outputs[] = { device.createTexture(outputDesc), device.createTexture(outputDesc) };
    const StagingTextureHandle readbacks[] = {
        device.createStagingTexture(outputDesc, CpuAccessMode::Read),
        device.createStagingTexture(outputDesc, CpuAccessMode::Read)
    };
    auto& heap = device.getDescriptorHeap();
    GpuDescriptorHandle descriptors[7]{};
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
    descriptors[4] = heap.allocate(GpuDescriptorClass::SampledImage2DArrayUint);
    ASSERT_TRUE(descriptors[4].valid());
    ASSERT_TRUE(heap.write(descriptors[4], DescriptorWriteItem::Texture_SRV(0u, accumulator.get())));
    for(u32 index = 0u; index < LengthOf(outputs); ++index){
        ASSERT_TRUE(outputs[index]);
        ASSERT_TRUE(readbacks[index]);
        descriptors[5u + index] = heap.allocate(GpuDescriptorClass::StorageImage);
        ASSERT_TRUE(descriptors[5u + index].valid());
        ASSERT_TRUE(heap.write(descriptors[5u + index], DescriptorWriteItem::Texture_UAV(0u, outputs[index].get())));
    }
    const CommandListHandle commandList = device.createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
    const void* const data[] = { world.data(), depth.data(), colors.data(), geometry.data() };
    const usize rowBytes[] = { width * sizeof(HalfPixel), width * sizeof(f32), halfWidth * sizeof(HalfPixel), halfWidth * sizeof(HalfPixel) };
    for(u32 index = 0u; index < LengthOf(sources); ++index){
        ASSERT_TRUE(commandList->tryWriteTexture(*sources[index], 0u, 0u, data[index], rowBytes[index]));
        commandList->setTextureState(sources[index].get(), s_AllSubresources, ResourceStates::ShaderResource);
    }
    for(u32 layer = 0u; layer < NWB_CAUSTIC_ACCUMULATOR_CHANNEL_COUNT; ++layer)
        ASSERT_TRUE(commandList->tryWriteTexture(*accumulator, layer, 0u, flux.data() + fullPixels * layer, width * sizeof(u32)));
    commandList->setTextureState(accumulator.get(), s_AllSubresources, ResourceStates::ShaderResource);
    ComputePipeline* const pipelines[] = { &referencePipeline, &candidatePipeline };
    for(u32 index = 0u; index < LengthOf(outputs); ++index){
        commandList->clearTextureFloat(*outputs[index], s_AllSubresources, Color(19.0f));
        commandList->setTextureState(outputs[index].get(), s_AllSubresources, ResourceStates::UnorderedAccess, true);
        commandList->commitBarriers();
        ComputeState state;
        state.setPipeline(pipelines[index]);
        commandList->setComputeState(state);
        heap.bindCompute(*commandList, *pipelines[index]);
        const ResolvePushConstants push{
            width, height, halfWidth, halfHeight, 0.75f, 1u, stage, 0u,
            descriptors[0].slot(), descriptors[1].slot(), descriptors[2].slot(), descriptors[3].slot(),
            descriptors[4].slot(), descriptors[5u + index].slot()
        };
        commandList->setPushConstants(&push, sizeof(push));
        commandList->dispatch(
            DivideUp(outputWidth, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE)),
            DivideUp(outputHeight, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE)), 1u
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
        mapped[index] = static_cast<const u8*>(device.mapStagingTexture(*readbacks[index], TextureSlice{}, CpuAccessMode::Read, &pitches[index]));
        ASSERT_NE(mapped[index], nullptr);
        ASSERT_GE(pitches[index], (outputWidth + 2u) * sizeof(HalfPixel));
    }
    usize activePixels = 0u;
    for(u32 y = 0u; y < outputHeight + 2u; ++y){
        for(u32 x = 0u; x < outputWidth + 2u; ++x){
            HalfPixel reference;
            HalfPixel candidate;
            NWB_MEMCPY(&reference, sizeof(reference), mapped[0] + y * pitches[0] + x * sizeof(HalfPixel), sizeof(reference));
            NWB_MEMCPY(&candidate, sizeof(candidate), mapped[1] + y * pitches[1] + x * sizeof(HalfPixel), sizeof(candidate));
            EXPECT_EQ(candidate.r, reference.r) << x << "," << y;
            EXPECT_EQ(candidate.g, reference.g) << x << "," << y;
            EXPECT_EQ(candidate.b, reference.b) << x << "," << y;
            EXPECT_EQ(candidate.a, reference.a) << x << "," << y;
            if(x >= outputWidth || y >= outputHeight){
                EXPECT_EQ(candidate.r, ConvertFloatToHalf(19.0f));
                EXPECT_EQ(candidate.g, ConvertFloatToHalf(19.0f));
                EXPECT_EQ(candidate.b, ConvertFloatToHalf(19.0f));
                EXPECT_EQ(candidate.a, ConvertFloatToHalf(19.0f));
                continue;
            }
            EXPECT_EQ(candidate.a, ConvertFloatToHalf(1.0f));
            EXPECT_TRUE(IsFinite(ConvertHalfToFloat(candidate.r)));
            EXPECT_TRUE(IsFinite(ConvertHalfToFloat(candidate.g)));
            EXPECT_TRUE(IsFinite(ConvertHalfToFloat(candidate.b)));
            // At the last full pixel both forward differences clamp to zero; an even extent leaves no coincident half-grid tap, so the original edge weights underflow to zero.
            const bool lastPixelHasNoCoincidentTap = !prepare && x + 1u == width && y + 1u == height
                && ((width % 2u) == 0u || (height % 2u) == 0u);
            const bool expectedZero = pattern == Pattern::Zero || pattern == Pattern::AllInvalid || lastPixelHasNoCoincidentTap
                || (prepare ? geometry[static_cast<usize>(y) * halfWidth + x].a == 0u : depth[static_cast<usize>(y) * width + x] == 1.0f);
            if(expectedZero){
                EXPECT_EQ(candidate.r, 0u);
                EXPECT_EQ(candidate.g, 0u);
                EXPECT_EQ(candidate.b, 0u);
            }
            else if(prepare){
                // This regular receiver grid has area four except at the clamped right/bottom boundary.
                const f32 area = x + 1u < halfWidth && y + 1u < halfHeight ? 4.0f : 1e-4f;
                u32 sum = 0u;
                for(u32 oy = 0u; oy < 2u; ++oy){
                    for(u32 ox = 0u; ox < 2u; ++ox){
                        const u32 fx = Min(x * 2u + ox, width - 1u);
                        const u32 fy = Min(y * 2u + oy, height - 1u);
                        sum += flux[static_cast<usize>(fy) * width + fx];
                    }
                }
                const f32 expected = (static_cast<f32>(sum) / 1048576.0f) / area * 0.75f;
                EXPECT_EQ(candidate.r, ConvertFloatToHalf(expected));
                EXPECT_EQ(candidate.g, ConvertFloatToHalf(expected * 2.0f));
                EXPECT_EQ(candidate.b, ConvertFloatToHalf(expected * 4.0f));
            }
            else if(pattern == Pattern::Uniform){
                const HalfPixel expected = Pixel(1.0f, 0.5f, 0.25f, 1.0f);
                EXPECT_GE(candidate.r, expected.r - 1u);
                EXPECT_LE(candidate.r, expected.r + 1u);
                EXPECT_GE(candidate.g, expected.g - 1u);
                EXPECT_LE(candidate.g, expected.g + 1u);
                EXPECT_GE(candidate.b, expected.b - 1u);
                EXPECT_LE(candidate.b, expected.b + 1u);
            }
            if(candidate.r != 0u)
                ++activePixels;
        }
    }
    if(pattern == Pattern::Uniform || pattern == Pattern::Varying)
        EXPECT_GT(activePixels, 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(CausticKernelTest, DynamicPrepareAndCompiledUpsampleMatchFrozenProductionAndAnalyticSignals){
    using namespace __hidden_caustic_resolve_stage_tests;
    Alloc::ScratchArena scratchArena(Name("tests/smoke/caustic_kernel/stages"));
    ComputePipelineHandle reference;
    ASSERT_TRUE(loadResolveKernel(true, NWB_CAUSTIC_RESOLVE_STAGE_PREPARE_DOWNSAMPLE, scratchArena, reference));
    const u32 stages[] = { NWB_CAUSTIC_RESOLVE_STAGE_PREPARE_DOWNSAMPLE, NWB_CAUSTIC_RESOLVE_STAGE_UPSAMPLE };
    const u32 dimensions[][2] = { { 1u, 1u }, { 13u, 17u }, { 16u, 16u }, { 17u, 13u }, { 19u, 19u } };
    for(const u32 stage : stages){
        ComputePipelineHandle candidate;
        ASSERT_TRUE(loadResolveKernel(false, stage, scratchArena, candidate));
        for(const auto& dimension : dimensions){
            for(u32 pattern = Pattern::Uniform; pattern <= Pattern::Zero; ++pattern){
                RunStageCase(device(), *reference, *candidate, dimension[0], dimension[1], stage, static_cast<Pattern::Enum>(pattern), scratchArena);
            }
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

