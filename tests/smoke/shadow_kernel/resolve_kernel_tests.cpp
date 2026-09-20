// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shadow_kernel_fixture.h"
#include "resolve_guided_fit_cases.h"

#include <impl/assets/graphics/shadow/shadow_resolve_binding_slots.h>
#include <impl/assets/graphics/shadow/sw_binding_slots.h>
#include <impl/assets_shader/cook.h>

#include <global/filesystem.h>
#include <global/math/convert.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_shadow_resolve_kernel_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Mirror only the production selector ABI; filtering and folding execute the live RGB/scalar shaders.
struct ResolvePushConstants{
    u32 width;
    u32 height;
    u32 halfWidth;
    u32 halfHeight;
    u32 stepWidth;
    u32 stage;
    u32 lightSlotStart;
    u32 lightSlotCount;
    u32 momentsValid;
    u32 upsampleFold;
    u32 geometrySlot;
    u32 depthSlot;
    u32 worldPositionSlot;
    u32 normalSlot;
    u32 softHalfSlot;
    u32 inputColorSlot;
    u32 momentsSlot;
    u32 outputStorageSlot;
    u32 visibilityStorageSlot;
    u32 sceneShadingSlot;
    u32 padding;
};

struct HalfPixel{
    u16 r;
    u16 g;
    u16 b;
    u16 a;
};

namespace Pattern{
    enum Enum : u8{
        Ramp,
        Edge,
        Uniform,
    };
};

namespace Guidance{
    enum Enum : u8{
        Flat,
        AllInvalid,
        OppositeNormals,
        MixedInvalid,
        MixedNormals,
        LinearDistanceOffset,
    };
};

struct ResolveCase{
    u32 width = 16u;
    u32 height = 12u;
    Pattern::Enum pattern = Pattern::Ramp;
    Guidance::Enum guidance = Guidance::Flat;
    bool multiply = false;
    bool backgroundBorder = false;
    u32 stage = NWB_SHADOW_RESOLVE_STAGE_UPSAMPLE;
    u32 stepWidth = 1u;
    bool momentsValid = false;
    const ShadowResolveGuidedFit::Case* guidedFit = nullptr;
};

using HalfPixels = Vector<HalfPixel, Alloc::ScratchArena>;
using Pixels = Vector<Float4U, Alloc::ScratchArena>;

constexpr f32 s_LinearGuidanceOffset = 2.0f;

static_assert(sizeof(ResolvePushConstants) == 84u);
static_assert(offsetof(ResolvePushConstants, geometrySlot) == 40u);
static_assert(offsetof(ResolvePushConstants, sceneShadingSlot) == 76u);
static_assert(sizeof(HalfPixel) == 8u);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] HalfPixel ToHalfPixel(const Float4U& value){
    return {
        ConvertFloatToHalf(value.x), ConvertFloatToHalf(value.y), ConvertFloatToHalf(value.z), ConvertFloatToHalf(value.w)
    };
}

[[nodiscard]] Float4U Ramp(const f32 x, const f32 y){
    return { 0.125f + 0.0625f * x, 0.1875f + 0.03125f * y, 0.75f - 0.03125f * x - 0.015625f * y, 1.0f };
}

[[nodiscard]] Float4U PriorVisibility(const u32 y, const u32 height){
    return y == height / 2u ? Float4U{ 0.0f, 0.0f, 0.0f, 1.0f } : Float4U{ 0.25f, 0.5f, 0.75f, 1.0f };
}

void ExpectPixelNear(const Float4U& actual, const Float4U& expected){
    EXPECT_NEAR(actual.x, expected.x, 0.001f);
    EXPECT_NEAR(actual.y, expected.y, 0.001f);
    EXPECT_NEAR(actual.z, expected.z, 0.001f);
    EXPECT_FLOAT_EQ(actual.w, expected.w);
}

void ExpectInteriorRamp(const ResolveCase& testCase, const Pixels& actual){
    for(u32 y = 2u; y + 3u < testCase.height; ++y){
        for(u32 x = 2u; x + 3u < testCase.width; ++x){
            SCOPED_TRACE(x);
            SCOPED_TRACE(y);
            // Production trace/downsample guidance is located at full-resolution pixels 2*h.
            ExpectPixelNear(actual[y * testCase.width + x], Ramp(static_cast<f32>(x) * 0.5f, static_cast<f32>(y) * 0.5f));
        }
    }
}

