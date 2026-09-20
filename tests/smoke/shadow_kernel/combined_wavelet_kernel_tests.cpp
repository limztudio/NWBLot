// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shadow_kernel_fixture.h"

#include <impl/assets/graphics/shadow/shadow_resolve_binding_slots.h>
#include <impl/assets_shader/cook.h>

#include <global/filesystem.h>
#include <global/math/convert.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{
namespace __hidden_combined_wavelet_kernel_tests{


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
    u32 opaqueMomentsSlot;
    u32 opaqueMomentsValid;
    u32 opaqueOutputStorageSlot;
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
        IndependentEdges,
        MixedGuidance,
        Invalid,
    };
};

struct Case{
    u32 width = 17u;
    u32 height = 19u;
    Pattern::Enum pattern = Pattern::IndependentEdges;
    u32 momentsMask = 0u;
    u32 slotStart = 1u;
    u32 slotCount = 3u;
    f32 distance = 10.0f;
};

using HalfPixels = Vector<HalfPixel, Alloc::ScratchArena>;
constexpr u32 s_Layers = 8u;
static_assert(sizeof(PushConstants) == 96u);
static_assert(offsetof(PushConstants, opaqueInputColorSlot) == 80u);
static_assert(offsetof(PushConstants, opaqueMomentsSlot) == 84u);
static_assert(offsetof(PushConstants, opaqueMomentsValid) == 88u);
static_assert(offsetof(PushConstants, opaqueOutputStorageSlot) == 92u);
static_assert(sizeof(HalfPixel) == 8u);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] HalfPixel Pixel(const f32 r, const f32 g, const f32 b, const f32 a){
    return { ConvertFloatToHalf(r), ConvertFloatToHalf(g), ConvertFloatToHalf(b), ConvertFloatToHalf(a) };
}

// Uniform data cancels in real arithmetic, but the original filter rounds its denominator in half and its numerator in float.
void ExpectUniformWithinAccumulationError(const HalfPixel& pixel, const bool scalar){
    constexpr f64 halfUnitRoundoff = 1.0 / 2048.0;
    constexpr f64 floatUnitRoundoff = 1.0 / 16777216.0;
    // At most 25 positive taps: 24 rounded denominator additions, at most 50 numerator operations, then division and image storage.
    constexpr f64 denominatorError = (24.0 * halfUnitRoundoff) / (1.0 - 24.0 * halfUnitRoundoff);
    constexpr f64 numeratorError = (50.0 * floatUnitRoundoff) / (1.0 - 50.0 * floatUnitRoundoff);
    // Allow conservative FP32 division error and one FP16 ULP for image-store rounding, whose mode need not match arithmetic conversion.
    constexpr f64 relativeError = (1.0 + numeratorError) * (1.0 + 8.0 * floatUnitRoundoff)
        * (1.0 + 2.0 * halfUnitRoundoff) / (1.0 - denominatorError) - 1.0;
    const f64 expected[] = { scalar ? 0.25 : 0.5, 0.25, scalar ? 0.25 : 0.125 };
    const u16 actual[] = { pixel.r, pixel.g, pixel.b };
    for(u32 component = 0u; component < LengthOf(actual); ++component)
        EXPECT_NEAR(static_cast<f64>(ConvertHalfToFloat(actual[component])), expected[component], expected[component] * relativeError);
    EXPECT_EQ(pixel.a, ConvertFloatToHalf(1.0f));
}

