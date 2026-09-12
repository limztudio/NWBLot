// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "reflection_kernel_fixture.h"

#include <impl/assets/graphics/reflection/spatial_constants.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>

#include <global/bit.h>
#include <global/math/convert.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{
namespace __hidden_spatial_kernel_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SpatialPushConstants{
#define NWB_REFLECTION_SPATIAL_TEST_FIELD(name) u32 name;
    NWB_REFLECTION_SPATIAL_UINT_FIELDS(NWB_REFLECTION_SPATIAL_TEST_FIELD)
#undef NWB_REFLECTION_SPATIAL_TEST_FIELD
};

struct Pixel{
    f32 r;
    f32 g;
    f32 b;
    f32 a;
};

struct SpatialView{
    f32 worldToClip[16]{};
    f32 clipToWorld[16]{};
    f32 cameraPosition[4]{};
};

struct Inputs{
    explicit Inputs(Alloc::ScratchArena& arena)
        : radiance(arena)
        , specular(arena)
        , position(arena)
        , normal(arena)
        , depth(arena)
    {}


    Vector<Pixel, Alloc::ScratchArena> radiance;
    Vector<Pixel, Alloc::ScratchArena> specular;
    Vector<Pixel, Alloc::ScratchArena> position;
    Vector<Pixel, Alloc::ScratchArena> normal;
    Vector<f32, Alloc::ScratchArena> depth;
};

namespace Pattern{
    enum Enum : u8{
        Dense,
        Ineligible,
        SingleEligible,
        Discontinuities,
        ZeroRadiance,
        Mirror,
        InvalidPosition,
        UnderflowGaussian,
        kCount,
    };
};

