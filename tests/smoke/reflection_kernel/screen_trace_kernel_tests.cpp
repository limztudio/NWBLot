// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "reflection_kernel_fixture.h"

#include <impl/assets/graphics/reflection/depth_constants.h>
#include <impl/assets/graphics/reflection/frame_constants.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>

#include <global/bit.h>
#include <global/scope_exit.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_screen_trace_kernel_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct Parameters{
#define NWB_SCREEN_TEST_UINT(name, value) u32 name = value;
    NWB_REFLECTION_FRAME_UINT_FIELDS(NWB_SCREEN_TEST_UINT)
#undef NWB_SCREEN_TEST_UINT
#define NWB_SCREEN_TEST_FLOAT(name, value) f32 name = value;
    NWB_REFLECTION_FRAME_FLOAT_FIELDS(NWB_SCREEN_TEST_FLOAT)
#undef NWB_SCREEN_TEST_FLOAT
};

struct View{
    f32 worldToClip[16]{};
    f32 clipToWorld[16]{};
    f32 cameraPosition[4]{};
};

struct Pixel{
    f32 r;
    f32 g;
    f32 b;
    f32 a;
};

struct Ray{
    Float3U position;
    u32 sourceX;
    Float3U normal;
    u32 sourceY;
    Float3U direction;
    f32 bias;
};

struct Observation{
    u32 returned;
    u32 iterations;
    u32 limited;
    u32 confidence;
    u32 radiance[3];
    u32 written;
};

struct DepthPush{
#define NWB_SCREEN_DEPTH_FIELD(name) u32 name;
    NWB_REFLECTION_DEPTH_UINT_FIELDS(NWB_SCREEN_DEPTH_FIELD)
#undef NWB_SCREEN_DEPTH_FIELD
};

namespace Source{
    enum Enum : u32{
        Depth,
        Position,
        Normal,
        Specular,
        Color,
        kCount,
    };
};

namespace Pattern{
    enum Enum : u8{
        Flat,
        Sloped,
        Gaps,
        Invalid,
        Perspective,
        Empty,
    };
};

struct Case{
    u32 width;
    u32 height;
    Pattern::Enum pattern;
    bool invalidCoarseMip = false;
};

struct Coverage{
    u32 hits = 0u;
    u32 limited = 0u;
    u32 marched = 0u;
    u32 rejected = 0u;
};

static constexpr u32 s_Sentinel = 0xd35ce17eu;
static constexpr u32 s_GuardWords = 8u;
static_assert(sizeof(Parameters) == 192u);
static_assert(sizeof(View) == 144u);
static_assert(sizeof(Pixel) == 16u);
static_assert(sizeof(Ray) == 48u);
static_assert(sizeof(Observation) == 32u);
static_assert(sizeof(DepthPush) == NWB_REFLECTION_DEPTH_PUSH_CONSTANT_BYTES);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void FillSource(const Case& testCase, const Source::Enum source, Vector<Pixel, Alloc::ScratchArena>& pixels){
    pixels.resize(static_cast<usize>(testCase.width) * testCase.height);
    const f32 nan = BitCast<f32>(0x7fc00000u);
    const f32 slope = testCase.pattern == Pattern::Sloped ? 0.125f : 0.0f;
    const f32 inverseNormalLength = 1.0f / Sqrt(1.0f + slope * slope);
    for(u32 y = 0u; y < testCase.height; ++y){
        for(u32 x = 0u; x < testCase.width; ++x){
            const f32 ndcX = (static_cast<f32>(x) + 0.5f) * 2.0f / static_cast<f32>(testCase.width) - 1.0f;
            const f32 ndcY = 1.0f - (static_cast<f32>(y) + 0.5f) * 2.0f / static_cast<f32>(testCase.height);
            const f32 z = testCase.pattern == Pattern::Perspective ? 1.2f : 0.55f + slope * ndcX;
            const f32 worldX = testCase.pattern == Pattern::Perspective ? ndcX * z : ndcX;
            const f32 worldY = testCase.pattern == Pattern::Perspective ? ndcY * z : ndcY;
            const f32 depth = testCase.pattern == Pattern::Perspective ? (z - 0.1f) / z : z;
            Pixel value{};
            switch(source){
            case Source::Depth:
                value.r = testCase.pattern == Pattern::Empty
                    || (testCase.pattern == Pattern::Gaps && ((x / 3u + y / s_ExpectedDualCount) & 1u) != 0u) ? 1.0f : depth;
                if(testCase.pattern == Pattern::Invalid && x % 5u == 0u)
                    value.r = x % s_ExpectedDualCount == 0u ? nan : -0.25f;
                break;
            case Source::Position:
                value = { worldX, worldY, z, 1.0f };
                if(testCase.pattern == Pattern::Invalid && x % 5u == 1u)
                    value.r = nan;
                break;
            case Source::Normal:
                value = { 0.5f + 0.5f * slope * inverseNormalLength, 0.5f, 0.5f - 0.5f * inverseNormalLength, 1.0f };
                if(testCase.pattern == Pattern::Invalid && x % 5u == s_ExpectedDualCount)
                    value.g = nan;
                break;
            case Source::Specular:
                value = { 0.0f, 0.0f, 0.0f, 0.5f };
                if(testCase.pattern == Pattern::Invalid && x % 5u == 3u)
                    value.a = nan;
                break;
            case Source::Color:
                value = { 0.125f + static_cast<f32>(x % 7u) * 0.125f, 0.25f, 0.5f, 1.0f };
                if(testCase.pattern == Pattern::Invalid && x % 5u == 4u)
                    value.b = nan;
                break;
            default:
                break;
            }
            pixels[static_cast<usize>(y) * testCase.width + x] = value;
        }
    }
}