void RunResolveCase(
    GraphicsBackend::Device& graphicsDevice,
    ComputePipeline& pipeline,
    const ResolveCase& testCase,
    Alloc::ScratchArena& scratchArena,
    Pixels& actual){
    SCOPED_TRACE(testCase.width);
    SCOPED_TRACE(testCase.height);
    SCOPED_TRACE(static_cast<u32>(testCase.pattern));
    SCOPED_TRACE(static_cast<u32>(testCase.guidance));
    const u32 halfWidth = DivideUp(testCase.width, static_cast<u32>(NWB_SW_SHADOW_SOFT_FACTOR));
    const u32 halfHeight = DivideUp(testCase.height, static_cast<u32>(NWB_SW_SHADOW_SOFT_FACTOR));
    const usize fullCount = static_cast<usize>(testCase.width) * testCase.height;
    const usize halfCount = static_cast<usize>(halfWidth) * halfHeight;
    const u32 layerCount = testCase.guidedFit ? ShadowResolveGuidedFit::s_LayerCount : 1u;
    const bool wavelet = testCase.stage == NWB_SHADOW_RESOLVE_STAGE_WAVELET;
    const u32 outputWidth = wavelet ? halfWidth : testCase.width;
    const u32 outputHeight = wavelet ? halfHeight : testCase.height;
    HalfPixels colors(scratchArena);
    HalfPixels geometry(scratchArena);
    HalfPixels moments(scratchArena);
    HalfPixels depth(scratchArena);
    HalfPixels world(scratchArena);
    HalfPixels normals(scratchArena);
    HalfPixels prior(scratchArena);
    colors.reserve(halfCount * layerCount);
    geometry.reserve(halfCount);
    moments.reserve(halfCount * layerCount);
    depth.reserve(fullCount);
    world.reserve(fullCount);
    normals.reserve(fullCount);
    prior.reserve(fullCount * layerCount);
    for(u32 y = 0u; y < halfHeight; ++y){
        for(u32 x = 0u; x < halfWidth; ++x){
            Float4U color = Ramp(static_cast<f32>(x), static_cast<f32>(y));
            if(testCase.pattern == Pattern::Uniform)
                color = { 0.25f, 0.5f, 0.75f, 1.0f };
            else if(testCase.pattern == Pattern::Edge)
                color = x < halfWidth / 2u ? Float4U{ 0.125f, 0.5f, 0.875f, 1.0f } : Float4U{ 0.875f, 0.5f, 0.125f, 1.0f };
            Float4U guide{ 0.0f, 0.0f, 10.0f, 1.0f };
            if(testCase.guidance == Guidance::LinearDistanceOffset)
                guide.z += static_cast<f32>(x);
            const bool alternate = ((x + y) & 1u) != 0u;
            if(testCase.guidance == Guidance::AllInvalid || (testCase.guidance == Guidance::MixedInvalid && alternate)){
                guide.w = 0.0f;
                color = { 1.0f, 0.0f, 1.0f, 1.0f };
            }
            if(testCase.guidance == Guidance::OppositeNormals || (testCase.guidance == Guidance::MixedNormals && alternate)){
                guide.x = 1.0f;
                guide.y = 1.0f;
                if(testCase.guidance == Guidance::MixedNormals)
                    color = { 1.0f, 0.0f, 1.0f, 1.0f };
            }
            if(testCase.guidedFit){
                color = ShadowResolveGuidedFit::Color(*testCase.guidedFit, x, y, 0u);
                guide = ShadowResolveGuidedFit::Geometry(*testCase.guidedFit, x, y);
            }
            colors.push_back(ToHalfPixel(color));
            geometry.push_back(ToHalfPixel(guide));
            moments.push_back(ToHalfPixel({ 0.5f, 0.5f, 8.0f, 1.0f }));
        }
    }
    for(u32 layer = 1u; layer < layerCount; ++layer){
        for(u32 y = 0u; y < halfHeight; ++y){
            for(u32 x = 0u; x < halfWidth; ++x){
                colors.push_back(ToHalfPixel(ShadowResolveGuidedFit::Color(*testCase.guidedFit, x, y, layer)));
                moments.push_back(ToHalfPixel({ 0.5f, 0.5f, 8.0f, 1.0f }));
            }
        }
    }
    for(u32 y = 0u; y < testCase.height; ++y){
        for(u32 x = 0u; x < testCase.width; ++x){
            const bool background = testCase.backgroundBorder && (x == 0u || y == 0u);
            depth.push_back(ToHalfPixel({ background ? 1.0f : 0.5f, 0.0f, 0.0f, 1.0f }));
            // Most cases isolate resampling; the offset-distance case also exercises the regularized guided fit.
            Float4U position{ 0.0f, 0.0f, 10.0f, 1.0f };
            if(testCase.guidance == Guidance::LinearDistanceOffset)
                position.z += static_cast<f32>(x) * 0.5f + s_LinearGuidanceOffset;
            if(testCase.guidedFit)
                position.z = ShadowResolveGuidedFit::ReceiverDistance(*testCase.guidedFit, x);
            world.push_back(ToHalfPixel(position));
            normals.push_back(ToHalfPixel({ 0.5f, 0.5f, 1.0f, 1.0f }));
            const Float4U initial = testCase.guidedFit
                ? ShadowResolveGuidedFit::Prior(y, testCase.height, 0u) : PriorVisibility(y, testCase.height);
            prior.push_back(ToHalfPixel(initial));
        }
    }

    for(u32 layer = 1u; layer < layerCount; ++layer){
        for(u32 y = 0u; y < testCase.height; ++y){
            for(u32 x = 0u; x < testCase.width; ++x)
                prior.push_back(ToHalfPixel(ShadowResolveGuidedFit::Prior(y, testCase.height, layer)));
        }
    }

    TextureDesc fullDesc;
    fullDesc
        .setWidth(testCase.width)
        .setHeight(testCase.height)
        .setFormat(Format::RGBA16_FLOAT)
        .setInitialState(ResourceStates::Common)
        .setKeepInitialState(true)
    ;
    TextureDesc halfDesc = fullDesc;
    halfDesc.setWidth(halfWidth).setHeight(halfHeight);
    TextureDesc colorDesc = halfDesc;
    colorDesc.setDimension(TextureDimension::Texture2DArray).setArraySize(layerCount);
    const TextureHandle sources[] = {
        graphicsDevice.createTexture(colorDesc), graphicsDevice.createTexture(halfDesc),
        graphicsDevice.createTexture(fullDesc), graphicsDevice.createTexture(fullDesc), graphicsDevice.createTexture(fullDesc),
        graphicsDevice.createTexture(colorDesc)
    };
    const HalfPixels* const data[] = { &colors, &geometry, &depth, &world, &normals, &moments };
    TextureDesc outputDesc = fullDesc;
    outputDesc
        .setWidth(outputWidth)
        .setHeight(outputHeight)
        .setDimension(TextureDimension::Texture2DArray)
        .setArraySize(layerCount)
        .setInUAV(true)
    ;
    const TextureHandle output = graphicsDevice.createTexture(outputDesc);
    const StagingTextureHandle readback = graphicsDevice.createStagingTexture(outputDesc, CpuAccessMode::Read);
    ASSERT_TRUE(output);
    ASSERT_TRUE(readback);
    BufferDesc sceneDesc;
    sceneDesc.setByteSize(sizeof(Float4U)).setIsConstantBuffer(true).setInitialState(ResourceStates::Common).setKeepInitialState(true);
    const BufferHandle scene = graphicsDevice.createBuffer(sceneDesc);
    ASSERT_TRUE(scene);
    auto& heap = graphicsDevice.getDescriptorHeap();
    GpuDescriptorHandle descriptors[8]{};
    ScopeExit releaseDescriptors([&]()noexcept{
        for(const GpuDescriptorHandle descriptor : descriptors){
            if(descriptor.valid())
                heap.free(descriptor);
        }
        heap.collectRetired();
    });
    for(u32 index = 0u; index < LengthOf(sources); ++index){
        ASSERT_TRUE(sources[index]);
        descriptors[index] = heap.allocate((index == 0u || index == 5u) ? GpuDescriptorClass::SampledImage2DArray : GpuDescriptorClass::SampledImage);
        ASSERT_TRUE(descriptors[index].valid());
        ASSERT_TRUE(heap.write(descriptors[index], DescriptorWriteItem::Texture_SRV(0u, sources[index].get())));
    }
    descriptors[6] = heap.allocate(GpuDescriptorClass::StorageImage);
    descriptors[7] = heap.allocate(GpuDescriptorClass::UniformBuffer);
    ASSERT_TRUE(descriptors[6].valid());
    ASSERT_TRUE(descriptors[7].valid());
    ASSERT_TRUE(heap.write(descriptors[6], DescriptorWriteItem::Texture_UAV(0u, output.get())));
    ASSERT_TRUE(heap.write(descriptors[7], DescriptorWriteItem::ConstantBuffer(0u, scene.get())));
    const CommandListHandle commandList = graphicsDevice.createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
    for(u32 index = 0u; index < LengthOf(sources); ++index){
        const usize pitch = (index < 2u || index == 5u ? halfWidth : testCase.width) * sizeof(HalfPixel);
        const u32 sourceLayers = (index == 0u || index == 5u) ? layerCount : 1u;
        for(u32 layer = 0u; layer < sourceLayers; ++layer)
            ASSERT_TRUE(commandList->tryWriteTexture(*sources[index], layer, 0u, data[index]->data() + layer * halfCount, pitch));
        commandList->setTextureState(sources[index].get(), s_AllSubresources, ResourceStates::ShaderResource);
    }
    const Float4U cameraPosition{ 0.0f, 0.0f, 0.0f, 1.0f };
    ASSERT_TRUE(commandList->tryWriteBuffer(*scene, &cameraPosition, sizeof(cameraPosition)));
    commandList->setBufferState(scene.get(), ResourceStates::ConstantBuffer);
    for(u32 layer = 0u; layer < layerCount; ++layer)
        ASSERT_TRUE(commandList->tryWriteTexture(*output, layer, 0u, prior.data() + layer * fullCount, testCase.width * sizeof(HalfPixel)));
    commandList->setTextureState(output.get(), s_AllSubresources, ResourceStates::UnorderedAccess, true);
    commandList->commitBarriers();
    ComputeState state;
    state.setPipeline(&pipeline);
    commandList->setComputeState(state);
    heap.bindCompute(*commandList, pipeline);
    const ResolvePushConstants push{
        testCase.width, testCase.height, halfWidth, halfHeight, testCase.stepWidth, testCase.stage,
        testCase.guidedFit ? ShadowResolveGuidedFit::s_ActiveStart : 0u,
        testCase.guidedFit ? ShadowResolveGuidedFit::s_ActiveCount : 1u,
        testCase.momentsValid ? 1u : 0u, testCase.multiply ? 1u : 0u,
        descriptors[1].slot(), descriptors[2].slot(), descriptors[3].slot(), descriptors[4].slot(),
        descriptors[0].slot(), descriptors[0].slot(), descriptors[5].slot(), descriptors[6].slot(),
        descriptors[6].slot(), descriptors[7].slot(), 0u
    };
    commandList->setPushConstants(&push, sizeof(push));
    commandList->dispatch(
        DivideUp(outputWidth, static_cast<u32>(NWB_SHADOW_RESOLVE_GROUP_SIZE)),
        DivideUp(outputHeight, static_cast<u32>(NWB_SHADOW_RESOLVE_GROUP_SIZE)), 1u
    );
    for(u32 layer = 0u; layer < layerCount; ++layer)
        commandList->copyTexture(*readback, TextureSlice{}.setArraySlice(layer), *output, TextureSlice{}.setArraySlice(layer));
    ASSERT_FALSE(commandList->commandRecordingFailed());
    commandList->close();
    ASSERT_FALSE(commandList->commandRecordingFailed());
    CommandList* const commandLists[] = { commandList.get() };
    const QueueSubmissionToken token = graphicsDevice.executeCommandLists(
        commandLists, LengthOf(commandLists), commandList->getDescription().physicalQueue, QueueSubmissionDesc{}
    );
    ASSERT_TRUE(token.valid());
    ASSERT_TRUE(graphicsDevice.waitForIdle());
    actual.clear();
    actual.reserve(static_cast<usize>(outputWidth) * outputHeight * layerCount);
    for(u32 layer = 0u; layer < layerCount; ++layer){
        usize rowPitch = 0u;
        const u8* const mapped = static_cast<const u8*>(
            graphicsDevice.mapStagingTexture(*readback, TextureSlice{}.setArraySlice(layer), CpuAccessMode::Read, &rowPitch)
        );
        ASSERT_NE(mapped, nullptr);
        ScopeExit unmap([&]()noexcept{ graphicsDevice.unmapStagingTexture(*readback); });
        ASSERT_GE(rowPitch, outputWidth * sizeof(HalfPixel));
        for(u32 y = 0u; y < outputHeight; ++y){
            for(u32 x = 0u; x < outputWidth; ++x){
                HalfPixel pixel;
                NWB_MEMCPY(&pixel, sizeof(pixel), mapped + y * rowPitch + x * sizeof(pixel), sizeof(pixel));
                actual.push_back({
                    ConvertHalfToFloat(pixel.r), ConvertHalfToFloat(pixel.g), ConvertHalfToFloat(pixel.b), ConvertHalfToFloat(pixel.a)
                });
            }
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ShadowResolveKernelTest : public ShadowKernelTest{
protected:
    [[nodiscard]] bool loadResolveKernel(
        Alloc::ScratchArena& scratchArena,
        ComputePipelineHandle& outPipeline,
        bool rgb = true,
        u32 compiledStage = NWB_SHADOW_RESOLVE_STAGE_UPSAMPLE
    );
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ShadowResolveKernelTest::loadResolveKernel(
    Alloc::ScratchArena& scratchArena,
    ComputePipelineHandle& outPipeline,
    const bool rgb,
    const u32 compiledStage){
    auto& graphicsDevice = device();
    auto& heap = graphicsDevice.getDescriptorHeap();
    auto& memoryArena = arena();
    const Path sourceRoot(memoryArena, NWB_SHADOW_KERNEL_SOURCE_ROOT);
    const Path kernelRoot = sourceRoot / "impl/assets/graphics/shadow";
    const Path sourcePath = kernelRoot / (rgb ? "shadow_resolve_rgb_cs.slang" : "shadow_resolve_cs.slang");
    const Path metadataPath = kernelRoot / (rgb ? "shadow_resolve_rgb_cs.nwb" : "shadow_resolve_cs.nwb");
    const Path outputRoot(memoryArena, NWB_SHADOW_KERNEL_OUTPUT_ROOT);
    ErrorCode directoryError;
    if(!CreateDirectories(outputRoot, directoryError) && directoryError)
        return false;
    const bool wavelet = compiledStage == NWB_SHADOW_RESOLVE_STAGE_WAVELET;
    const Path outputPath = outputRoot / (rgb
        ? (wavelet ? "shadow_resolve_rgb_wavelet_cs.spv" : "shadow_resolve_rgb_upsample_cs.spv")
        : (wavelet ? "shadow_resolve_wavelet_cs.spv" : "shadow_resolve_upsample_cs.spv")
    );
    Impl::ShaderCook shaderCook(memoryArena);
    Impl::ShaderCook::ShaderEntry entry(memoryArena);
    Impl::ShaderCook::CookVector<u8> bytecode(memoryArena);
    {
        const Common::LoggerRegistrationGuard cookLogger(*s_logger, Common::LoggerBreakPolicy::BreakOnFatal);
        const bool cooked = [&]{
            if(!shaderCook.parseShaderMeta(metadataPath, entry, scratchArena))
                return false;
            Impl::ShaderCook::CookVector<Path> includes(memoryArena);
            includes.push_back(kernelRoot);
            includes.push_back(sourceRoot / "impl/assets/graphics");
            Impl::ShaderCook::CookVector<Path> dependencies(memoryArena);
            if(!shaderCook.gatherShaderDependencies(sourcePath, includes, dependencies, scratchArena))
                return false;
            const Impl::ShaderCook::ShaderMacroDefinition definition{ "NWB_SHADOW_RESOLVE_COMPILED_STAGE", wavelet ? "1" : "2" };
            const Impl::ShaderCook::ShaderCompilerRequest request{
                .shaderName = rgb ? "tests/shadow_kernel/shadow_resolve_rgb_cs" : "tests/shadow_kernel/shadow_resolve_cs",
                .stage = entry.stage.view(),
                .targetProfile = entry.targetProfile.view(),
                .entryPoint = AStringView(entry.entryPoint.data(), entry.entryPoint.size()),
                .variantName = wavelet ? "NWB_SHADOW_RESOLVE_COMPILED_STAGE=1" : "NWB_SHADOW_RESOLVE_COMPILED_STAGE=2",
                .defines = &definition,
                .includeDirectories = includes,
                .dependencies = dependencies,
                .sourcePath = sourcePath,
                .outputPath = outputPath,
                .defineCount = 1u,
                .optimizationLevel = entry.optimizationLevel
            };
            return shaderCook.compileVariant(request, bytecode) && !bytecode.empty();
        }();
        if(!cooked){
            s_logger->emitErrorsToStderr();
            return false;
        }
    }
    ShaderDesc shaderDesc(memoryArena);
    shaderDesc.setShaderType(ShaderType::Compute).setEntryName(AStringView(entry.entryPoint.data(), entry.entryPoint.size()));
    const ShaderHandle shader = graphicsDevice.createShader(shaderDesc, bytecode.data(), bytecode.size());
    if(!shader)
        return false;
    BindingLayoutDesc layoutDesc(memoryArena);
    const u32 pushBytes = static_cast<u32>(sizeof(__hidden_shadow_resolve_kernel_tests::ResolvePushConstants));
    layoutDesc.setVisibility(ShaderType::Compute).addItem(BindingLayoutItem::PushConstants(0u, pushBytes));
    const BindingLayoutHandle layout = graphicsDevice.createBindingLayout(layoutDesc);
    if(!layout)
        return false;
    ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(shader)
        .addBindingLayout(layout)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    outPipeline = graphicsDevice.createComputePipeline(pipelineDesc);
    return outPipeline.get() != nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(ShadowResolveKernelTest, WaveletVariantsPreserveUniformVisibilityAndRejectInvalidGuidance){
    using namespace __hidden_shadow_resolve_kernel_tests;
    Alloc::ScratchArena scratchArena(Name("tests/smoke/shadow_kernel/resolve_wavelet_guidance"));
    const u32 steps[] = { 1u, NWB_SHADOW_RESOLVE_LDS_MAX_STEP, NWB_SHADOW_RESOLVE_LDS_MAX_STEP + 1u };
    const Guidance::Enum guides[] = { Guidance::Flat, Guidance::AllInvalid, Guidance::MixedInvalid };
    Pixels actual(scratchArena);
    for(const bool rgb : { false, true }){
        SCOPED_TRACE(rgb);
        ComputePipelineHandle pipeline;
        ASSERT_TRUE(loadResolveKernel(scratchArena, pipeline, rgb, NWB_SHADOW_RESOLVE_STAGE_WAVELET));
        for(const u32 step : steps){
            SCOPED_TRACE(step);
            for(const Guidance::Enum guidance : guides){
                SCOPED_TRACE(static_cast<u32>(guidance));
                const ResolveCase testCase{
                    .width = 35u, .height = 31u, .pattern = Pattern::Uniform, .guidance = guidance,
                    .stage = NWB_SHADOW_RESOLVE_STAGE_WAVELET, .stepWidth = step
                };
                ASSERT_NO_FATAL_FAILURE(RunResolveCase(device(), *pipeline, testCase, scratchArena, actual));
                const u32 width = DivideUp(testCase.width, static_cast<u32>(NWB_SW_SHADOW_SOFT_FACTOR));
                const u32 height = DivideUp(testCase.height, static_cast<u32>(NWB_SW_SHADOW_SOFT_FACTOR));
                ASSERT_EQ(actual.size(), static_cast<usize>(width) * height);
                for(u32 y = 0u; y < height; ++y){
                    for(u32 x = 0u; x < width; ++x){
                        SCOPED_TRACE(x);
                        SCOPED_TRACE(y);
                        const bool invalid = guidance == Guidance::AllInvalid
                            || (guidance == Guidance::MixedInvalid && ((x + y) & 1u) != 0u);
                        const Float4U expected = invalid ? Float4U{ 1.0f, 1.0f, 1.0f, 1.0f }
                            : rgb ? Float4U{ 0.25f, 0.5f, 0.75f, 1.0f } : Float4U{ 0.25f, 0.25f, 0.25f, 1.0f };
                        ExpectPixelNear(actual[y * width + x], expected);
                    }
                }
            }
        }
    }
}

TEST_F(ShadowResolveKernelTest, WaveletVariantsSmoothAnEdgeWithinItsVisibilityBounds){
    using namespace __hidden_shadow_resolve_kernel_tests;
    Alloc::ScratchArena scratchArena(Name("tests/smoke/shadow_kernel/resolve_wavelet_edge"));
    Pixels actual(scratchArena);
    for(const bool rgb : { false, true }){
        SCOPED_TRACE(rgb);
        ComputePipelineHandle pipeline;
        ASSERT_TRUE(loadResolveKernel(scratchArena, pipeline, rgb, NWB_SHADOW_RESOLVE_STAGE_WAVELET));
        for(const bool history : { false, true }){
            SCOPED_TRACE(history);
            for(const u32 step : { 1u, static_cast<u32>(NWB_SHADOW_RESOLVE_LDS_MAX_STEP + 1u) }){
                SCOPED_TRACE(step);
                const ResolveCase testCase{
                    .width = 35u, .height = 31u, .pattern = Pattern::Edge,
                    .stage = NWB_SHADOW_RESOLVE_STAGE_WAVELET, .stepWidth = step, .momentsValid = history
                };
                ASSERT_NO_FATAL_FAILURE(RunResolveCase(device(), *pipeline, testCase, scratchArena, actual));
                const u32 width = DivideUp(testCase.width, static_cast<u32>(NWB_SW_SHADOW_SOFT_FACTOR));
                const u32 height = DivideUp(testCase.height, static_cast<u32>(NWB_SW_SHADOW_SOFT_FACTOR));
                ASSERT_EQ(actual.size(), static_cast<usize>(width) * height);
                const usize edge = static_cast<usize>(height / 2u) * width + width / 2u;
                // A copy or wrong-stage dispatch cannot satisfy these strict changes on both sides of the half-resolution edge.
                EXPECT_GT(actual[edge - 1u].x, 0.125f + 0.001f);
                EXPECT_LT(actual[edge].x, 0.875f - 0.001f);
                for(const Float4U& pixel : actual){
                    EXPECT_TRUE(IsFinite(pixel.x) && IsFinite(pixel.y) && IsFinite(pixel.z));
                    EXPECT_GE(pixel.x, 0.125f - 0.001f);
                    EXPECT_LE(pixel.x, 0.875f + 0.001f);
                    EXPECT_FLOAT_EQ(pixel.w, 1.0f);
                    if(rgb){
                        EXPECT_NEAR(pixel.y, 0.5f, 0.001f);
                        EXPECT_NEAR(pixel.x + pixel.z, 1.0f, 0.002f);
                    }
                    else{
                        EXPECT_FLOAT_EQ(pixel.y, pixel.x);
                        EXPECT_FLOAT_EQ(pixel.z, pixel.x);
                    }
                }
            }
        }
    }
}

TEST_F(ShadowResolveKernelTest, FlatReceiverInterpolatesRampAndEdgeWithoutPairedPixelPlateaus){
    using namespace __hidden_shadow_resolve_kernel_tests;
    Alloc::ScratchArena scratchArena(Name("tests/smoke/shadow_kernel/resolve_ramp"));
    ComputePipelineHandle pipeline;
    ASSERT_TRUE(loadResolveKernel(scratchArena, pipeline));
    ResolveCase testCase;
    Pixels actual(scratchArena);
    ASSERT_NO_FATAL_FAILURE(RunResolveCase(device(), *pipeline, testCase, scratchArena, actual));
    ExpectInteriorRamp(testCase, actual);
    testCase.pattern = Pattern::Edge;
    ASSERT_NO_FATAL_FAILURE(RunResolveCase(device(), *pipeline, testCase, scratchArena, actual));
    const u32 edge = testCase.width / 2u;
    const usize row = static_cast<usize>(testCase.height / 2u) * testCase.width;
    EXPECT_GT(actual[row + edge - 1u].x - actual[row + edge - 2u].x, 0.03f);
    EXPECT_GT(actual[row + edge].x - actual[row + edge - 1u].x, 0.03f);
    EXPECT_GT(actual[row + edge - 2u].z - actual[row + edge - 1u].z, 0.03f);
    EXPECT_GT(actual[row + edge - 1u].z - actual[row + edge].z, 0.03f);
    for(u32 x = 1u; x < testCase.width; ++x){
        EXPECT_GE(actual[row + x].x + 0.001f, actual[row + x - 1u].x);
        EXPECT_LE(actual[row + x].z - 0.001f, actual[row + x - 1u].z);
        EXPECT_NEAR(actual[row + x].y, 0.5f, 0.001f);
    }
}

TEST_F(ShadowResolveKernelTest, VaryingDistanceGuidanceFitsRegularizedColorAtOffsetReceiver){
    using namespace __hidden_shadow_resolve_kernel_tests;
    Alloc::ScratchArena scratchArena(Name("tests/smoke/shadow_kernel/resolve_distance_fit"));
    ComputePipelineHandle pipeline;
    ASSERT_TRUE(loadResolveKernel(scratchArena, pipeline));
    ResolveCase testCase;
    testCase.guidance = Guidance::LinearDistanceOffset;
    Pixels actual(scratchArena);
    ASSERT_NO_FATAL_FAILURE(RunResolveCase(device(), *pipeline, testCase, scratchArena, actual));
    // An interior quadratic B-spline footprint has distance variance 1/4 at both integer and half-texel phases. The affine color fit therefore has a closed form; no shader tap enumeration is copied into this oracle.
    constexpr f32 s_DistanceVariance = 0.25f;
    for(u32 y = 2u; y + 3u < testCase.height; ++y){
        for(u32 x = 2u; x + 3u < testCase.width; ++x){
            SCOPED_TRACE(x);
            SCOPED_TRACE(y);
            const f32 sampleX = static_cast<f32>(x) * 0.5f;
            const f32 sampleY = static_cast<f32>(y) * 0.5f;
            const f32 centerDistance = 10.0f + sampleX + s_LinearGuidanceOffset;
            const f32 epsilon = 0.02f * centerDistance;
            const f32 fittedOffset = s_LinearGuidanceOffset * s_DistanceVariance / (s_DistanceVariance + epsilon * epsilon);
            const Float4U unfiltered = Ramp(sampleX, sampleY);
            Float4U expected = unfiltered;
            expected.x += 0.0625f * fittedOffset;
            expected.z -= 0.03125f * fittedOffset;
            const Float4U& pixel = actual[y * testCase.width + x];
            ExpectPixelNear(pixel, expected);
            EXPECT_GT(pixel.x - unfiltered.x, 0.075f);
            EXPECT_GT(unfiltered.z - pixel.z, 0.0375f);
        }
    }
}

TEST_F(ShadowResolveKernelTest, ScalarOpaqueResolveInterpolatesTheSameSampleLatticeAndOverwritesPrior){
    using namespace __hidden_shadow_resolve_kernel_tests;
    Alloc::ScratchArena scratchArena(Name("tests/smoke/shadow_kernel/resolve_scalar"));
    ComputePipelineHandle pipeline;
    ASSERT_TRUE(loadResolveKernel(scratchArena, pipeline, false));
    const ResolveCase testCase;
    Pixels actual(scratchArena);
    ASSERT_NO_FATAL_FAILURE(RunResolveCase(device(), *pipeline, testCase, scratchArena, actual));
    for(u32 y = 2u; y + 3u < testCase.height; ++y){
        for(u32 x = 2u; x + 3u < testCase.width; ++x){
            SCOPED_TRACE(x);
            SCOPED_TRACE(y);
            const f32 expected = Ramp(static_cast<f32>(x) * 0.5f, static_cast<f32>(y) * 0.5f).x;
            ExpectPixelNear(actual[y * testCase.width + x], { expected, expected, expected, 1.0f });
        }
    }
}

TEST_F(ShadowResolveKernelTest, TransparentFoldMultipliesOpaqueVisibilityAndPreservesBackground){
    using namespace __hidden_shadow_resolve_kernel_tests;
    Alloc::ScratchArena scratchArena(Name("tests/smoke/shadow_kernel/resolve_fold"));
    ComputePipelineHandle pipeline;
    ASSERT_TRUE(loadResolveKernel(scratchArena, pipeline));
    ResolveCase testCase;
    testCase.backgroundBorder = true;
    Pixels overwrite(scratchArena);
    Pixels multiplied(scratchArena);
    ASSERT_NO_FATAL_FAILURE(RunResolveCase(device(), *pipeline, testCase, scratchArena, overwrite));
    testCase.multiply = true;
    ASSERT_NO_FATAL_FAILURE(RunResolveCase(device(), *pipeline, testCase, scratchArena, multiplied));
    for(u32 y = 0u; y < testCase.height; ++y){
        for(u32 x = 0u; x < testCase.width; ++x){
            SCOPED_TRACE(x);
            SCOPED_TRACE(y);
            const usize index = static_cast<usize>(y) * testCase.width + x;
            const Float4U prior = PriorVisibility(y, testCase.height);
            if(x == 0u || y == 0u){
                ExpectPixelNear(overwrite[index], { 1.0f, 1.0f, 1.0f, 1.0f });
                ExpectPixelNear(multiplied[index], prior);
            }
            else{
                ExpectPixelNear(multiplied[index], {
                    overwrite[index].x * prior.x, overwrite[index].y * prior.y, overwrite[index].z * prior.z, 1.0f
                });
            }
        }
    }
}

TEST_F(ShadowResolveKernelTest, InvalidGuidanceUsesLitIdentityAndNormalFallbackStillInterpolates){
    using namespace __hidden_shadow_resolve_kernel_tests;
    Alloc::ScratchArena scratchArena(Name("tests/smoke/shadow_kernel/resolve_guidance"));
    ComputePipelineHandle pipeline;
    ASSERT_TRUE(loadResolveKernel(scratchArena, pipeline));
    Pixels actual(scratchArena);
    const Guidance::Enum cases[] = { Guidance::AllInvalid, Guidance::OppositeNormals, Guidance::MixedInvalid, Guidance::MixedNormals };
    for(const Guidance::Enum guidance : cases){
        ResolveCase testCase;
        testCase.guidance = guidance;
        testCase.pattern = guidance == Guidance::OppositeNormals ? Pattern::Ramp : Pattern::Uniform;
        ASSERT_NO_FATAL_FAILURE(RunResolveCase(device(), *pipeline, testCase, scratchArena, actual));
        if(guidance == Guidance::OppositeNormals){
            ExpectInteriorRamp(testCase, actual);
            continue;
        }
        const Float4U expected = guidance == Guidance::AllInvalid
            ? Float4U{ 1.0f, 1.0f, 1.0f, 1.0f } : Float4U{ 0.25f, 0.5f, 0.75f, 1.0f };
        for(const Float4U& pixel : actual)
            ExpectPixelNear(pixel, expected);
    }
}

TEST_F(ShadowResolveKernelTest, OddAndTinyExtentsKeepNormalizedFiniteBoundaryValues){
    using namespace __hidden_shadow_resolve_kernel_tests;
    Alloc::ScratchArena scratchArena(Name("tests/smoke/shadow_kernel/resolve_boundaries"));
    ComputePipelineHandle pipeline;
    ASSERT_TRUE(loadResolveKernel(scratchArena, pipeline));
    Pixels actual(scratchArena);
    const u32 dimensions[][2] = { { 1u, 1u }, { 1u, 7u }, { 7u, 1u }, { 7u, 9u }, { 9u, 7u } };
    for(const auto& dimension : dimensions){
        ResolveCase testCase;
        testCase.width = dimension[0];
        testCase.height = dimension[1];
        testCase.pattern = Pattern::Uniform;
        ASSERT_NO_FATAL_FAILURE(RunResolveCase(device(), *pipeline, testCase, scratchArena, actual));
        for(const Float4U& pixel : actual)
            ExpectPixelNear(pixel, { 0.25f, 0.5f, 0.75f, 1.0f });
        testCase.pattern = Pattern::Ramp;
        ASSERT_NO_FATAL_FAILURE(RunResolveCase(device(), *pipeline, testCase, scratchArena, actual));
        for(u32 y = 0u; y < testCase.height; ++y){
            for(u32 x = 0u; x < testCase.width; ++x){
                const usize index = static_cast<usize>(y) * testCase.width + x;
                const Float4U& pixel = actual[index];
                EXPECT_TRUE(IsFinite(pixel.x) && IsFinite(pixel.y) && IsFinite(pixel.z));
                EXPECT_GE(pixel.x, 0.125f - 0.001f);
                EXPECT_LE(pixel.x, 0.125f + 0.0625f * static_cast<f32>((testCase.width - 1u) / 2u) + 0.001f);
                EXPECT_GE(pixel.y, 0.1875f - 0.001f);
                EXPECT_LE(pixel.y, 0.1875f + 0.03125f * static_cast<f32>((testCase.height - 1u) / 2u) + 0.001f);
                EXPECT_GE(pixel.z, 0.0f);
                EXPECT_LE(pixel.z, 1.0f);
                EXPECT_FLOAT_EQ(pixel.w, 1.0f);
                if(x > 0u)
                    EXPECT_GE(pixel.x + 0.001f, actual[index - 1u].x);
                if(y > 0u)
                    EXPECT_GE(pixel.y + 0.001f, actual[index - testCase.width].y);
            }
        }
    }
}


TEST_F(ShadowResolveKernelTest, GuidedFitPreservesSignedExtrapolationAndOriginalMomentsAcrossLightLayers){
    using namespace __hidden_shadow_resolve_kernel_tests;
    namespace Fit = ShadowResolveGuidedFit;
    Alloc::ScratchArena scratchArena(Name("tests/smoke/shadow_kernel/resolve_guided_fit"));
    const Fit::Case cases[] = {
        {},
        { 0.03125f, 0.0078125f, 0.015625f },
        { 8192.0f, 8.0f, 64.0f },
        { 10.0f, 0.001f, 0.5f },
        { 10.0f, 0.0f, 2.0f },
        { 10.0f, 0.5f, 128.0f },
        { 10.0f, 1.0f, 2.0f, Fit::Mask::Mixed },
        { 10.0f, 1.0f, 2.0f, Fit::Mask::AllInvalid },
        { 10.0f, 1.0f, 2.0f, Fit::Mask::AllOpposite },
        { 10.0f, 1.0f, 2.0f, Fit::Mask::SingleGuided },
        { 10.0f, 0.5f, 128.0f, Fit::Mask::None, true },
    };
    Pixels actual(scratchArena);
    for(const bool rgb : { false, true }){
        SCOPED_TRACE(rgb);
        ComputePipelineHandle pipeline;
        ASSERT_TRUE(loadResolveKernel(scratchArena, pipeline, rgb));
        for(usize caseIndex = 0u; caseIndex < LengthOf(cases); ++caseIndex){
            SCOPED_TRACE(caseIndex);
            for(const bool multiply : { false, true }){
                if(multiply && !rgb)
                    continue;
                SCOPED_TRACE(multiply);
                const ResolveCase testCase{
                    .width = 17u, .height = 13u, .multiply = multiply, .backgroundBorder = true,
                    .guidedFit = &cases[caseIndex]
                };
                ASSERT_NO_FATAL_FAILURE(RunResolveCase(device(), *pipeline, testCase, scratchArena, actual));
                const usize layerPixels = static_cast<usize>(testCase.width) * testCase.height;
                ASSERT_EQ(actual.size(), layerPixels * Fit::s_LayerCount);
                for(u32 layer = 0u; layer < Fit::s_LayerCount; ++layer){
                    SCOPED_TRACE(layer);
                    for(u32 y = 0u; y < testCase.height; ++y){
                        for(u32 x = 0u; x < testCase.width; ++x){
                            SCOPED_TRACE(x);
                            SCOPED_TRACE(y);
                            const Float4U expected = Fit::Reference(cases[caseIndex], testCase.width, testCase.height, x, y, layer, rgb, multiply);
                            ExpectPixelNear(actual[layer * layerPixels + y * testCase.width + x], expected);
                        }
                    }
                }
                if(caseIndex == 0u && !multiply){
                    // At (6,4), the original fit overshoots R above one and G below zero before final clamping.
                    const usize index = Fit::s_ActiveStart * layerPixels + 4u * testCase.width + 6u;
                    EXPECT_FLOAT_EQ(actual[index].x, 1.0f);
                    if(rgb){
                        EXPECT_FLOAT_EQ(actual[index].y, 0.0f);
                        const f32 expectedBlue = 0.375f + 0.0625f * (2.0f * 0.25f / (0.25f + 0.3f * 0.3f));
                        EXPECT_NEAR(actual[index].z, expectedBlue, 0.001f);
                    }
                    // The next layer rotates G into scalar/R; layer identity must not collapse to the first layer.
                    EXPECT_FLOAT_EQ(actual[index + layerPixels].x, 0.0f);
                }
            }
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