static_assert(sizeof(SpatialPushConstants) == NWB_REFLECTION_SPATIAL_PUSH_CONSTANT_BYTES);
static_assert(sizeof(Pixel) == 16u);
static_assert(sizeof(SpatialView) == 144u);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void RunSpatialCase(
    GraphicsBackend::Device& device,
    ComputePipeline& referencePipeline,
    ComputePipeline& candidatePipeline,
    const u32 width,
    const u32 height,
    const u32 radius,
    const Pattern::Enum pattern,
    Alloc::ScratchArena& scratchArena){
    SCOPED_TRACE(width);
    SCOPED_TRACE(height);
    SCOPED_TRACE(radius);
    SCOPED_TRACE(static_cast<u32>(pattern));
    const usize pixelCount = static_cast<usize>(width) * height;
    Inputs input(scratchArena);
    input.radiance.reserve(pixelCount);
    input.specular.reserve(pixelCount);
    input.position.reserve(pixelCount);
    input.normal.reserve(pixelCount);
    input.depth.reserve(pixelCount);
    for(u32 y = 0u; y < height; ++y){
        for(u32 x = 0u; x < width; ++x){
            const u32 index = y * width + x;
            Pixel radiance{
                static_cast<f32>(index % 4u), static_cast<f32>((index + 1u) % 4u) * 0.5f,
                static_cast<f32>(index % 3u) * 0.25f, 1.0f
            };
            radiance.a = (index & 1u) == 0u ? 1.0f : 3.0f;
            Pixel specular{ 0.25f, 0.25f, 0.25f, 0.5f };
            Pixel position{
                (static_cast<f32>(x) - static_cast<f32>(width / 2u)) * 0.125f,
                (static_cast<f32>(y) - static_cast<f32>(height / 2u)) * 0.125f, 2.0f, 0.0f
            };
            Pixel normal{ 0.5f, 0.5f, 1.0f, 0.0f };
            f32 depth = 0.5f;
            switch(pattern){
            case Pattern::Ineligible:
                switch(index % 7u){
                case 0u:
                    radiance.a = 0.0f;
                    break;
                case 1u:
                    depth = 1.0f;
                    break;
                case 2u:
                    depth = BitCast<f32>(0x7fc00000u);
                    break;
                case 3u:
                    depth = -0.25f;
                    break;
                case 4u:
                    specular.a = 0.0f;
                    break;
                case 5u:
                    specular.r = specular.g = specular.b = 0.0f;
                    break;
                default:
                    specular.r = BitCast<f32>(0x7f800000u);
                    break;
                }
                break;
            case Pattern::SingleEligible:
                if(x != Min(width / 2u, 7u) || y != Min(height / 2u, 7u))
                    radiance.a = 0.0f;
                break;
            case Pattern::Discontinuities:
                switch(x % 7u){
                case 0u:
                    specular.r = 0.75f;
                    break;
                case 1u:
                    specular.a = 0.75f;
                    break;
                case 2u:
                    normal = Pixel{ 1.0f, 0.5f, 0.5f, 0.0f };
                    break;
                case 3u:
                    depth = 1.0f;
                    break;
                case 4u:
                    position.b = 4.0f;
                    break;
                case 5u:
                    radiance.a = 0.0f;
                    break;
                default:
                    radiance.r = radiance.g = radiance.b = 0.0f;
                    break;
                }
                break;
            case Pattern::ZeroRadiance:
                radiance.r = radiance.g = radiance.b = 0.0f;
                break;
            case Pattern::Mirror:
                specular.a = 0.0f;
                break;
            case Pattern::InvalidPosition:
                position.b = BitCast<f32>(0x7fc00000u);
                break;
            case Pattern::UnderflowGaussian:
                specular.a = 0.0009765625f;
                break;
            default:
                break;
            }
            input.radiance.push_back(radiance);
            input.specular.push_back(specular);
            input.position.push_back(position);
            input.normal.push_back(normal);
            input.depth.push_back(depth);
        }
    }

    TextureDesc sourceDesc;
    sourceDesc
        .setWidth(width)
        .setHeight(height)
        .setFormat(Format::RGBA32_FLOAT)
        .setInitialState(ResourceStates::Common)
        .setKeepInitialState(true)
    ;
    const TextureHandle sources[] = {
        device.createTexture(sourceDesc), device.createTexture(sourceDesc),
        device.createTexture(sourceDesc), device.createTexture(sourceDesc)
    };
    TextureDesc depthDesc = sourceDesc;
    depthDesc.setFormat(Format::R32_FLOAT);
    const TextureHandle depth = device.createTexture(depthDesc);
    ASSERT_TRUE(depth);
    TextureDesc outputDesc = sourceDesc;
    outputDesc.setFormat(Format::RGBA16_FLOAT).setInUAV(true);
    const TextureHandle outputs[] = { device.createTexture(outputDesc), device.createTexture(outputDesc) };
    const StagingTextureHandle readbacks[] = {
        device.createStagingTexture(outputDesc, CpuAccessMode::Read),
        device.createStagingTexture(outputDesc, CpuAccessMode::Read)
    };
    const BufferHandle slotsBuffer = device.createBuffer(
        BufferDesc().setByteSize(sizeof(Impl::DeferredBindlessResourceSlots)).setIsConstantBuffer(true)
            .setInitialState(ResourceStates::ConstantBuffer).setKeepInitialState(true)
    );
    const BufferHandle viewBuffer = device.createBuffer(
        BufferDesc().setByteSize(sizeof(SpatialView)).setIsConstantBuffer(true)
            .setInitialState(ResourceStates::ConstantBuffer).setKeepInitialState(true)
    );
    ASSERT_TRUE(slotsBuffer);
    ASSERT_TRUE(viewBuffer);
    auto& heap = device.getDescriptorHeap();
    GpuDescriptorHandle descriptors[9]{};
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
    descriptors[4] = heap.allocate(GpuDescriptorClass::SampledImage);
    descriptors[5] = heap.allocate(GpuDescriptorClass::UniformBuffer);
    descriptors[6] = heap.allocate(GpuDescriptorClass::UniformBuffer);
    ASSERT_TRUE(descriptors[4].valid());
    ASSERT_TRUE(descriptors[5].valid());
    ASSERT_TRUE(descriptors[6].valid());
    ASSERT_TRUE(heap.write(descriptors[4], DescriptorWriteItem::Texture_SRV(0u, depth.get())));
    ASSERT_TRUE(heap.write(descriptors[5], DescriptorWriteItem::ConstantBuffer(0u, slotsBuffer.get())));
    ASSERT_TRUE(heap.write(descriptors[6], DescriptorWriteItem::ConstantBuffer(0u, viewBuffer.get())));
    for(u32 index = 0u; index < LengthOf(outputs); ++index){
        ASSERT_TRUE(outputs[index]);
        ASSERT_TRUE(readbacks[index]);
        descriptors[7u + index] = heap.allocate(GpuDescriptorClass::StorageImage);
        ASSERT_TRUE(descriptors[7u + index].valid());
        ASSERT_TRUE(heap.write(descriptors[7u + index], DescriptorWriteItem::Texture_UAV(0u, outputs[index].get())));
    }
    Impl::DeferredBindlessResourceSlots slots;
    slots.gbufferNormal = descriptors[3].slot();
    slots.gbufferWorldPosition = descriptors[2].slot();
    slots.gbufferDepth = descriptors[4].slot();
    SpatialView view;
    for(u32 diagonal = 0u; diagonal < 16u; diagonal += 5u){
        view.worldToClip[diagonal] = 1.0f;
        view.clipToWorld[diagonal] = 1.0f;
    }
    // Orthographic +Z-facing receiver at world Z=2 projects to depth 0.5 from the camera at Z=10.
    view.worldToClip[10] = -0.0625f;
    view.worldToClip[11] = 0.625f;
    view.clipToWorld[10] = -16.0f;
    view.clipToWorld[11] = 10.0f;
    view.cameraPosition[2] = 10.0f;
    view.cameraPosition[3] = 1.0f;
    const CommandListHandle commandList = device.createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
    const Pixel* const data[] = { input.radiance.data(), input.specular.data(), input.position.data(), input.normal.data() };
    for(u32 index = 0u; index < LengthOf(sources); ++index){
        ASSERT_TRUE(commandList->tryWriteTexture(*sources[index], 0u, 0u, data[index], width * sizeof(Pixel)));
        commandList->setTextureState(sources[index].get(), s_AllSubresources, ResourceStates::ShaderResource);
    }
    ASSERT_TRUE(commandList->tryWriteTexture(*depth, 0u, 0u, input.depth.data(), width * sizeof(f32)));
    commandList->setTextureState(depth.get(), s_AllSubresources, ResourceStates::ShaderResource);
    ASSERT_TRUE(commandList->tryWriteBuffer(*slotsBuffer, &slots, sizeof(slots)));
    ASSERT_TRUE(commandList->tryWriteBuffer(*viewBuffer, &view, sizeof(view)));
    commandList->setBufferState(slotsBuffer.get(), ResourceStates::ConstantBuffer);
    commandList->setBufferState(viewBuffer.get(), ResourceStates::ConstantBuffer);
    ComputePipeline* const pipelines[] = { &referencePipeline, &candidatePipeline };
    for(u32 index = 0u; index < LengthOf(outputs); ++index){
        // Different representable sentinels make a matching unwritten/partial output fail parity as well as copy checks.
        commandList->clearTextureFloat(*outputs[index], s_AllSubresources, Color(index == 0u ? 31.0f : 19.0f));
        commandList->setTextureState(outputs[index].get(), s_AllSubresources, ResourceStates::UnorderedAccess, true);
        commandList->commitBarriers();
        ComputeState state;
        state.setPipeline(pipelines[index]);
        commandList->setComputeState(state);
        heap.bindCompute(*commandList, *pipelines[index]);
        const SpatialPushConstants push{
            width, height, descriptors[0].slot(), descriptors[7u + index].slot(),
            descriptors[5].slot(), descriptors[1].slot(), descriptors[6].slot(), radius
        };
        commandList->setPushConstants(&push, sizeof(push));
        commandList->dispatch(
            DivideUp(width, NWB_REFLECTION_SPATIAL_GROUP_SIZE), DivideUp(height, NWB_REFLECTION_SPATIAL_GROUP_SIZE), 1u
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
        ASSERT_GE(pitches[index], width * sizeof(u16) * 4u);
    }
    usize changedPixels = 0u;
    usize brightenedZeroSamples = 0u;
    const bool exactCopy = radius == 0u || (pattern != Pattern::Dense && pattern != Pattern::Discontinuities);
    for(u32 y = 0u; y < height; ++y){
        for(u32 x = 0u; x < width; ++x){
            u16 reference[4];
            u16 candidate[4];
            const usize byteX = x * sizeof(reference);
            NWB_MEMCPY(reference, sizeof(reference), mapped[0] + y * pitches[0] + byteX, sizeof(reference));
            NWB_MEMCPY(candidate, sizeof(candidate), mapped[1] + y * pitches[1] + byteX, sizeof(candidate));
            const Pixel center = input.radiance[static_cast<usize>(y) * width + x];
            const u16 expectedCopy[] = {
                ConvertFloatToHalf(center.r), ConvertFloatToHalf(center.g),
                ConvertFloatToHalf(center.b), ConvertFloatToHalf(center.a)
            };
            for(u32 channel = 0u; channel < 4u; ++channel){
                EXPECT_EQ(candidate[channel], reference[channel]) << "pixel " << x << "," << y << " channel " << channel;
                if(exactCopy)
                    EXPECT_EQ(candidate[channel], expectedCopy[channel]) << "copy pixel " << x << "," << y;
            }
            EXPECT_EQ(candidate[3], expectedCopy[3]) << "source diagnostic alpha at " << x << "," << y;
            if(NWB_MEMCMP(candidate, expectedCopy, sizeof(candidate)) != 0)
                ++changedPixels;
            if(center.r == 0.0f && candidate[0] != 0u)
                ++brightenedZeroSamples;
        }
    }
    // A baseline/candidate pair that both accidentally bypass filtering must fail these independent observations.
    if(pattern == Pattern::Dense && radius != 0u && pixelCount > 1u){
        EXPECT_GT(changedPixels, 0u);
        EXPECT_GT(brightenedZeroSamples, 0u);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(ReflectionKernelTest, CookedSpatialKernelMatchesFrozenProductionAcrossEligibilityEdges){
    using namespace __hidden_spatial_kernel_tests;
    Alloc::ScratchArena scratchArena(Name("tests/smoke/reflection_kernel/spatial"));
    ComputePipelineHandle candidate;
    ComputePipelineHandle reference;
    const Path sourceRoot(arena(), NWB_REFLECTION_KERNEL_SOURCE_ROOT);
    const Path referenceSource = sourceRoot / "tests/smoke/reflection_kernel/reference/spatial_cs.slang";
    ASSERT_TRUE(loadKernel("spatial_cs", sizeof(SpatialPushConstants), scratchArena, candidate));
    ASSERT_TRUE(loadKernel("spatial_cs", sizeof(SpatialPushConstants), scratchArena, reference, &referenceSource));
    const u32 dimensions[][2] = { { 1u, 1u }, { 7u, 9u }, { 8u, 8u }, { 9u, 7u }, { 17u, 19u } };
    for(const auto& dimension : dimensions){
        for(u32 radius = 0u; radius <= NWB_REFLECTION_SPATIAL_MAX_RADIUS; ++radius){
            for(u32 pattern = 0u; pattern < Pattern::kCount; ++pattern){
                RunSpatialCase(
                    device(), *reference, *candidate, dimension[0], dimension[1], radius,
                    static_cast<Pattern::Enum>(pattern), scratchArena
                );
            }
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