void RunCase(
    GraphicsBackend::Device& device,
    ComputePipelineHandle (&pipelines)[3],
    const Case& testCase,
    Alloc::ScratchArena& scratchArena){
    SCOPED_TRACE(testCase.width);
    SCOPED_TRACE(testCase.height);
    SCOPED_TRACE(static_cast<u32>(testCase.pattern));
    SCOPED_TRACE(testCase.momentsMask);
    SCOPED_TRACE(testCase.distance);
    const u32 width = testCase.width;
    const u32 height = testCase.height;
    const usize pixelCount = static_cast<usize>(width) * height;
    const u32 outputWidth = width + 3u;
    const u32 outputHeight = height + 2u;
    HalfPixels opaque(scratchArena), rgb(scratchArena), opaqueMoments(scratchArena), rgbMoments(scratchArena), geometry(scratchArena);
    HalfPixels* const data[] = { &opaque, &rgb, &opaqueMoments, &rgbMoments, &geometry };
    for(u32 index = 0u; index < LengthOf(data); ++index)
        data[index]->reserve(pixelCount * (index == 4u ? 1u : s_Layers));
    const f32 historyCounts[] = { 0.0f, 3.0f, 4.0f, 8.0f };
    for(u32 layer = 0u; layer < s_Layers; ++layer){
        for(u32 y = 0u; y < height; ++y){
            for(u32 x = 0u; x < width; ++x){
                const f32 layerValue = static_cast<f32>(layer) * 0.015625f;
                f32 scalar = (x < width / 2u ? 0.09375f : 0.8125f) + layerValue;
                f32 red = (y < height / 2u ? 0.75f : 0.125f) + layerValue;
                f32 green = (x + 3u * y) % 3u == 0u ? 0.03125f : 0.5625f;
                f32 blue = 0.03125f * static_cast<f32>(1u + (x + 2u * y + layer) % 17u);
                if(testCase.pattern == Pattern::Uniform){
                    scalar = 0.25f;
                    red = 0.5f;
                    green = 0.25f;
                    blue = 0.125f;
                }
                opaque.push_back(Pixel(scalar, 0.0f, 1.0f, 1.0f));
                rgb.push_back(Pixel(red, green, blue, 1.0f));
                const u32 historyIndex = (x + y + layer) % static_cast<u32>(LengthOf(historyCounts));
                // Independent validity/counts and opposing variances expose cross-channel moment or sigma reuse.
                opaqueMoments.push_back(Pixel(0.5f, x % 2u == 0u ? 0.25f : 0.5f, historyCounts[historyIndex], 1.0f));
                rgbMoments.push_back(Pixel(0.25f, y % 2u == 0u ? 0.5f : 0.03125f, historyCounts[3u - historyIndex], 1.0f));
                if(layer == 0u){
                    HalfPixel guide = Pixel(0.0f, 0.0f, testCase.distance, 1.0f);
                    if(testCase.pattern == Pattern::Invalid)
                        guide.a = 0u;
                    else if(testCase.pattern == Pattern::MixedGuidance){
                        guide = Pixel(x % 3u == 0u ? 1.0f : 0.0f, y % 3u == 0u ? 1.0f : 0.0f,
                            testCase.distance + static_cast<f32>((x + y) % 4u) * 0.125f,
                            (x + 2u * y) % 5u == 0u ? 0.0f : 1.0f);
                    }
                    geometry.push_back(guide);
                }
            }
        }
    }
    TextureDesc sourceDesc;
    sourceDesc
        .setWidth(width)
        .setHeight(height)
        .setDimension(TextureDimension::Texture2DArray)
        .setArraySize(s_Layers)
        .setFormat(Format::RGBA16_FLOAT)
        .setInitialState(ResourceStates::Common)
        .setKeepInitialState(true)
    ;
    TextureDesc geometryDesc = sourceDesc;
    geometryDesc
        .setDimension(TextureDimension::Texture2D)
        .setArraySize(1u)
    ;
    TextureHandle sources[5];
    for(u32 index = 0u; index < LengthOf(sources); ++index){
        sources[index] = device.createTexture(index == 4u ? geometryDesc : sourceDesc);
        ASSERT_TRUE(sources[index]);
    }
    TextureDesc outputDesc = sourceDesc;
    outputDesc
        .setWidth(outputWidth)
        .setHeight(outputHeight)
        .setInUAV(true)
    ;
    TextureHandle outputs[4];
    StagingTextureHandle readbacks[4];
    auto& heap = device.getDescriptorHeap();
    GpuDescriptorHandle descriptors[9]{};
    ScopeExit releaseDescriptors([&]()noexcept{
        for(const auto descriptor : descriptors){
            if(descriptor.valid())
                heap.free(descriptor);
        }
        heap.collectRetired();
    });
    for(u32 index = 0u; index < LengthOf(sources); ++index){
        descriptors[index] = heap.allocate(index == 4u ? GpuDescriptorClass::SampledImage : GpuDescriptorClass::SampledImage2DArray);
        ASSERT_TRUE(descriptors[index].valid());
        ASSERT_TRUE(heap.write(descriptors[index], DescriptorWriteItem::Texture_SRV(0u, sources[index].get())));
    }
    for(u32 index = 0u; index < LengthOf(outputs); ++index){
        outputs[index] = device.createTexture(outputDesc);
        readbacks[index] = device.createStagingTexture(outputDesc, CpuAccessMode::Read);
        ASSERT_TRUE(outputs[index]);
        ASSERT_TRUE(readbacks[index]);
        descriptors[5u + index] = heap.allocate(GpuDescriptorClass::StorageImage);
        ASSERT_TRUE(descriptors[5u + index].valid());
        ASSERT_TRUE(heap.write(descriptors[5u + index], DescriptorWriteItem::Texture_UAV(0u, outputs[index].get())));
    }
    const CommandListHandle commands = device.createCommandList();
    ASSERT_TRUE(commands);
    commands->open();
    for(u32 index = 0u; index < LengthOf(sources); ++index){
        const u32 layers = index == 4u ? 1u : s_Layers;
        for(u32 layer = 0u; layer < layers; ++layer)
            ASSERT_TRUE(commands->tryWriteTexture(*sources[index], layer, 0u, data[index]->data() + pixelCount * layer, width * sizeof(HalfPixel)));
        commands->setTextureState(sources[index].get(), s_AllSubresources, ResourceStates::ShaderResource);
    }
    for(const auto& output : outputs){
        commands->clearTextureFloat(*output, s_AllSubresources, Color(19.0f));
        commands->setTextureState(output.get(), s_AllSubresources, ResourceStates::UnorderedAccess, true);
    }
    commands->commitBarriers();
    for(u32 program = 0u; program < LengthOf(pipelines); ++program){
        ComputeState state;
        state.setPipeline(pipelines[program].get());
        commands->setComputeState(state);
        heap.bindCompute(*commands, *pipelines[program]);
        const bool scalar = program == 0u;
        const u32 input = scalar ? 0u : 1u;
        PushConstants push{};
        push.width = width * 2u;
        push.height = height * 2u;
        push.halfWidth = width;
        push.halfHeight = height;
        // The combined route is eligible only for one first wavelet; compare that exact step against both generic kernels.
        push.stepWidth = 1u;
        push.stage = NWB_SHADOW_RESOLVE_STAGE_WAVELET;
        push.lightSlotStart = testCase.slotStart;
        push.lightSlotCount = testCase.slotCount;
        push.momentsValid = (testCase.momentsMask & (scalar ? 1u : 2u)) != 0u ? 1u : 0u;
        push.geometrySlot = descriptors[4].slot();
        push.inputColorSlot = descriptors[input].slot();
        push.softHalfSlot = push.inputColorSlot;
        push.momentsSlot = descriptors[input + 2u].slot();
        push.outputStorageSlot = descriptors[program == 2u ? 8u : 5u + program].slot();
        push.visibilityStorageSlot = push.outputStorageSlot;
        push.opaqueInputColorSlot = descriptors[0].slot();
        push.opaqueMomentsSlot = descriptors[2].slot();
        push.opaqueMomentsValid = (testCase.momentsMask & 1u) != 0u ? 1u : 0u;
        push.opaqueOutputStorageSlot = descriptors[7].slot();
        commands->setPushConstants(&push, sizeof(push));
        commands->dispatch(DivideUp(width, 8u), DivideUp(height, 8u), 1u);
    }
    for(u32 index = 0u; index < LengthOf(outputs); ++index){
        for(u32 layer = 0u; layer < s_Layers; ++layer)
            commands->copyTexture(*readbacks[index], TextureSlice{}.setArraySlice(layer), *outputs[index], TextureSlice{}.setArraySlice(layer));
    }
    ASSERT_FALSE(commands->commandRecordingFailed());
    commands->close();
    ASSERT_FALSE(commands->commandRecordingFailed());
    CommandList* const lists[] = { commands.get() };
    const auto token = device.executeCommandLists(lists, LengthOf(lists), commands->getDescription().physicalQueue, QueueSubmissionDesc{});
    ASSERT_TRUE(token.valid());
    ASSERT_TRUE(device.waitForIdle());
    const u32 slotEnd = Min(testCase.slotStart + testCase.slotCount, s_Layers);
    for(u32 layer = 0u; layer < s_Layers; ++layer){
        TextureSlice slice;
        slice.arraySlice = layer;
        usize pitches[4]{};
        const u8* mapped[4]{};
        ScopeExit unmap([&]()noexcept{
            for(u32 index = 0u; index < LengthOf(readbacks); ++index){
                if(mapped[index])
                    device.unmapStagingTexture(*readbacks[index]);
            }
        });
        for(u32 index = 0u; index < LengthOf(readbacks); ++index){
            mapped[index] = static_cast<const u8*>(device.mapStagingTexture(*readbacks[index], slice, CpuAccessMode::Read, &pitches[index]));
            ASSERT_NE(mapped[index], nullptr);
            ASSERT_GE(pitches[index], outputWidth * sizeof(HalfPixel));
        }
        for(u32 y = 0u; y < outputHeight; ++y){
            for(u32 x = 0u; x < outputWidth; ++x){
                HalfPixel values[4];
                for(u32 index = 0u; index < LengthOf(values); ++index)
                    NWB_MEMCPY(&values[index], sizeof(HalfPixel), mapped[index] + y * pitches[index] + x * sizeof(HalfPixel), sizeof(HalfPixel));
                for(u32 channel = 0u; channel < 2u; ++channel){
                    const auto& candidate = values[channel + 2u];
                    const auto& reference = values[channel];
                    EXPECT_EQ(candidate.r, reference.r) << layer << ":" << x << "," << y;
                    EXPECT_EQ(candidate.g, reference.g) << layer << ":" << x << "," << y;
                    EXPECT_EQ(candidate.b, reference.b) << layer << ":" << x << "," << y;
                    EXPECT_EQ(candidate.a, reference.a) << layer << ":" << x << "," << y;
                    if(x >= width || y >= height || layer < testCase.slotStart || layer >= slotEnd){
                        const HalfPixel guard = Pixel(19.0f, 19.0f, 19.0f, 19.0f);
                        EXPECT_EQ(NWB_MEMCMP(&candidate, &guard, sizeof(HalfPixel)), 0);
                    }
                    else{
                        EXPECT_EQ(candidate.a, ConvertFloatToHalf(1.0f));
                        EXPECT_TRUE(IsFinite(ConvertHalfToFloat(candidate.r)));
                        EXPECT_TRUE(IsFinite(ConvertHalfToFloat(candidate.g)));
                        EXPECT_TRUE(IsFinite(ConvertHalfToFloat(candidate.b)));
                        if(testCase.pattern == Pattern::Invalid){
                            const HalfPixel lit = Pixel(1.0f, 1.0f, 1.0f, 1.0f);
                            EXPECT_EQ(NWB_MEMCMP(&candidate, &lit, sizeof(HalfPixel)), 0);
                        }
                        if(testCase.pattern == Pattern::Uniform){
                            // Check the analytic constant independently for both arms; the candidate/reference comparisons above remain bit-exact.
                            ExpectUniformWithinAccumulationError(reference, channel == 0u);
                            ExpectUniformWithinAccumulationError(candidate, channel == 0u);
                        }
                    }
                }
            }
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CombinedShadowWaveletKernelTest : public ShadowKernelTest{
protected:
    [[nodiscard]] bool loadPipelines(Alloc::ScratchArena& scratchArena, ComputePipelineHandle (&pipelines)[3]);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



bool CombinedShadowWaveletKernelTest::loadPipelines(Alloc::ScratchArena& scratchArena, ComputePipelineHandle (&pipelines)[3]){
    auto& graphicsDevice = device();
    auto& heap = graphicsDevice.getDescriptorHeap();
    auto& memoryArena = arena();
    const Path sourceRoot(memoryArena, NWB_SHADOW_KERNEL_SOURCE_ROOT);
    const Path kernelRoot = sourceRoot / "impl/assets/graphics/shadow";
    const Path outputRoot(memoryArena, NWB_SHADOW_KERNEL_OUTPUT_ROOT);
    ErrorCode directoryError;
    if(!CreateDirectories(outputRoot, directoryError) && directoryError)
        return false;
    constexpr AStringView sources[] = { "shadow_resolve_cs", "shadow_resolve_rgb_cs", "shadow_resolve_wavelet_combined_cs" };
    Impl::ShaderCook shaderCook(memoryArena);
    for(u32 index = 0u; index < LengthOf(sources); ++index){
        Path sourcePath = kernelRoot / sources[index];
        sourcePath.replace_extension(".slang");
        if(index < 2u){
            sourcePath = sourceRoot / (index == 0u
                ? "tests/smoke/shadow_kernel/reference/shadow_resolve_before_wavelet_fusion.slang"
                : "tests/smoke/shadow_kernel/reference/shadow_resolve_rgb_before_wavelet_fusion.slang");
        }
        Path metadataPath = kernelRoot / sources[index];
        metadataPath.replace_extension(".nwb");
        Path outputPath = outputRoot / sources[index];
        outputPath.replace_extension(".combined_wavelet_test.spv");
        Impl::ShaderCook::ShaderEntry entry(memoryArena);
        if(!shaderCook.parseShaderMeta(metadataPath, entry, scratchArena))
            return false;
        Impl::ShaderCook::CookVector<Path> includes(memoryArena);
        includes.push_back(kernelRoot);
        includes.push_back(sourceRoot / "impl/assets/graphics");
        Impl::ShaderCook::CookVector<Path> dependencies(memoryArena);
        if(!shaderCook.gatherShaderDependencies(sourcePath, includes, dependencies, scratchArena))
            return false;
        const Impl::ShaderCook::ShaderMacroDefinition definition{ "NWB_SHADOW_RESOLVE_COMPILED_STAGE", "1" };
        const Impl::ShaderCook::ShaderCompilerRequest request{
            .shaderName = sources[index],
            .stage = entry.stage.view(),
            .targetProfile = entry.targetProfile.view(),
            .entryPoint = AStringView(entry.entryPoint.data(), entry.entryPoint.size()),
            .variantName = index == 2u ? "default" : "NWB_SHADOW_RESOLVE_COMPILED_STAGE=1",
            .defines = &definition,
            .includeDirectories = includes,
            .dependencies = dependencies,
            .sourcePath = sourcePath,
            .outputPath = outputPath,
            .defineCount = index == 2u ? 0u : 1u,
            .optimizationLevel = entry.optimizationLevel
        };
        Impl::ShaderCook::CookVector<u8> bytecode(memoryArena);
        if(!shaderCook.compileVariant(request, bytecode) || bytecode.empty())
            return false;
        ShaderDesc shaderDesc(memoryArena);
        shaderDesc
            .setShaderType(ShaderType::Compute)
            .setEntryName("main")
        ;
        const ShaderHandle shader = graphicsDevice.createShader(shaderDesc, bytecode.data(), bytecode.size());
        if(!shader)
            return false;
        BindingLayoutDesc layoutDesc(memoryArena);
        layoutDesc
            .setVisibility(ShaderType::Compute)
            .addItem(BindingLayoutItem::PushConstants(0u, static_cast<u32>(sizeof(__hidden_combined_wavelet_kernel_tests::PushConstants))))
        ;
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
        pipelines[index] = graphicsDevice.createComputePipeline(pipelineDesc);
        if(!pipelines[index])
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(CombinedShadowWaveletKernelTest, FusedChannelsMatchFrozenIndependentWaveletsExactly){
    using namespace __hidden_combined_wavelet_kernel_tests;
    const Common::LoggerRegistrationGuard diagnosticLogger(*s_logger, Common::LoggerBreakPolicy::BreakOnFatal);
    const auto run = [&](){
        Alloc::ScratchArena scratchArena(Name("tests/smoke/shadow_kernel/combined_wavelet"));
        ComputePipelineHandle pipelines[3];
        ASSERT_TRUE(loadPipelines(scratchArena, pipelines));
        const u32 extents[][2] = { { 1u, 1u }, { 7u, 9u }, { 9u, 7u }, { 17u, 19u } };
        for(const auto& extent : extents){
            for(u32 pattern = Pattern::Uniform; pattern <= Pattern::Invalid; ++pattern){
                for(u32 moments = 0u; moments < 4u; ++moments){
                    const Case testCase{
                        .width = extent[0], .height = extent[1], .pattern = static_cast<Pattern::Enum>(pattern),
                        .momentsMask = moments,
                    };
                    ASSERT_NO_FATAL_FAILURE(RunCase(device(), pipelines, testCase, scratchArena));
                }
            }
        }
        const Case additional[] = {
            { .momentsMask = 3u, .slotStart = 7u, .slotCount = 8u },
            { .momentsMask = 3u, .slotStart = 7u, .slotCount = 0u },
            { .pattern = Pattern::MixedGuidance, .distance = 0.03125f },
            { .pattern = Pattern::MixedGuidance, .momentsMask = 3u, .distance = 8192.0f },
        };
        for(const auto& testCase : additional)
            ASSERT_NO_FATAL_FAILURE(RunCase(device(), pipelines, testCase, scratchArena));
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

