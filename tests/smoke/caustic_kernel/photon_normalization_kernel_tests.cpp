// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "caustic_kernel_fixture.h"

#include <impl/assets_csg/cook.h>
#include <impl/assets_shader/cook.h>

#include <global/filesystem.h>
#include <global/math/convert.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_photon_normalization_kernel_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct Input{
    f32 domainArea;
    u32 photonCount;
    u32 targetCount;
    u32 reserved = 0u;
    Float4U light;
    Float4U origin{ 3.25f, -7.5f, 1.125f, 0.0f };
    Float4U direction{ 0.0f, 0.6f, 0.8f, 0.0f };
};

struct Output{
    Float4U origin;
    Float4U direction;
    Float4U flux;
};

struct Push{
    u32 inputSlot;
    u32 outputSlot;
    u32 count;
};

static_assert(sizeof(Input) == 64u);
static_assert(sizeof(Output) == 48u);
static_assert(sizeof(Push) == 12u);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool LoadKernel(
    GraphicsBackend::Device& device,
    Alloc::GlobalArena& arena,
    Alloc::ScratchArena& scratchArena,
    ComputePipelineHandle& outPipeline){
    const Path sourceRoot(arena, NWB_CAUSTIC_KERNEL_SOURCE_ROOT);
    const Path testRoot = sourceRoot / "tests/smoke/caustic_kernel/assets";
    const Path source = testRoot / "photon_normalization_cs.slang";
    const Path metadata = testRoot / "photon_normalization_cs.nwb";
    const Path outputRoot(arena, NWB_CAUSTIC_KERNEL_OUTPUT_ROOT);
    ErrorCode error;
    if(!CreateDirectories(outputRoot, error) && error)
        return false;
    const Path output = outputRoot / "photon_normalization_cs.spv";
    Impl::ShaderCook cook(arena);
    Impl::ShaderCook::ShaderEntry entry(arena);
    if(!cook.parseShaderMeta(metadata, entry, scratchArena))
        return false;
    Impl::ShaderCook::CookVector<Path> includes(arena);
    includes.push_back(sourceRoot / "impl/assets/graphics");
    includes.push_back(testRoot);
    // Dependency discovery follows disabled CSG includes reached through the shared material surface ABI.
    const Impl::AssetsCsgCook::CsgShapeCookEntryVector noShapes(arena);
    Path generatedRoot(arena);
    if(!Impl::AssetsCsgCook::EmitCsgShapeModuleIncludes(outputRoot, "photon_normalization", noShapes, generatedRoot, scratchArena))
        return false;
    includes.push_back(generatedRoot);
    Impl::ShaderCook::CookVector<Path> dependencies(arena);
    if(!cook.gatherShaderDependencies(source, includes, dependencies, scratchArena))
        return false;
    const Impl::ShaderCook::ShaderCompilerRequest request{
        .shaderName = "tests/caustic_kernel/photon_normalization_cs",
        .stage = entry.stage.view(),
        .targetProfile = entry.targetProfile.view(),
        .entryPoint = AStringView(entry.entryPoint.data(), entry.entryPoint.size()),
        .variantName = "default",
        .defines = nullptr,
        .includeDirectories = includes,
        .dependencies = dependencies,
        .sourcePath = source,
        .outputPath = output,
        .defineCount = 0u,
        .optimizationLevel = entry.optimizationLevel,
    };
    Impl::ShaderCook::CookVector<u8> bytecode(arena);
    if(!cook.compileVariant(request, bytecode) || bytecode.empty())
        return false;
    ShaderDesc shaderDesc(arena);
    shaderDesc.setShaderType(ShaderType::Compute).setEntryName(request.entryPoint);
    const ShaderHandle shader = device.createShader(shaderDesc, bytecode.data(), bytecode.size());
    if(!shader)
        return false;
    BindingLayoutDesc layoutDesc(arena);
    layoutDesc.setVisibility(ShaderType::Compute).addItem(BindingLayoutItem::PushConstants(0u, sizeof(Push)));
    const BindingLayoutHandle layout = device.createBindingLayout(layoutDesc);
    if(!layout)
        return false;
    ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(shader)
        .addBindingLayout(layout)
        .addBindingLayout(device.getDescriptorHeap().getResourceLayout())
        .addBindingLayout(device.getDescriptorHeap().getSamplerLayout())
    ;
    outPipeline = device.createComputePipeline(pipelineDesc);
    return outPipeline.get() != nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(CausticKernelTest, PhotonEnergySurvivesBudgetsBeyondFiniteHalfRange){
    using namespace __hidden_photon_normalization_kernel_tests;
    const Common::LoggerRegistrationGuard diagnostics(*s_logger, Common::LoggerBreakPolicy::BreakOnFatal);
    Alloc::ScratchArena scratchArena(Name("tests/caustic_kernel/photon_normalization"));
    ComputePipelineHandle pipeline;
    const bool loaded = LoadKernel(device(), arena(), scratchArena, pipeline);
    if(!loaded)
        s_logger->emitErrorsToStderr();
    ASSERT_TRUE(loaded);
    constexpr u32 s_Budgets[] = { 0u, 1u, 2u, 65503u, 65504u, 65505u, 65519u, 65520u, 65535u, 65536u, 131072u, 262144u };
    constexpr f32 s_Areas[] = { 2.0f, 4.0f, 256.0f };
    constexpr Float4U s_Lights[] = { { 1.0f, 0.5f, 2.0f, 0.0f }, { 0.1873f, 1.499f, 0.8123f, 0.0f } };
    Vector<Input, Alloc::ScratchArena> inputs(scratchArena);
    for(const u32 budget : s_Budgets){
        for(const f32 area : s_Areas){
            for(const Float4U& light : s_Lights)
                inputs.push_back(Input{ .domainArea = area, .photonCount = budget, .targetCount = 1u, .light = light });
        }
    }
    inputs.push_back(Input{ .domainArea = 4.0f, .photonCount = 131072u, .targetCount = 3u, .light = s_Lights[0] });
    inputs.push_back(Input{ .domainArea = 65536.0f, .photonCount = 131072u, .targetCount = 1u, .light = s_Lights[0] });
    inputs.push_back(Input{ .domainArea = 1.0f, .photonCount = 262144u, .targetCount = 65536u, .light = s_Lights[0] });
    inputs.push_back(Input{ .domainArea = 0.0f, .photonCount = 131072u, .targetCount = 1u, .light = s_Lights[0] });
    inputs.push_back(Input{ .domainArea = 4.0f, .photonCount = 131072u, .targetCount = 0u, .light = s_Lights[0] });
    inputs.push_back(Input{ .domainArea = 4.0f, .photonCount = 131072u, .targetCount = 1u, .light = {} });
    Vector<Output, Alloc::ScratchArena> sentinels(inputs.size() + 1u, scratchArena);
    NWB_MEMSET(sentinels.data(), 0xa5, sentinels.size() * sizeof(Output));
    BufferDesc inputDesc;
    inputDesc
        .setByteSize(inputs.size() * sizeof(Input))
        .setCanHaveRawViews(true)
        .setInitialState(ResourceStates::Common)
        .setKeepInitialState(true)
    ;
    BufferDesc outputDesc;
    outputDesc
        .setByteSize(sentinels.size() * sizeof(Output))
        .setCanHaveRawViews(true)
        .setCanHaveUAVs(true)
        .setCpuAccess(CpuAccessMode::Read)
        .setInitialState(ResourceStates::Common)
        .setKeepInitialState(true)
    ;
    const BufferHandle input = device().createBuffer(inputDesc);
    const BufferHandle output = device().createBuffer(outputDesc);
    ASSERT_TRUE(input && output);
    auto& heap = device().getDescriptorHeap();
    const GpuDescriptorHandle inputSlot = heap.allocate(GpuDescriptorClass::StorageBuffer);
    const GpuDescriptorHandle outputSlot = heap.allocate(GpuDescriptorClass::StorageBuffer);
    ScopeExit release([&]()noexcept{
        if(inputSlot.valid())
            heap.free(inputSlot);
        if(outputSlot.valid())
            heap.free(outputSlot);
        heap.collectRetired();
    });
    ASSERT_TRUE(inputSlot.valid() && outputSlot.valid());
    ASSERT_TRUE(heap.write(inputSlot, DescriptorWriteItem::RawBuffer_SRV(0u, input.get())));
    ASSERT_TRUE(heap.write(outputSlot, DescriptorWriteItem::RawBuffer_UAV(0u, output.get())));
    const CommandListHandle commands = device().createCommandList();
    ASSERT_TRUE(commands);
    commands->open();
    ASSERT_TRUE(commands->tryWriteBuffer(*input, inputs.data(), inputs.size() * sizeof(Input)));
    ASSERT_TRUE(commands->tryWriteBuffer(*output, sentinels.data(), sentinels.size() * sizeof(Output)));
    commands->setBufferState(input.get(), ResourceStates::ShaderResource);
    commands->setBufferState(output.get(), ResourceStates::UnorderedAccess, true);
    commands->commitBarriers();
    ComputeState state;
    state.setPipeline(pipeline.get());
    commands->setComputeState(state);
    heap.bindCompute(*commands, *pipeline);
    const Push push{ inputSlot.slot(), outputSlot.slot(), static_cast<u32>(inputs.size()) };
    commands->setPushConstants(&push, sizeof(push));
    commands->dispatch(DivideUp(push.count, 64u), 1u, 1u);
    ASSERT_FALSE(commands->commandRecordingFailed());
    commands->close();
    ASSERT_FALSE(commands->commandRecordingFailed());
    CommandList* const lists[] = { commands.get() };
    const QueueSubmissionToken token = device().executeCommandLists(
        lists, LengthOf(lists), commands->getDescription().physicalQueue, QueueSubmissionDesc{}
    );
    ASSERT_TRUE(token.valid());
    ASSERT_TRUE(device().waitForIdle());
    const Output* actual = static_cast<const Output*>(device().mapBuffer(*output, CpuAccessMode::Read));
    ASSERT_NE(actual, nullptr);
    ScopeExit unmap([&]()noexcept{ device().unmapBuffer(*output); });
    EXPECT_EQ(NWB_MEMCMP(&actual[inputs.size()], &sentinels[inputs.size()], sizeof(Output)), 0);
    for(usize index = 0u; index < inputs.size(); ++index){
        SCOPED_TRACE(index);
        const Input& source = inputs[index];
        const Output& observed = actual[index];
        EXPECT_EQ(NWB_MEMCMP(&observed.origin, &source.origin, sizeof(Float4U)), 0);
        EXPECT_EQ(NWB_MEMCMP(&observed.direction, &source.direction, sizeof(Float4U)), 0);
        EXPECT_FLOAT_EQ(observed.flux.w, 1.0f);
        const f32 light[] = { source.light.x, source.light.y, source.light.z };
        const f32 flux[] = { observed.flux.x, observed.flux.y, observed.flux.z };
        const f64 denominator = static_cast<f64>(Max(source.photonCount, 1u));
        for(usize channel = 0u; channel < 3u; ++channel){
            SCOPED_TRACE(channel);
            const f64 quantizedLight = ConvertHalfToFloat(ConvertFloatToHalf(light[channel]));
            const f64 energy = static_cast<f64>(source.domainArea) * static_cast<f64>(source.targetCount) * quantizedLight;
            const f64 exact = energy / denominator;
            const u16 expectedHalf = ConvertFloatToHalf(static_cast<f32>(exact));
            const u16 actualHalf = ConvertFloatToHalf(flux[channel]);
            EXPECT_TRUE(IsFinite(flux[channel]));
            // The fixture stores float(half(flux)); the float readback carries half quantization. Require the same half code rather than float identity with its half expansion.
            EXPECT_EQ(actualHalf, ConvertFloatToHalf(ConvertHalfToFloat(actualHalf)));
            // The oracle uses FP64 energy normalization and only final half quantization. One adjacent half permits FP32 arithmetic at a rounding midpoint; it cannot hide the original zero-flux failure.
            EXPECT_LE(Abs(static_cast<i32>(actualHalf) - static_cast<i32>(expectedHalf)), 1);
            if(exact > 0.0){
                EXPECT_GT(flux[channel], 0.0f);
                const f64 halfStep = static_cast<f64>(ConvertHalfToFloat(static_cast<u16>(expectedHalf + 1u)))
                    - static_cast<f64>(ConvertHalfToFloat(expectedHalf));
                EXPECT_NEAR(static_cast<f64>(flux[channel]) * denominator, energy, halfStep * denominator * 1.5);
            }
            else
                EXPECT_FLOAT_EQ(flux[channel], 0.0f);
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

