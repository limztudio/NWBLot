// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shadow_kernel_fixture.h"
#include <impl/assets_shader/cook.h>
#include <global/filesystem.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_software_transparent_sampling_kernel_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct Input{
    Float4U world{ 0.0f, 0.0f, 0.5f, 1.0f };
    Float4U current{ 0.0f, 0.0f, 10.0f, 1.0f };
    Float4U previous{ 0.0f, 0.0f, 10.0f, 1.0f };
    Float4U moments{ 0.5f, 0.25f, 16.0f, 0.0f };
    u32 pixelX = s_ExpectedDualCount;
    u32 pixelY = s_ExpectedDualCount;
    u32 width = 16u;
    u32 height = 16u;
    u32 historyValid = 1u;
    u32 maximumSamples = 3u;
    u32 reserved[2]{};
};
struct Output{ u32 samples; u32 geometryAccepted; u32 previousX; u32 previousY; };
struct Push{ u32 input; u32 output; u32 count; };
static_assert(sizeof(Input) == 96u);
static_assert(sizeof(Output) == 16u);
static_assert(sizeof(Push) == 12u);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Input Stable(const u32 width, const u32 height, const u32 x, const u32 y){
    Input result;
    result.width = width;
    result.height = height;
    result.pixelX = x;
    result.pixelY = y;
    result.world.x = (static_cast<f32>(x * s_ExpectedDualCount) + 0.5f) / static_cast<f32>(width) * 2.0f - 1.0f;
    result.world.y = 1.0f - (static_cast<f32>(y * s_ExpectedDualCount) + 0.5f) / static_cast<f32>(height) * 2.0f;
    return result;
}