static void FillRays(const Case& testCase, Vector<Ray, Alloc::ScratchArena>& rays){
    rays.clear();
    for(u32 row = 0u; row < 3u; ++row){
        for(u32 direction = 0u; direction < 4u; ++direction){
            const f32 signX = (direction & 1u) == 0u ? 1.0f : -1.0f;
            const f32 signZ = (direction & s_ExpectedDualCount) == 0u ? 1.0f : -1.0f;
            const f32 y = -0.5f + static_cast<f32>(row) * 0.5f;
            Ray ray{
                { -0.9f * signX, y, signZ > 0.0f ? 0.05f : 0.95f },
                signX > 0.0f ? 0u : testCase.width - 1u,
                { 0.0f, 0.0f, -signZ },
                Min(static_cast<u32>((0.5f - y * 0.5f) * static_cast<f32>(testCase.height)), testCase.height - 1u),
                { 0.8f * signX, 0.0f, 0.6f * signZ },
                0.001f,
            };
            if(testCase.pattern == Pattern::Perspective){
                ray.position = { -0.3f * signX, y * 0.3f, signZ > 0.0f ? 0.3f : 2.0f };
                ray.direction = { 0.6f * signX, 0.0f, 0.8f * signZ };
            }
            rays.push_back(ray);
            ray.direction = { 0.8f * signX, 0.36f, 0.48f * signZ };
            rays.push_back(ray);
        }
    }
    Ray vertical = rays[0];
    vertical.position = { 0.0f, -0.9f, 0.05f };
    vertical.direction = { 0.0f, 0.8f, 0.6f };
    vertical.sourceX = testCase.width / s_ExpectedDualCount;
    vertical.sourceY = testCase.height - 1u;
    rays.push_back(vertical);
    vertical.direction.x = 1e-22f;
    rays.push_back(vertical);
    Ray invalid = rays[0];
    invalid.position.x = BitCast<f32>(0x7fc00000u);
    rays.push_back(invalid);
    invalid = rays[0];
    invalid.direction = { 0.0f, 0.0f, 0.0f };
    rays.push_back(invalid);
    invalid = rays[0];
    invalid.position.z = -0.2f;
    rays.push_back(invalid);
}

