// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shadow_kernel_fixture.h"
#include "resolve_guided_fit_cases.h"

#include <impl/assets/graphics/shadow/shadow_resolve_binding_slots.h>
#include <impl/assets_shader/cook.h>

#include <global/algorithm.h>
#include <global/filesystem.h>
#include <global/math/convert.h>
#include <global/scope_exit.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;
constexpr u32 s_HalfDivisor = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_combined_resolve_kernel_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct PushConstants{
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
    u32 opaqueInputColorSlot;
};

struct HalfPixel{
    u16 r;
    u16 g;
    u16 b;
    u16 a;
};

struct Case{
    ShadowResolveGuidedFit::Case fit{};
    u32 width = 17u;
    u32 height = 13u;
    u32 slotStart = ShadowResolveGuidedFit::s_ActiveStart;
    u32 slotCount = ShadowResolveGuidedFit::s_ActiveCount;
};

using HalfPixels = Vector<HalfPixel, Alloc::ScratchArena>;

inline constexpr ShadowResolveGuidedFit::ReferenceInput s_OpaqueInput{ 1u, 0.719f, 0.137f };

static_assert(sizeof(PushConstants) == 84u);
static_assert(offsetof(PushConstants, opaqueInputColorSlot) == 80u);
static_assert(sizeof(HalfPixel) == 8u);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] HalfPixel Pack(const Float4U& value){
    return { ConvertFloatToHalf(value.x), ConvertFloatToHalf(value.y), ConvertFloatToHalf(value.z), ConvertFloatToHalf(value.w) };
}

// An image store may round differently from an arithmetic half conversion. Bound their difference using adjacent finite half values.
[[nodiscard]] f32 HalfStep(const f32 value){
    const u16 word = ConvertFloatToHalf(Clamp(value, 0.0f, 1.0f));
    const f32 center = ConvertHalfToFloat(word);
    const f32 lower = ConvertHalfToFloat(static_cast<u16>(word == 0u ? 0u : word - 1u));
    const f32 upper = ConvertHalfToFloat(static_cast<u16>(word + 1u));
    return Max(center - lower, upper - center);
}

[[nodiscard]] HalfPixel InitialPixel(const u32 x, const u32 y, const u32 layer){
    return Pack({ 0.125f + 0.0625f * static_cast<f32>(layer), y % s_ExpectedDualCount == 0u ? 0.5f : 0.0f, 0.75f, x % s_ExpectedDualCount == 0u ? 0.25f : 0.375f });
}