[[nodiscard]] bool LoadKernel(GraphicsBackend::Device& device, Alloc::GlobalArena& arena, Alloc::ScratchArena& scratch, ComputePipelineHandle& pipeline){
    const Path root(arena, NWB_SHADOW_KERNEL_SOURCE_ROOT);
    const Path testRoot = root / "tests/smoke/shadow_kernel/assets";
    const Path outputRoot(arena, NWB_SHADOW_KERNEL_OUTPUT_ROOT);
    ErrorCode error;
    if(!CreateDirectories(outputRoot, error) && error)
        return false;
    Impl::ShaderCook cook(arena);
    Impl::ShaderCook::ShaderEntry entry(arena);
    if(!cook.parseShaderMeta(testRoot / "software_transparent_sampling_cs.nwb", entry, scratch))
        return false;
    Impl::ShaderCook::CookVector<Path> includes(arena);
    includes.push_back(root / "impl/assets/graphics");
    includes.push_back(testRoot);
    const Path source = testRoot / "software_transparent_sampling_cs.slang";
    Impl::ShaderCook::CookVector<Path> dependencies(arena);
    if(!cook.gatherShaderDependencies(source, includes, dependencies, scratch))
        return false;
    const Impl::ShaderCook::ShaderCompilerRequest request{
        .shaderName = "tests/shadow_kernel/software_transparent_sampling_cs",
        .stage = entry.stage.view(), .targetProfile = entry.targetProfile.view(),
        .entryPoint = AStringView(entry.entryPoint.data(), entry.entryPoint.size()), .variantName = "default",
        .defines = nullptr, .includeDirectories = includes, .dependencies = dependencies,
        .sourcePath = source, .outputPath = outputRoot / "software_transparent_sampling_cs.spv",
        .defineCount = 0u, .optimizationLevel = entry.optimizationLevel,
    };
    Impl::ShaderCook::CookVector<u8> bytes(arena);
    if(!cook.compileVariant(request, bytes) || bytes.empty())
        return false;
    ShaderDesc shaderDesc(arena);
    shaderDesc.setShaderType(ShaderType::Compute).setEntryName(request.entryPoint);
    const ShaderHandle shader = device.createShader(shaderDesc, bytes.data(), bytes.size());
    if(!shader)
        return false;
    BindingLayoutDesc layoutDesc(arena);
    layoutDesc.setVisibility(ShaderType::Compute).addItem(BindingLayoutItem::PushConstants(0u, sizeof(Push)));
    const BindingLayoutHandle layout = device.createBindingLayout(layoutDesc);
    if(!layout)
        return false;
    ComputePipelineDesc desc;
    desc
        .setComputeShader(shader)
        .addBindingLayout(layout)
        .addBindingLayout(device.getDescriptorHeap().getResourceLayout())
        .addBindingLayout(device.getDescriptorHeap().getSamplerLayout())
    ;
    pipeline = device.createComputePipeline(desc);
    return static_cast<bool>(pipeline);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(ShadowKernelTest, SoftwareTransparentSamplingRejectsLocalDisocclusionMotionAndUnsettledHistory){
    using namespace __hidden_software_transparent_sampling_kernel_tests;
    const Common::LoggerRegistrationGuard diagnostics(*s_logger, Common::LoggerBreakPolicy::BreakOnFatal);
    Alloc::ScratchArena scratch(Name("tests/shadow_kernel/software_transparent_sampling"));
    ComputePipelineHandle pipeline;
    const bool loaded = LoadKernel(device(), arena(), scratch, pipeline);
    if(!loaded)
        s_logger->emitErrorsToStderr();
    ASSERT_TRUE(loaded);
    Vector<Input, Alloc::ScratchArena> inputs(scratch);
    Vector<u32, Alloc::ScratchArena> expected(scratch);
    const auto append = [&](const Input& input, const u32 samples){ inputs.push_back(input); expected.push_back(samples); };
    for(const u32 width : { 1u, 15u, 16u, 17u }){
        for(const u32 height : { 1u, 15u, 16u, 17u }){
            append(Stable(width, height, 0u, 0u), 1u);
            append(Stable(width, height, (width - 1u) / s_ExpectedDualCount, (height - 1u) / s_ExpectedDualCount), 1u);
        }
    }
    const Input stable = Stable(16u, 16u, s_ExpectedDualCount, s_ExpectedDualCount);
    for(u32 fault = 0u; fault < 20u; ++fault){
        Input test = stable;
        switch(fault){
        case 0u: test.historyValid = 0u; break;
        case 1u: test.current.w = 0.0f; break;
        case s_ExpectedDualCount: test.previous.w = 0.0f; break;
        case 3u: test.current.z = 12.0f; break;
        case 4u: test.previous.x = 1.0f; test.previous.y = 1.0f; break;
        case 5u: test.world.x += 0.125f; break;
        case 6u: test.world.y += 0.125f; break;
        case 7u: test.world.z = 1.25f; break;
        case 8u: test.world.w = -1.0f; break;
        case 9u: test.world.x = 5.0f; break;
        case 10u: test.moments.z = 7.0f; break;
        case 11u: test.moments.y = 0.375f; break;
        case 12u: test.moments.x = -0.25f; break;
        case 13u: test.moments.y = -0.25f; break;
        case 14u: test.moments.z = 33.0f; break;
        case 15u: test.current.z = -1.0f; break;
        case 16u: test.moments.y = 0.0f; break;
        case 17u: test.width = 0u; break;
        case 18u: test.current.z = Limit<f32>::s_Max; break;
        case 19u: test.moments.x = Limit<f32>::s_Max; break;
        default: FAIL();
        }
        append(test, 3u);
    }
    Input nearHistory = stable;
    nearHistory.world.x += 0.03125f;
    append(nearHistory, 1u);
    nearHistory.moments.z = 8.0f;
    append(nearHistory, 1u);
    // Cross a native workgroup boundary and keep the padded tail guarded.
    while(inputs.size() < 65u)
        append(stable, 1u);
    Vector<Output, Alloc::ScratchArena> sentinel(inputs.size() + 1u, scratch);
    NWB_MEMSET(sentinel.data(), 0xa5, sentinel.size() * sizeof(Output));
    BufferDesc inputDesc;
    inputDesc.setByteSize(inputs.size() * sizeof(Input)).setCanHaveRawViews(true).enableAutomaticStateTracking(ResourceStates::Common);
    BufferDesc outputDesc;
    outputDesc
        .setByteSize(sentinel.size() * sizeof(Output))
        .setCanHaveRawViews(true)
        .setCanHaveUAVs(true)
        .setCpuAccess(CpuAccessMode::Read)
        .enableAutomaticStateTracking(ResourceStates::Common)
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
    ASSERT_TRUE(commands->tryWriteBuffer(*output, sentinel.data(), sentinel.size() * sizeof(Output)));
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
    const QueueSubmissionToken token = device().executeCommandLists(lists, LengthOf(lists), commands->getDescription().physicalQueue, QueueSubmissionDesc{});
    ASSERT_TRUE(token.valid());
    ASSERT_TRUE(device().waitForIdle());
    const Output* actual = static_cast<const Output*>(device().mapBuffer(*output, CpuAccessMode::Read));
    ASSERT_NE(actual, nullptr);
    ScopeExit unmap([&]()noexcept{ device().unmapBuffer(*output); });
    for(usize index = 0u; index < inputs.size(); ++index){
        SCOPED_TRACE(index);
        EXPECT_EQ(actual[index].samples, expected[index]);
        if(expected[index] == 1u){
            EXPECT_EQ(actual[index].geometryAccepted, 1u);
            EXPECT_EQ(actual[index].previousX, inputs[index].pixelX);
            EXPECT_EQ(actual[index].previousY, inputs[index].pixelY);
        }
    }
    EXPECT_EQ(NWB_MEMCMP(&actual[inputs.size()], &sentinel[inputs.size()], sizeof(Output)), 0);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