static void RunCase(
    GraphicsBackend::Device& device,
    ComputePipeline& depthPipeline,
    ComputePipeline& reference,
    ComputePipeline& candidate,
    const Case& testCase,
    Alloc::ScratchArena& scratchArena,
    Coverage& coverage){
    SCOPED_TRACE(testCase.width);
    SCOPED_TRACE(testCase.height);
    SCOPED_TRACE(static_cast<u32>(testCase.pattern));
    SCOPED_TRACE(testCase.invalidCoarseMip);
    u32 mipCount = 1u;
    for(u32 extent = Max(testCase.width, testCase.height); extent > 1u; extent >>= 1u)
        ++mipCount;
    ASSERT_LE(mipCount, NWB_REFLECTION_MAX_DEPTH_MIPS);
    Vector<Ray, Alloc::ScratchArena> rays(scratchArena);
    FillRays(testCase, rays);
    const u32 rayCount = static_cast<u32>(rays.size());
    constexpr u32 pyramidDescriptor = Source::kCount;
    constexpr u32 slotsDescriptor = pyramidDescriptor + 1u;
    constexpr u32 viewDescriptor = slotsDescriptor + 1u;
    constexpr u32 parametersDescriptor = viewDescriptor + 1u;
    constexpr u32 raysDescriptor = parametersDescriptor + s_ExpectedDualCount;
    constexpr u32 outputsDescriptor = raysDescriptor + 1u;
    constexpr u32 mipsDescriptor = outputsDescriptor + s_ExpectedDualCount;
    GpuDescriptorHandle descriptors[mipsDescriptor + s_ExpectedDualCount * NWB_REFLECTION_MAX_DEPTH_MIPS]{};
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
        .setWidth(testCase.width).setHeight(testCase.height).setFormat(Format::RGBA32_FLOAT)
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
    TextureDesc pyramidDesc = sourceDesc;
    pyramidDesc.setMipLevels(mipCount).setInUAV(true);
    const TextureHandle pyramid = device.createTexture(pyramidDesc);
    ASSERT_TRUE(pyramid);
    descriptors[pyramidDescriptor] = heap.allocate(GpuDescriptorClass::SampledImage);
    ASSERT_TRUE(descriptors[pyramidDescriptor].valid());
    ASSERT_TRUE(heap.write(descriptors[pyramidDescriptor], DescriptorWriteItem::Texture_SRV(0u, pyramid.get())));
    for(u32 mip = 0u; mip < mipCount; ++mip){
        const TextureSubresourceSet subresources(mip, 1u, 0u, 1u);
        descriptors[mipsDescriptor + mip * s_ExpectedDualCount] = heap.allocate(GpuDescriptorClass::SampledImage);
        descriptors[mipsDescriptor + mip * s_ExpectedDualCount + 1u] = heap.allocate(GpuDescriptorClass::StorageImage);
        ASSERT_TRUE(descriptors[mipsDescriptor + mip * s_ExpectedDualCount].valid());
        ASSERT_TRUE(descriptors[mipsDescriptor + mip * s_ExpectedDualCount + 1u].valid());
        ASSERT_TRUE(heap.write(
            descriptors[mipsDescriptor + mip * s_ExpectedDualCount],
            DescriptorWriteItem::Texture_SRV(0u, pyramid.get(), Format::RGBA32_FLOAT, subresources)
        ));
        ASSERT_TRUE(heap.write(
            descriptors[mipsDescriptor + mip * s_ExpectedDualCount + 1u],
            DescriptorWriteItem::Texture_UAV(0u, pyramid.get(), Format::RGBA32_FLOAT, subresources)
        ));
    }
    const usize constantBytes[] = { sizeof(Impl::DeferredBindlessResourceSlots), sizeof(View), sizeof(Parameters), sizeof(Parameters) };
    BufferHandle constants[4];
    for(u32 index = 0u; index < LengthOf(constants); ++index){
        BufferDesc desc;
        desc.setByteSize(constantBytes[index]).setIsConstantBuffer(true).setInitialState(ResourceStates::Common).setKeepInitialState(true);
        constants[index] = device.createBuffer(desc);
        ASSERT_TRUE(constants[index]);
        descriptors[slotsDescriptor + index] = heap.allocate(GpuDescriptorClass::UniformBuffer);
        ASSERT_TRUE(descriptors[slotsDescriptor + index].valid());
        ASSERT_TRUE(heap.write(descriptors[slotsDescriptor + index], DescriptorWriteItem::ConstantBuffer(0u, constants[index].get())));
    }
    const u32 outputWords = rayCount * static_cast<u32>(sizeof(Observation) / sizeof(u32)) + s_GuardWords;
    BufferDesc raysDesc;
    raysDesc.setByteSize(rays.size() * sizeof(Ray)).setCanHaveRawViews(true).setInitialState(ResourceStates::Common).setKeepInitialState(true);
    const BufferHandle rayInputs = device.createBuffer(raysDesc);
    ASSERT_TRUE(rayInputs);
    descriptors[raysDescriptor] = heap.allocate(GpuDescriptorClass::StorageBuffer);
    ASSERT_TRUE(descriptors[raysDescriptor].valid());
    ASSERT_TRUE(heap.write(descriptors[raysDescriptor], DescriptorWriteItem::RawBuffer_SRV(0u, rayInputs.get())));
    BufferHandle outputs[2];
    for(u32 arm = 0u; arm < s_ExpectedDualCount; ++arm){
        BufferDesc desc;
        desc
            .setByteSize(static_cast<u64>(outputWords) * sizeof(u32)).setCanHaveRawViews(true).setCanHaveUAVs(true)
            .setCpuAccess(CpuAccessMode::Read).setInitialState(ResourceStates::Common).setKeepInitialState(true)
        ;
        outputs[arm] = device.createBuffer(desc);
        ASSERT_TRUE(outputs[arm]);
        descriptors[outputsDescriptor + arm] = heap.allocate(GpuDescriptorClass::StorageBuffer);
        ASSERT_TRUE(descriptors[outputsDescriptor + arm].valid());
        ASSERT_TRUE(heap.write(descriptors[outputsDescriptor + arm], DescriptorWriteItem::RawBuffer_UAV(0u, outputs[arm].get())));
    }
    Impl::DeferredBindlessResourceSlots slots;
    slots.gbufferDepth = descriptors[Source::Depth].slot();
    slots.gbufferWorldPosition = descriptors[Source::Position].slot();
    slots.gbufferNormal = descriptors[Source::Normal].slot();
    slots.opaqueColor = descriptors[Source::Color].slot();
    View view;
    for(u32 diagonal = 0u; diagonal < 16u; diagonal += 5u)
        view.worldToClip[diagonal] = 1.0f;
    view.cameraPosition[2] = -2.0f;
    if(testCase.pattern == Pattern::Perspective){
        view.worldToClip[11] = -0.1f;
        view.worldToClip[14] = 1.0f;
        view.worldToClip[15] = 0.0f;
        view.cameraPosition[2] = 0.0f;
    }
    const CommandListHandle commandList = device.createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
    Vector<Pixel, Alloc::ScratchArena> pixels(scratchArena);
    for(u32 source = 0u; source < Source::kCount; ++source){
        FillSource(testCase, static_cast<Source::Enum>(source), pixels);
        ASSERT_TRUE(commandList->tryWriteTexture(*sources[source], 0u, 0u, pixels.data(), testCase.width * sizeof(Pixel)));
        commandList->setTextureState(sources[source].get(), s_AllSubresources, ResourceStates::ShaderResource);
    }
    ASSERT_TRUE(commandList->tryWriteBuffer(*constants[0], &slots, sizeof(slots)));
    ASSERT_TRUE(commandList->tryWriteBuffer(*constants[1], &view, sizeof(view)));
    ASSERT_TRUE(commandList->tryWriteBuffer(*rayInputs, rays.data(), rays.size() * sizeof(Ray)));
    u32 sourceWidth = testCase.width;
    u32 sourceHeight = testCase.height;
    for(u32 mip = 0u; mip < mipCount; ++mip){
        const u32 width = Max(testCase.width >> mip, 1u);
        const u32 height = Max(testCase.height >> mip, 1u);
        if(mip != 0u)
            commandList->setTextureState(pyramid.get(), TextureSubresourceSet(mip - 1u, 1u, 0u, 1u), ResourceStates::ShaderResource);
        commandList->setTextureState(pyramid.get(), TextureSubresourceSet(mip, 1u, 0u, 1u), ResourceStates::UnorderedAccess);
        commandList->commitBarriers();
        ComputeState state;
        state.setPipeline(&depthPipeline);
        commandList->setComputeState(state);
        heap.bindCompute(*commandList, depthPipeline);
        const DepthPush push{
            mip == 0u ? descriptors[Source::Depth].slot() : descriptors[mipsDescriptor + (mip - 1u) * s_ExpectedDualCount].slot(),
            descriptors[mipsDescriptor + mip * s_ExpectedDualCount + 1u].slot(), sourceWidth, sourceHeight, width, height,
        };
        commandList->setPushConstants(&push, sizeof(push));
        commandList->dispatch(DivideUp(width, NWB_REFLECTION_DEPTH_GROUP_SIZE), DivideUp(height, NWB_REFLECTION_DEPTH_GROUP_SIZE), 1u);
        sourceWidth = width;
        sourceHeight = height;
    }
    if(testCase.invalidCoarseMip){
        ASSERT_GT(mipCount, 1u);
        const u32 width = Max(testCase.width >> 1u, 1u);
        const u32 height = Max(testCase.height >> 1u, 1u);
        const f32 nan = BitCast<f32>(0x7fc00000u);
        pixels.assign(static_cast<usize>(width) * height, Pixel{ nan, nan, 0.0f, 0.0f });
        ASSERT_TRUE(commandList->tryWriteTexture(*pyramid, 0u, 1u, pixels.data(), width * sizeof(Pixel)));
    }
    ASSERT_FALSE(commandList->commandRecordingFailed());
    commandList->close();
    ASSERT_FALSE(commandList->commandRecordingFailed());
    CommandList* const submitted[] = { commandList.get() };
    ASSERT_TRUE(device.executeCommandLists(submitted, LengthOf(submitted), commandList->getDescription().physicalQueue, QueueSubmissionDesc{}).valid());
    ASSERT_TRUE(device.waitForIdle());

    Parameters parameters;
    parameters.width = testCase.width;
    parameters.height = testCase.height;
    parameters.deferredResourcesSlot = descriptors[slotsDescriptor].slot();
    parameters.viewSlot = descriptors[viewDescriptor].slot();
    parameters.opaqueSpecularSlot = descriptors[Source::Specular].slot();
    parameters.depthPyramidSlot = descriptors[pyramidDescriptor].slot();
    parameters.counterSlot = descriptors[raysDescriptor].slot();
    parameters.queueCapacity = rayCount;
    parameters.maxRayDistance = 4.0f;
    parameters.distanceFadeStart = 3.0f;
    parameters.screenThickness = 0.1f;
    Vector<u32, Alloc::ScratchArena> initial(scratchArena);
    initial.resize(outputWords, s_Sentinel);
    Vector<u32, Alloc::ScratchArena> expected(scratchArena);
    expected.resize(outputWords);
    constexpr u32 budgets[] = { 0u, 1u, s_ExpectedDualCount, 3u, 4u, 8u, 16u, 96u, 256u, 300u };
    for(const u32 budget : budgets){
        u32 singleMipIterations = 0u;
        for(const u32 selectedMips : { 0u, 1u, mipCount }){
            SCOPED_TRACE(budget);
            SCOPED_TRACE(selectedMips);
            parameters.screenMaxSteps = budget;
            parameters.depthMipCount = selectedMips;
            commandList->open();
            for(const TextureHandle& texture : sources)
                commandList->setTextureState(texture.get(), s_AllSubresources, ResourceStates::ShaderResource);
            commandList->setTextureState(pyramid.get(), s_AllSubresources, ResourceStates::ShaderResource);
            commandList->setBufferState(rayInputs.get(), ResourceStates::ShaderResource);
            commandList->setBufferState(constants[0].get(), ResourceStates::ConstantBuffer);
            commandList->setBufferState(constants[1].get(), ResourceStates::ConstantBuffer);
            for(u32 arm = 0u; arm < s_ExpectedDualCount; ++arm){
                parameters.queueSlot = descriptors[outputsDescriptor + arm].slot();
                ASSERT_TRUE(commandList->tryWriteBuffer(*constants[s_ExpectedDualCount + arm], &parameters, sizeof(parameters)));
                ASSERT_TRUE(commandList->tryWriteBuffer(*outputs[arm], initial.data(), outputWords * sizeof(u32)));
                commandList->setBufferState(constants[s_ExpectedDualCount + arm].get(), ResourceStates::ConstantBuffer);
                commandList->setBufferState(outputs[arm].get(), ResourceStates::UnorderedAccess);
                commandList->commitBarriers();
                ComputePipeline& pipeline = arm == 0u ? reference : candidate;
                ComputeState state;
                state.setPipeline(&pipeline);
                commandList->setComputeState(state);
                heap.bindCompute(*commandList, pipeline);
                const u32 push = descriptors[parametersDescriptor + arm].slot();
                commandList->setPushConstants(&push, sizeof(push));
                commandList->dispatch(DivideUp(rayCount, 64u), 1u, 1u);
            }
            ASSERT_FALSE(commandList->commandRecordingFailed());
            commandList->close();
            ASSERT_FALSE(commandList->commandRecordingFailed());
            ASSERT_TRUE(device.executeCommandLists(submitted, LengthOf(submitted), commandList->getDescription().physicalQueue, QueueSubmissionDesc{}).valid());
            ASSERT_TRUE(device.waitForIdle());
            for(u32 arm = 0u; arm < s_ExpectedDualCount; ++arm){
                const u32* const mapped = static_cast<const u32*>(device.mapBuffer(*outputs[arm], CpuAccessMode::Read));
                ASSERT_NE(mapped, nullptr);
                ScopeExit unmap([&]()noexcept{ device.unmapBuffer(*outputs[arm]); });
                if(arm == 0u)
                    NWB_MEMCPY(expected.data(), expected.size() * sizeof(u32), mapped, outputWords * sizeof(u32));
                else{
                    for(u32 word = 0u; word < outputWords; ++word)
                        ASSERT_EQ(mapped[word], expected[word]) << "word " << word;
                }
                for(u32 word = outputWords - s_GuardWords; word < outputWords; ++word)
                    ASSERT_EQ(mapped[word], s_Sentinel);
            }
            for(u32 index = 0u; index < rayCount; ++index){
                const u32* const words = expected.data() + index * sizeof(Observation) / sizeof(u32);
                ASSERT_EQ(words[7], 0x51a7e001u);
                ASSERT_LE(words[1], Min(budget, 256u));
                ASSERT_LE(words[0], 1u);
                ASSERT_LE(words[2], 1u);
                if(budget == 0u || selectedMips == 0u){
                    ASSERT_EQ(words[0], 0u);
                    ASSERT_EQ(words[1], 0u);
                    ASSERT_EQ(words[2], 0u);
                }
                if(selectedMips > 1u){
                    coverage.hits += words[0];
                    coverage.limited += words[2];
                    coverage.marched += words[1] > 1u ? 1u : 0u;
                    coverage.rejected += words[1] == 0u ? 1u : 0u;
                }
            }
            // Ray zero first skips a finite mip-zero cell, then must fetch the deliberately invalid mip one.
            if(testCase.invalidCoarseMip && selectedMips > 1u && budget >= s_ExpectedDualCount){
                ASSERT_EQ(expected[0], 0u);
                ASSERT_EQ(expected[1], s_ExpectedDualCount);
                ASSERT_EQ(expected[2], 0u);
            }
            if(selectedMips == 1u)
                singleMipIterations = expected[1];
            if(
                testCase.width == 65u && testCase.pattern == Pattern::Flat && !testCase.invalidCoarseMip
                && budget == 96u && selectedMips > 1u
            ){
                // This ray ascends past empty cells, then must descend to mip zero before returning its planar hit.
                ASSERT_EQ(expected[0], 1u);
                ASSERT_NE(expected[1], singleMipIterations);
            }
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(ReflectionKernelTest, ScreenTraceDescentMatchesFrozenTraversalAtEveryBudget){
    using namespace __hidden_screen_trace_kernel_tests;
    Alloc::ScratchArena scratchArena(Name("tests/smoke/reflection_kernel/screen_descent"));
    ComputePipelineHandle depth;
    ComputePipelineHandle reference;
    ComputePipelineHandle candidate;
    const Path sourceRoot(arena(), NWB_REFLECTION_KERNEL_SOURCE_ROOT);
    const Path referenceSource = sourceRoot / "tests/smoke/reflection_kernel/reference/screen_trace_cs.slang";
    const Path candidateSource = sourceRoot / "tests/smoke/reflection_kernel/assets/screen_trace_cs.slang";
    ASSERT_TRUE(loadKernel("depth_reduce_cs", sizeof(DepthPush), scratchArena, depth));
    ASSERT_TRUE(loadKernel("classify_cs", sizeof(u32), scratchArena, reference, &referenceSource));
    ASSERT_TRUE(loadKernel("classify_cs", sizeof(u32), scratchArena, candidate, &candidateSource));
    constexpr Case cases[] = {
        { 65u, 17u, Pattern::Flat }, { 31u, 19u, Pattern::Sloped },
        { 33u, 25u, Pattern::Gaps }, { 17u, 1u, Pattern::Flat }, { 1u, 17u, Pattern::Flat },
        { 33u, 25u, Pattern::Perspective }, { 31u, 19u, Pattern::Invalid }, { 31u, 19u, Pattern::Empty },
        { 65u, 17u, Pattern::Flat, true },
    };
    Coverage coverage;
    for(const Case& testCase : cases){
        RunCase(device(), *depth, *reference, *candidate, testCase, scratchArena, coverage);
        ASSERT_FALSE(HasFatalFailure());
    }
    EXPECT_GT(coverage.hits, 0u);
    EXPECT_GT(coverage.limited, 0u);
    EXPECT_GT(coverage.marched, 0u);
    EXPECT_GT(coverage.rejected, 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

