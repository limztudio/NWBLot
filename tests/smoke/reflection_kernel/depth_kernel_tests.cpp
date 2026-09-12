// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "reflection_kernel_fixture.h"

#include <impl/assets/graphics/reflection/depth_constants.h>

#include <global/bit.h>
#include <global/not_null.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{
namespace __hidden_reflection_depth_kernel_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct DepthPushConstants{
#define NWB_REFLECTION_DEPTH_TEST_FIELD(name) u32 name;
    NWB_REFLECTION_DEPTH_UINT_FIELDS(NWB_REFLECTION_DEPTH_TEST_FIELD)
#undef NWB_REFLECTION_DEPTH_TEST_FIELD
};

struct DepthInterval{
    f32 minimum;
    f32 maximum;
};

static_assert(sizeof(DepthPushConstants) == NWB_REFLECTION_DEPTH_PUSH_CONSTANT_BYTES);
static_assert(sizeof(DepthInterval) == sizeof(f32) * 2u);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static f32 ValidDepth(const f32 value){
    const u32 exponent = BitCast<u32>(value) & 0x7f800000u;
    return exponent == 0x7f800000u || value < 0.0f ? 1.0f : Min(value, 1.0f);
}

static void RunDepthCase(
    GraphicsBackend::Device& device,
    ComputePipeline& pipeline,
    const u32 width,
    const u32 height,
    const NotNull<const f32*> input,
    const Format::Enum inputFormat,
    Alloc::ScratchArena& scratchArena,
    const DepthInterval* literalMipZero = nullptr){
    SCOPED_TRACE(width);
    SCOPED_TRACE(height);
    SCOPED_TRACE(static_cast<u32>(inputFormat));
    u32 mipCount = 1u;
    for(u32 extent = Max(width, height); extent > 1u; extent >>= 1u)
        ++mipCount;
    ASSERT_LE(mipCount, NWB_REFLECTION_MAX_DEPTH_MIPS);

    TextureDesc inputDesc;
    inputDesc
        .setWidth(width)
        .setHeight(height)
        .setFormat(inputFormat)
        .setInitialState(ResourceStates::Common)
        .setKeepInitialState(true)
    ;
    constexpr FormatSupport::Mask requiredSupport = FormatSupport::Texture | FormatSupport::ShaderUavStore;
    Format::Enum outputFormat = Format::RG32_FLOAT;
    if((device.queryFormatSupport(outputFormat) & requiredSupport) != requiredSupport)
        outputFormat = Format::RGBA32_FLOAT;
    ASSERT_EQ(device.queryFormatSupport(outputFormat) & requiredSupport, requiredSupport);
    const usize outputTexelBytes = outputFormat == Format::RG32_FLOAT ? sizeof(f32) * 2u : sizeof(f32) * 4u;
    TextureDesc outputDesc;
    outputDesc
        .setWidth(width)
        .setHeight(height)
        .setMipLevels(mipCount)
        .setFormat(outputFormat)
        .setInUAV(true)
        .setInitialState(ResourceStates::Common)
        .setKeepInitialState(true)
    ;
    const TextureHandle source = device.createTexture(inputDesc);
    const TextureHandle pyramid = device.createTexture(outputDesc);
    const StagingTextureHandle readback = device.createStagingTexture(outputDesc, CpuAccessMode::Read);
    ASSERT_TRUE(source);
    ASSERT_TRUE(pyramid);
    ASSERT_TRUE(readback);

    auto& heap = device.getDescriptorHeap();
    GpuDescriptorHandle descriptors[1u + 2u * NWB_REFLECTION_MAX_DEPTH_MIPS]{};
    ScopeExit releaseDescriptors([&]()noexcept{
        for(GpuDescriptorHandle handle : descriptors){
            if(handle.valid())
                heap.free(handle);
        }
        heap.collectRetired();
    });
    descriptors[0] = heap.allocate(GpuDescriptorClass::SampledImage);
    ASSERT_TRUE(descriptors[0].valid());
    ASSERT_TRUE(heap.write(descriptors[0], DescriptorWriteItem::Texture_SRV(0u, source.get())));
    for(u32 mip = 0u; mip < mipCount; ++mip){
        const TextureSubresourceSet subresources(mip, 1u, 0u, 1u);
        descriptors[1u + mip * 2u] = heap.allocate(GpuDescriptorClass::SampledImage);
        descriptors[2u + mip * 2u] = heap.allocate(GpuDescriptorClass::StorageImage);
        ASSERT_TRUE(descriptors[1u + mip * 2u].valid());
        ASSERT_TRUE(descriptors[2u + mip * 2u].valid());
        ASSERT_TRUE(heap.write(
            descriptors[1u + mip * 2u], DescriptorWriteItem::Texture_SRV(0u, pyramid.get(), outputFormat, subresources)
        ));
        ASSERT_TRUE(heap.write(
            descriptors[2u + mip * 2u], DescriptorWriteItem::Texture_UAV(0u, pyramid.get(), outputFormat, subresources)
        ));
    }

    const CommandListHandle commandList = device.createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
    ASSERT_TRUE(commandList->tryWriteTexture(*source, 0u, 0u, input.get(), static_cast<usize>(width) * sizeof(f32)));
    commandList->setTextureState(source.get(), s_AllSubresources, ResourceStates::ShaderResource);
    u32 sourceWidth = width;
    u32 sourceHeight = height;
    for(u32 mip = 0u; mip < mipCount; ++mip){
        const u32 mipWidth = Max(width >> mip, 1u);
        const u32 mipHeight = Max(height >> mip, 1u);
        if(mip != 0u){
            commandList->setTextureState(
                pyramid.get(), TextureSubresourceSet(mip - 1u, 1u, 0u, 1u), ResourceStates::ShaderResource
            );
        }
        commandList->setTextureState(pyramid.get(), TextureSubresourceSet(mip, 1u, 0u, 1u), ResourceStates::UnorderedAccess);
        commandList->commitBarriers();
        ComputeState state;
        state.setPipeline(&pipeline);
        commandList->setComputeState(state);
        heap.bindCompute(*commandList, pipeline);
        const DepthPushConstants push{
            mip == 0u ? descriptors[0].slot() : descriptors[1u + (mip - 1u) * 2u].slot(),
            descriptors[2u + mip * 2u].slot(),
            sourceWidth,
            sourceHeight,
            mipWidth,
            mipHeight
        };
        commandList->setPushConstants(&push, sizeof(push));
        commandList->dispatch(
            DivideUp(mipWidth, NWB_REFLECTION_DEPTH_GROUP_SIZE),
            DivideUp(mipHeight, NWB_REFLECTION_DEPTH_GROUP_SIZE),
            1u
        );
        TextureSlice slice;
        slice.setMipLevel(mip);
        commandList->copyTexture(*readback, slice, *pyramid, slice);
        sourceWidth = mipWidth;
        sourceHeight = mipHeight;
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

    // The oracle enumerates mathematical footprints directly; it has no workgroup, shared-memory or lane indexing.
    Vector<DepthInterval, Alloc::ScratchArena> previous(scratchArena);
    Vector<DepthInterval, Alloc::ScratchArena> expected(scratchArena);
    previous.reserve(static_cast<usize>(width) * height);
    expected.reserve(static_cast<usize>(width) * height);
    sourceWidth = width;
    sourceHeight = height;
    for(u32 mip = 0u; mip < mipCount; ++mip){
        SCOPED_TRACE(mip);
        const u32 mipWidth = Max(width >> mip, 1u);
        const u32 mipHeight = Max(height >> mip, 1u);
        expected.clear();
        for(u32 y = 0u; y < mipHeight; ++y){
            for(u32 x = 0u; x < mipWidth; ++x){
                DepthInterval interval{ 1.0f, 0.0f };
                if(mip == 0u){
                    const u32 minY = y == 0u ? 0u : y - 1u;
                    const u32 maxY = Min(y + 1u, height - 1u);
                    const u32 minX = x == 0u ? 0u : x - 1u;
                    const u32 maxX = Min(x + 1u, width - 1u);
                    for(u32 inputY = minY; inputY <= maxY; ++inputY){
                        for(u32 inputX = minX; inputX <= maxX; ++inputX){
                            const f32 depth = ValidDepth(input.get()[static_cast<usize>(inputY) * width + inputX]);
                            interval.minimum = Min(interval.minimum, depth);
                            interval.maximum = Max(interval.maximum, depth);
                        }
                    }
                }
                else{
                    const u32 beginX = sourceWidth == mipWidth ? x : x * 2u;
                    const u32 beginY = sourceHeight == mipHeight ? y : y * 2u;
                    const u32 endX = beginX + (sourceWidth == mipWidth ? 1u : 2u + (sourceWidth & 1u));
                    const u32 endY = beginY + (sourceHeight == mipHeight ? 1u : 2u + (sourceHeight & 1u));
                    for(u32 inputY = beginY; inputY < endY; ++inputY){
                        for(u32 inputX = beginX; inputX < endX; ++inputX){
                            const DepthInterval depth = previous[static_cast<usize>(inputY) * sourceWidth + inputX];
                            interval.minimum = Min(interval.minimum, depth.minimum);
                            interval.maximum = Max(interval.maximum, depth.maximum);
                        }
                    }
                }
                if(mip == 0u && literalMipZero != nullptr)
                    interval = literalMipZero[static_cast<usize>(y) * width + x];
                expected.push_back(interval);
            }
        }
        TextureSlice slice;
        slice.setMipLevel(mip);
        usize rowPitch = 0u;
        const u8* const mapped = static_cast<const u8*>(
            device.mapStagingTexture(*readback, slice, CpuAccessMode::Read, &rowPitch)
        );
        ASSERT_NE(mapped, nullptr);
        ScopeExit unmap([&]()noexcept{ device.unmapStagingTexture(*readback); });
        ASSERT_GE(rowPitch, static_cast<usize>(mipWidth) * outputTexelBytes);
        for(u32 y = 0u; y < mipHeight; ++y){
            for(u32 x = 0u; x < mipWidth; ++x){
                DepthInterval actual;
                NWB_MEMCPY(&actual, sizeof(actual), mapped + static_cast<usize>(y) * rowPitch + x * outputTexelBytes, sizeof(actual));
                const DepthInterval reference = expected[static_cast<usize>(y) * mipWidth + x];
                EXPECT_EQ(actual.minimum, reference.minimum) << "texel " << x << "," << y;
                EXPECT_EQ(actual.maximum, reference.maximum) << "texel " << x << "," << y;
            }
        }
        previous.swap(expected);
        sourceWidth = mipWidth;
        sourceHeight = mipHeight;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(ReflectionKernelTest, CookedReflectionDepthKernelPreservesEveryNativeMip){
    using namespace __hidden_reflection_depth_kernel_tests;
    auto& device = ReflectionKernelTest::device();
    Alloc::ScratchArena scratchArena(Name("tests/smoke/reflection_kernel/depth"));
    ComputePipelineHandle pipeline;
    ASSERT_TRUE(loadKernel("depth_reduce_cs", sizeof(DepthPushConstants), scratchArena, pipeline));

    const u32 singletonBits[] = {
        0x3e800000u, 0u, 0x80000000u, 0x3f800000u, 0xbe800000u, 0x40000000u, 0x7fc00000u, 0x7f800000u, 0xff800000u
    };
    const f32 singletonExpected[] = { 0.25f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
    for(usize singleton = 0u; singleton < LengthOf(singletonBits); ++singleton){
        const u32 bits = singletonBits[singleton];
        SCOPED_TRACE(bits);
        const f32 input = BitCast<f32>(bits);
        const DepthInterval expected{ singletonExpected[singleton], singletonExpected[singleton] };
        RunDepthCase(device, *pipeline, 1u, 1u, NotNull<const f32*>(&input), Format::R32_FLOAT, scratchArena, &expected);
    }
    const f32 line[] = { 0.0f, 0.5f, 1.0f };
    const DepthInterval lineExpected[] = { { 0.0f, 0.5f }, { 0.0f, 1.0f }, { 0.5f, 1.0f } };
    RunDepthCase(device, *pipeline, 3u, 1u, NotNull<const f32*>(line), Format::R32_FLOAT, scratchArena, lineExpected);
    RunDepthCase(device, *pipeline, 1u, 3u, NotNull<const f32*>(line), Format::R32_FLOAT, scratchArena, lineExpected);
    const f32 square[] = { 0.25f, 0.5f, 0.75f, 1.0f };
    const DepthInterval squareExpected[] = { { 0.25f, 1.0f }, { 0.25f, 1.0f }, { 0.25f, 1.0f }, { 0.25f, 1.0f } };
    RunDepthCase(device, *pipeline, 2u, 2u, NotNull<const f32*>(square), Format::R32_FLOAT, scratchArena, squareExpected);

    const u32 dimensions[][2] = { {1u,17u}, {17u,1u}, {7u,9u}, {8u,8u}, {9u,7u}, {17u,19u}, {31u,33u}, {45u,23u}, {960u,720u} };
    Vector<f32, Alloc::ScratchArena> input(scratchArena);
    input.reserve(960u * 720u);
    for(const auto& dimension : dimensions){
        const u32 width = dimension[0];
        const u32 height = dimension[1];
        input.clear();
        for(u32 y = 0u; y < height; ++y){
            for(u32 x = 0u; x < width; ++x)
                input.push_back(static_cast<f32>((x * 17u + y * 29u + (x ^ y) * 3u) & 255u) / 256.0f);
        }
        RunDepthCase(device, *pipeline, width, height, NotNull<const f32*>(input.data()), Format::R32_FLOAT, scratchArena);
        // The renderer samples D32 directly. Test that view/format path too, using finite valid depth values.
        RunDepthCase(device, *pipeline, width, height, NotNull<const f32*>(input.data()), Format::D32, scratchArena);
        for(usize index = 0u; index < input.size(); index += 19u)
            input[index] = BitCast<f32>(singletonBits[(index / 19u) % LengthOf(singletonBits)]);
        RunDepthCase(device, *pipeline, width, height, NotNull<const f32*>(input.data()), Format::R32_FLOAT, scratchArena);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