void RunCase(
    GraphicsBackend::Device& device,
    ComputePipeline& opaquePipeline,
    ComputePipeline& rgbPipeline,
    ComputePipeline& combinedPipeline,
    const Case& testCase,
    Alloc::ScratchArena& scratchArena){
    SCOPED_TRACE(testCase.width);
    SCOPED_TRACE(testCase.height);
    SCOPED_TRACE(testCase.fit.baseDistance);
    SCOPED_TRACE(testCase.fit.distanceStep);
    SCOPED_TRACE(testCase.fit.receiverOffset);
    SCOPED_TRACE(static_cast<u32>(testCase.fit.mask));
    constexpr u32 layers = ShadowResolveGuidedFit::s_LayerCount;
    const u32 halfWidth = DivideUp(testCase.width, s_HalfDivisor);
    const u32 halfHeight = DivideUp(testCase.height, s_HalfDivisor);
    const u32 outputWidth = testCase.width + 3u;
    const u32 outputHeight = testCase.height + s_ExpectedDualCount;
    const usize halfCount = static_cast<usize>(halfWidth) * halfHeight;
    const ShadowResolveGuidedFit::ReferenceInput rgbReference{ .slotStart = testCase.slotStart, .slotCount = testCase.slotCount };
    ShadowResolveGuidedFit::ReferenceInput opaqueReference = s_OpaqueInput;
    opaqueReference.slotStart = testCase.slotStart;
    opaqueReference.slotCount = testCase.slotCount;
    HalfPixels opaque(scratchArena), rgb(scratchArena), geometry(scratchArena);
    HalfPixels depth(scratchArena), world(scratchArena), normals(scratchArena), initial(scratchArena);
    opaque.reserve(halfCount * layers);
    rgb.reserve(halfCount * layers);
    geometry.reserve(halfCount);
    depth.reserve(static_cast<usize>(testCase.width) * testCase.height);
    world.reserve(static_cast<usize>(testCase.width) * testCase.height);
    normals.reserve(static_cast<usize>(testCase.width) * testCase.height);
    initial.reserve(static_cast<usize>(outputWidth) * outputHeight * layers);
    for(u32 layer = 0u; layer < layers; ++layer){
        for(u32 y = 0u; y < halfHeight; ++y){
            for(u32 x = 0u; x < halfWidth; ++x){
                const Float4U color = ShadowResolveGuidedFit::Color(testCase.fit, x, y, layer);
                // Distinct, non-binary opaque values expose a missing intermediate half conversion and swapped inputs.
                const Float4U opaqueColor = ShadowResolveGuidedFit::Color(testCase.fit, x, y, layer + s_OpaqueInput.layerOffset);
                opaque.push_back(Pack({ s_OpaqueInput.bias + s_OpaqueInput.scale * opaqueColor.x, 0.0f, 0.0f, 1.0f }));
                rgb.push_back(Pack(color));
                if(layer == 0u)
                    geometry.push_back(Pack(ShadowResolveGuidedFit::Geometry(testCase.fit, x, y)));
            }
        }
        for(u32 y = 0u; y < outputHeight; ++y){
            for(u32 x = 0u; x < outputWidth; ++x)
                initial.push_back(InitialPixel(x, y, layer));
        }
    }
    for(u32 y = 0u; y < testCase.height; ++y){
        for(u32 x = 0u; x < testCase.width; ++x){
            depth.push_back(Pack({ x == 0u || y == 0u ? 1.0f : 0.5f, 0.0f, 0.0f, 1.0f }));
            world.push_back(Pack({ 0.0f, 0.0f, ShadowResolveGuidedFit::ReceiverDistance(testCase.fit, x), 1.0f }));
            normals.push_back(Pack({ 0.5f, 0.5f, 1.0f, 1.0f }));
        }
    }
    TextureDesc fullDesc;
    fullDesc.setWidth(testCase.width).setHeight(testCase.height).setFormat(Format::RGBA16_FLOAT)
        .setInitialState(ResourceStates::Common).setKeepInitialState(true);
    TextureDesc halfDesc = fullDesc;
    halfDesc.setWidth(halfWidth).setHeight(halfHeight);
    TextureDesc arrayDesc = halfDesc;
    arrayDesc.setDimension(TextureDimension::Texture2DArray).setArraySize(layers);
    const TextureHandle sources[] = {
        device.createTexture(arrayDesc), device.createTexture(arrayDesc), device.createTexture(halfDesc),
        device.createTexture(fullDesc), device.createTexture(fullDesc), device.createTexture(fullDesc)
    };
    const HalfPixels* const data[] = { &opaque, &rgb, &geometry, &depth, &world, &normals };
    TextureDesc outputDesc = fullDesc;
    outputDesc.setWidth(outputWidth).setHeight(outputHeight).setDimension(TextureDimension::Texture2DArray)
        .setArraySize(layers).setInUAV(true);
    TextureHandle outputs[2];
    StagingTextureHandle readbacks[3];
    auto& heap = device.getDescriptorHeap();
    GpuDescriptorHandle descriptors[9]{};
    ScopeExit releaseDescriptors([&]()noexcept{
        for(const GpuDescriptorHandle descriptor : descriptors){
            if(descriptor.valid())
                heap.free(descriptor);
        }
        heap.collectRetired();
    });
    for(u32 index = 0u; index < LengthOf(sources); ++index){
        ASSERT_TRUE(sources[index]);
        descriptors[index] = heap.allocate(index < s_ExpectedDualCount ? GpuDescriptorClass::SampledImage2DArray : GpuDescriptorClass::SampledImage);
        ASSERT_TRUE(descriptors[index].valid());
        ASSERT_TRUE(heap.write(descriptors[index], DescriptorWriteItem::Texture_SRV(0u, sources[index].get())));
    }
    for(u32 arm = 0u; arm < s_ExpectedDualCount; ++arm){
        outputs[arm] = device.createTexture(outputDesc);
        readbacks[arm] = device.createStagingTexture(outputDesc, CpuAccessMode::Read);
        ASSERT_TRUE(outputs[arm]);
        ASSERT_TRUE(readbacks[arm]);
        descriptors[6u + arm] = heap.allocate(GpuDescriptorClass::StorageImage);
        ASSERT_TRUE(descriptors[6u + arm].valid());
        ASSERT_TRUE(heap.write(descriptors[6u + arm], DescriptorWriteItem::Texture_UAV(0u, outputs[arm].get())));
    }
    readbacks[2] = device.createStagingTexture(outputDesc, CpuAccessMode::Read);
    ASSERT_TRUE(readbacks[2]);
    BufferDesc sceneDesc;
    sceneDesc.setByteSize(sizeof(Float4U)).setIsConstantBuffer(true)
        .setInitialState(ResourceStates::Common).setKeepInitialState(true);
    const BufferHandle scene = device.createBuffer(sceneDesc);
    ASSERT_TRUE(scene);
    descriptors[8] = heap.allocate(GpuDescriptorClass::UniformBuffer);
    ASSERT_TRUE(descriptors[8].valid());
    ASSERT_TRUE(heap.write(descriptors[8], DescriptorWriteItem::ConstantBuffer(0u, scene.get())));
    const CommandListHandle commands = device.createCommandList();
    ASSERT_TRUE(commands);
    commands->open();
    for(u32 index = 0u; index < LengthOf(sources); ++index){
        const u32 sourceWidth = index < 3u ? halfWidth : testCase.width;
        const u32 sourceLayers = index < s_ExpectedDualCount ? layers : 1u;
        for(u32 layer = 0u; layer < sourceLayers; ++layer)
            ASSERT_TRUE(commands->tryWriteTexture(*sources[index], layer, 0u, data[index]->data() + layer * halfCount, sourceWidth * sizeof(HalfPixel)));
        commands->setTextureState(sources[index].get(), s_AllSubresources, ResourceStates::ShaderResource);
    }
    const Float4U camera{ 0.0f, 0.0f, 0.0f, 1.0f };
    ASSERT_TRUE(commands->tryWriteBuffer(*scene, &camera, sizeof(camera)));
    commands->setBufferState(scene.get(), ResourceStates::ConstantBuffer);
    for(u32 arm = 0u; arm < s_ExpectedDualCount; ++arm){
        for(u32 layer = 0u; layer < layers; ++layer){
            const usize offset = static_cast<usize>(layer) * outputWidth * outputHeight;
            ASSERT_TRUE(commands->tryWriteTexture(*outputs[arm], layer, 0u, initial.data() + offset, outputWidth * sizeof(HalfPixel)));
        }
        commands->setTextureState(outputs[arm].get(), s_AllSubresources, ResourceStates::UnorderedAccess, true);
    }
    commands->commitBarriers();
    PushConstants push{
        testCase.width, testCase.height, halfWidth, halfHeight, 1u, NWB_SHADOW_RESOLVE_STAGE_UPSAMPLE,
        testCase.slotStart, testCase.slotCount, 0u, 0u,
        descriptors[2].slot(), descriptors[3].slot(), descriptors[4].slot(), descriptors[5].slot(),
        descriptors[0].slot(), descriptors[0].slot(), descriptors[0].slot(), descriptors[6].slot(),
        descriptors[6].slot(), descriptors[8].slot(), descriptors[0].slot()
    };
    const auto dispatch = [&](ComputePipeline& pipeline){
        ComputeState state;
        state.setPipeline(&pipeline);
        commands->setComputeState(state);
        heap.bindCompute(*commands, pipeline);
        commands->setPushConstants(&push, sizeof(push));
        commands->dispatch(DivideUp(testCase.width, 8u), DivideUp(testCase.height, 8u), 1u);
    };
    dispatch(opaquePipeline);
    for(u32 layer = 0u; layer < layers; ++layer)
        commands->copyTexture(*readbacks[2], TextureSlice{}.setArraySlice(layer), *outputs[0], TextureSlice{}.setArraySlice(layer));
    commands->setTextureState(outputs[0].get(), s_AllSubresources, ResourceStates::UnorderedAccess, true);
    commands->commitBarriers();
    push.inputColorSlot = descriptors[1].slot();
    push.upsampleFold = 1u;
    dispatch(rgbPipeline);
    push.visibilityStorageSlot = descriptors[7].slot();
    push.outputStorageSlot = descriptors[7].slot();
    push.upsampleFold = 0u;
    dispatch(combinedPipeline);
    for(u32 arm = 0u; arm < s_ExpectedDualCount; ++arm){
        for(u32 layer = 0u; layer < layers; ++layer)
            commands->copyTexture(*readbacks[arm], TextureSlice{}.setArraySlice(layer), *outputs[arm], TextureSlice{}.setArraySlice(layer));
    }
    ASSERT_FALSE(commands->commandRecordingFailed());
    commands->close();
    ASSERT_FALSE(commands->commandRecordingFailed());
    CommandList* const submitted[] = { commands.get() };
    const QueueSubmissionToken token = device.executeCommandLists(
        submitted, LengthOf(submitted), commands->getDescription().physicalQueue, QueueSubmissionDesc{}
    );
    ASSERT_TRUE(token.valid());
    ASSERT_TRUE(device.waitForIdle());
    for(u32 layer = 0u; layer < layers; ++layer){
        SCOPED_TRACE(layer);
        const u8* mapped[3]{};
        usize pitches[3]{};
        ScopeExit unmap([&]()noexcept{
            for(u32 arm = 0u; arm < LengthOf(mapped); ++arm){
                if(mapped[arm] != nullptr)
                    device.unmapStagingTexture(*readbacks[arm]);
            }
        });
        for(u32 arm = 0u; arm < LengthOf(mapped); ++arm){
            mapped[arm] = static_cast<const u8*>(device.mapStagingTexture(
                *readbacks[arm], TextureSlice{}.setArraySlice(layer), CpuAccessMode::Read, &pitches[arm]
            ));
            ASSERT_NE(mapped[arm], nullptr);
        }
        for(u32 y = 0u; y < outputHeight; ++y){
            for(u32 x = 0u; x < outputWidth; ++x){
                SCOPED_TRACE(x);
                SCOPED_TRACE(y);
                HalfPixel pixels[3];
                for(u32 arm = 0u; arm < LengthOf(pixels); ++arm)
                    NWB_MEMCPY(&pixels[arm], sizeof(HalfPixel), mapped[arm] + y * pitches[arm] + x * sizeof(HalfPixel), sizeof(HalfPixel));
                const bool active = layer >= testCase.slotStart && layer < Min(testCase.slotStart + testCase.slotCount, layers);
                if(!active || x >= testCase.width || y >= testCase.height){
                    const HalfPixel expected = InitialPixel(x, y, layer);
                    for(u32 arm = 0u; arm < s_ExpectedDualCount; ++arm)
                        EXPECT_EQ(NWB_MEMCMP(&pixels[arm], &expected, sizeof(HalfPixel)), 0);
                }
                else if(x == 0u || y == 0u || testCase.fit.mask == ShadowResolveGuidedFit::Mask::AllInvalid){
                    const HalfPixel expected = Pack({ 1.0f, 1.0f, 1.0f, 1.0f });
                    for(u32 arm = 0u; arm < s_ExpectedDualCount; ++arm)
                        EXPECT_EQ(NWB_MEMCMP(&pixels[arm], &expected, sizeof(HalfPixel)), 0);
                }
                else{
                    const Float4U expectedRgb = ShadowResolveGuidedFit::Reference(
                        testCase.fit, testCase.width, testCase.height, x, y, layer, true, false, rgbReference
                    );
                    const f32 expectedOpaque = ShadowResolveGuidedFit::Reference(
                        testCase.fit, testCase.width, testCase.height, x, y, layer, false, false, opaqueReference
                    ).x;
                    const f32 storedOpaque = ConvertHalfToFloat(pixels[2].r);
                    EXPECT_NEAR(storedOpaque, expectedOpaque, 0.001f);
                    const u16 words[2][3] = {
                        { pixels[0].r, pixels[0].g, pixels[0].b },
                        { pixels[1].r, pixels[1].g, pixels[1].b }
                    };
                    for(u32 arm = 0u; arm < s_ExpectedDualCount; ++arm){
                        EXPECT_EQ(pixels[arm].a, ConvertFloatToHalf(1.0f));
                        for(u32 channel = 0u; channel < 3u; ++channel){
                            const f32 value = ConvertHalfToFloat(words[arm][channel]);
                            EXPECT_TRUE(IsFinite(value));
                            EXPECT_NEAR(value, expectedOpaque * expectedRgb.raw[channel], 0.001f);
                        }
                    }
                    for(u32 channel = 0u; channel < 3u; ++channel){
                        const f32 sequential = ConvertHalfToFloat(words[0][channel]);
                        const f32 combined = ConvertHalfToFloat(words[1][channel]);
                        // One opaque half step propagates through RGB; the final image conversion contributes at most one output step.
                        const f32 opaqueError = HalfStep(storedOpaque) * Min(expectedRgb.raw[channel] + 0.001f, 1.0f);
                        const f32 outputError = Max(HalfStep(sequential), HalfStep(combined));
                        EXPECT_NEAR(sequential, combined, opaqueError + outputError);
                    }
                }
            }
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CombinedShadowResolveKernelTest : public ShadowKernelTest{
protected:
    [[nodiscard]] bool loadPipelines(Alloc::ScratchArena& scratchArena, ComputePipelineHandle (&pipelines)[3]);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool CombinedShadowResolveKernelTest::loadPipelines(Alloc::ScratchArena& scratchArena, ComputePipelineHandle (&pipelines)[3]){
    auto& graphicsDevice = device();
    auto& heap = graphicsDevice.getDescriptorHeap();
    auto& memoryArena = arena();
    const Path sourceRoot(memoryArena, NWB_SHADOW_KERNEL_SOURCE_ROOT);
    const Path kernelRoot = sourceRoot / "impl/assets/graphics/shadow";
    const Path outputRoot(memoryArena, NWB_SHADOW_KERNEL_OUTPUT_ROOT);
    ErrorCode directoryError;
    if(!CreateDirectories(outputRoot, directoryError) && directoryError)
        return false;
    constexpr AStringView sources[] = { "shadow_resolve_cs", "shadow_resolve_rgb_cs", "shadow_resolve_combined_cs" };
    Impl::ShaderCook shaderCook(memoryArena);
    for(u32 index = 0u; index < LengthOf(sources); ++index){
        Path sourcePath = kernelRoot / sources[index];
        sourcePath.replace_extension(".slang");
        Path metadataPath = kernelRoot / sources[index];
        metadataPath.replace_extension(".nwb");
        Path outputPath = outputRoot / sources[index];
        outputPath.replace_extension(".combined_test.spv");
        Impl::ShaderCook::ShaderEntry entry(memoryArena);
        if(!shaderCook.parseShaderMeta(metadataPath, entry, scratchArena))
            return false;
        Impl::ShaderCook::CookVector<Path> includes(memoryArena);
        includes.push_back(kernelRoot);
        includes.push_back(sourceRoot / "impl/assets/graphics");
        Impl::ShaderCook::CookVector<Path> dependencies(memoryArena);
        if(!shaderCook.gatherShaderDependencies(sourcePath, includes, dependencies, scratchArena))
            return false;
        const Impl::ShaderCook::ShaderMacroDefinition definition{ "NWB_SHADOW_RESOLVE_COMPILED_STAGE", "2" };
        const Impl::ShaderCook::ShaderCompilerRequest request{
            .shaderName = sources[index],
            .stage = entry.stage.view(),
            .targetProfile = entry.targetProfile.view(),
            .entryPoint = AStringView(entry.entryPoint.data(), entry.entryPoint.size()),
            .variantName = index == s_ExpectedDualCount ? "default" : "NWB_SHADOW_RESOLVE_COMPILED_STAGE=2",
            .defines = &definition,
            .includeDirectories = includes,
            .dependencies = dependencies,
            .sourcePath = sourcePath,
            .outputPath = outputPath,
            .defineCount = index == s_ExpectedDualCount ? 0u : 1u,
            .optimizationLevel = entry.optimizationLevel
        };
        Impl::ShaderCook::CookVector<u8> bytecode(memoryArena);
        if(!shaderCook.compileVariant(request, bytecode) || bytecode.empty())
            return false;
        ShaderDesc shaderDesc(memoryArena);
        shaderDesc.setShaderType(ShaderType::Compute).setEntryName("main");
        const ShaderHandle shader = graphicsDevice.createShader(shaderDesc, bytecode.data(), bytecode.size());
        if(!shader)
            return false;
        BindingLayoutDesc layoutDesc(memoryArena);
        layoutDesc.setVisibility(ShaderType::Compute).addItem(BindingLayoutItem::PushConstants(0u, static_cast<u32>(sizeof(__hidden_combined_resolve_kernel_tests::PushConstants))));
        const BindingLayoutHandle layout = graphicsDevice.createBindingLayout(layoutDesc);
        if(!layout)
            return false;
        ComputePipelineDesc pipelineDesc;
        pipelineDesc.setComputeShader(shader).addBindingLayout(layout).addBindingLayout(heap.getResourceLayout()).addBindingLayout(heap.getSamplerLayout());
        pipelines[index] = graphicsDevice.createComputePipeline(pipelineDesc);
        if(!pipelines[index])
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(CombinedShadowResolveKernelTest, FusedUpsampleMatchesSequentialVisibilityWithinHalfStoragePrecision){
    using namespace __hidden_combined_resolve_kernel_tests;
    namespace Fit = ShadowResolveGuidedFit;
    const Common::LoggerRegistrationGuard diagnosticLogger(*s_logger, Common::LoggerBreakPolicy::BreakOnFatal);
    const auto run = [&](){
        Alloc::ScratchArena scratchArena(Name("tests/smoke/shadow_kernel/combined_resolve"));
        ComputePipelineHandle pipelines[3];
        ASSERT_TRUE(loadPipelines(scratchArena, pipelines));
        const Fit::Case cases[] = {
            {}, { 0.03125f, 0.0078125f, 0.015625f }, { 8192.0f, 8.0f, 64.0f },
            { 10.0f, 0.001f, 0.5f }, { 10.0f, 0.0f, 2.0f }, { 10.0f, 0.5f, 128.0f },
            { 10.0f, 1.0f, 2.0f, Fit::Mask::Mixed }, { 10.0f, 1.0f, 2.0f, Fit::Mask::AllInvalid },
            { 10.0f, 1.0f, 2.0f, Fit::Mask::AllOpposite }, { 10.0f, 1.0f, 2.0f, Fit::Mask::SingleGuided },
            { 10.0f, 0.5f, 128.0f, Fit::Mask::None, true }
        };
        for(const Fit::Case& fit : cases){
            const Case testCase{ .fit = fit };
            ASSERT_NO_FATAL_FAILURE(RunCase(device(), *pipelines[0], *pipelines[1], *pipelines[2], testCase, scratchArena));
        }
        const u32 sizes[][2] = { { 1u, 1u }, { 1u, 7u }, { 7u, 1u }, { 7u, 9u }, { 35u, 31u } };
        for(const auto& size : sizes){
            const Case testCase{ .fit = { 10.0f, 0.0f, 2.0f, Fit::Mask::None, true }, .width = size[0], .height = size[1] };
            ASSERT_NO_FATAL_FAILURE(RunCase(device(), *pipelines[0], *pipelines[1], *pipelines[2], testCase, scratchArena));
        }
        for(const u32 count : { 0u, 8u }){
            const Case testCase{ .slotStart = 7u, .slotCount = count };
            ASSERT_NO_FATAL_FAILURE(RunCase(device(), *pipelines[0], *pipelines[1], *pipelines[2], testCase, scratchArena));
        }
    };
    run();
    const TStringView validationPrefix = NWB_TEXT("Vulkan debug: [severity=error");
    const bool validationFailed = s_logger->sawMessageContaining(validationPrefix);
    if(HasFailure() || validationFailed || s_logger->errorCount() != 0u){
        s_logger->emitErrorsToStderr();
        s_logger->emitMessagesContainingToStderr(validationPrefix);
    }
    EXPECT_EQ(s_logger->errorCount(), 0u);
    EXPECT_FALSE(validationFailed);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

