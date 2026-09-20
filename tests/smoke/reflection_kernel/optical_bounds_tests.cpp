// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "reflection_kernel_fixture.h"

#include <impl/assets/graphics/raytrace/optical_scene_constants.h>
#include <impl/assets_shader/cook.h>

#include <global/filesystem.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_optical_bounds_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct RayInput{
    Float4U origin{ 2.0f, 0.0f, 0.0f, 0.01f };
    Float4U direction{ 0.0f, 0.0f, 1.0f, 4.0f };
    Float4U boundsMin{ -1.0f, -1.0f, -1.0f, 0.0f };
    Float4U boundsMax{ 1.0f, 1.0f, 1.0f, 0.0f };
    u32 metadataSlot = 0u;
    u32 maxQueries = 16u;
    u32 padding[2]{};
};

struct Metadata{
    Float3U boundsMin;
    u32 transparentCount;
    Float3U boundsMax;
    u32 flags;
};

struct Case{
    AStringView label = "grid";
    RayInput ray;
    u32 transparentCount = 1u;
    u32 flags = NWB_RT_OPTICAL_SCENE_FLAG_BOUNDS_VALID;
    bool missingSlot = false;
    i32 expectedOutside = -1;
    i32 expectedExterior = -1;
};

struct Observation{
    u32 outside;
    u32 exterior;
};

struct PushConstants{
    u32 inputSlot;
    u32 outputSlot;
    u32 caseCount;
};

using Cases = Vector<Case, Alloc::ScratchArena>;

static_assert(sizeof(RayInput) == 80u);
static_assert(offsetof(RayInput, metadataSlot) == 64u);
static_assert(sizeof(Metadata) == NWB_RT_OPTICAL_SCENE_HEADER_BYTES);
static_assert(offsetof(Metadata, transparentCount) == NWB_RT_OPTICAL_SCENE_TRANSPARENT_COUNT_OFFSET);
static_assert(offsetof(Metadata, flags) == NWB_RT_OPTICAL_SCENE_FLAGS_OFFSET);
static_assert(sizeof(Observation) == 8u);
static_assert(sizeof(PushConstants) == 12u);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Case& AddCase(Cases& cases, const AStringView label, const i32 outside, const i32 exterior){
    cases.push_back(Case{ .label = label, .ray = {}, .expectedOutside = outside, .expectedExterior = exterior });
    return cases.back();
}

void BuildCases(Cases& cases){
    cases.reserve(128u);
    AddCase(cases, "parallel disjoint", 1, 1);
    AddCase(cases, "away from bound", 1, 1).ray.direction = { 1.0f, 0.0f, 0.0f, 4.0f };
    AddCase(cases, "incoming crossing", 0, 0).ray.direction = { -1.0f, 0.0f, 0.0f, 4.0f };
    AddCase(cases, "nonunit disjoint", 1, 1).ray.direction.z = 7.0f;
    AddCase(cases, "nonunit crossing", 0, 0).ray.direction = { -7.0f, 0.0f, 0.0f, 4.0f };
    AddCase(cases, "inside", 0, 0).ray.origin.x = 0.0f;
    AddCase(cases, "on face", 0, 0).ray.origin.x = 1.0f;
    auto& onFaceAway = AddCase(cases, "on face moving outward", 0, 0);
    onFaceAway.ray.origin.x = 1.0f;
    onFaceAway.ray.direction = { 1.0f, 0.0f, 0.0f, 4.0f };
    auto& tangent = AddCase(cases, "face tangent", 0, 0);
    tangent.ray.origin.y = 1.0f;
    tangent.ray.direction = { -1.0f, 0.0f, 0.0f, 4.0f };
    auto& edge = AddCase(cases, "edge tangent", 0, 0);
    edge.ray.origin.y = edge.ray.origin.z = 1.0f;
    edge.ray.direction = { -1.0f, 0.0f, 0.0f, 4.0f };
    AddCase(cases, "endpoint touches", 0, 0).ray.direction = { -1.0f, 0.0f, 0.0f, 1.0f };
    AddCase(cases, "range ends before entry", 1, 1).ray.direction = { -1.0f, 0.0f, 0.0f, 0.5f };
    auto& initialMedium = AddCase(cases, "TMin cannot hide entry", 0, 0);
    initialMedium.ray.origin.w = 3.5f;
    initialMedium.ray.direction = { -1.0f, 0.0f, 0.0f, 4.0f };
    AddCase(cases, "negative parallel zero", 1, 1).ray.direction = { -0.0f, 1.0f, 0.0f, 4.0f };
    AddCase(cases, "uncertain outside face", 0, 0).ray.origin.x = 1.0f + 1.1920928955078125e-7f;
    AddCase(cases, "clearly outside face", 1, 1).ray.origin.x = 1.01f;
    auto& envelope = AddCase(cases, "disjoint ray but inconclusive envelope", 0, 0);
    envelope.ray.origin = { -2.0f, 1.0f, 0.0f, 0.01f };
    envelope.ray.direction = { 1.0f, 1.0f, 0.0f, 4.0f };
    auto& large = AddCase(cases, "large coordinates uncertainty", 0, 0);
    large.ray.boundsMin.x = 100000000.0f;
    large.ray.boundsMax.x = 100000016.0f;
    large.ray.origin.x = 100000032.0f;
    const RayInput largeRay = large.ray;
    auto& largeClear = AddCase(cases, "large coordinates clear", 1, 1);
    largeClear.ray = largeRay;
    largeClear.ray.origin.x = 100004096.0f;
    AddCase(cases, "zero direction", 0, 0).ray.direction.z = 0.0f;
    AddCase(cases, "nonfinite direction", 0, 0).ray.direction.z = Limit<f32>::s_Infinity;
    AddCase(cases, "NaN origin", 0, 0).ray.origin.x = Limit<f32>::s_QuietNaN;
    AddCase(cases, "overflowed direction norm", 0, 0).ray.direction.z = Limit<f32>::s_Max;
    AddCase(cases, "zero range", 0, 0).ray.direction.w = 0.0f;
    AddCase(cases, "negative range", 0, 0).ray.direction.w = -1.0f;
    AddCase(cases, "nonfinite range", 0, 0).ray.direction.w = Limit<f32>::s_Infinity;
    AddCase(cases, "equal range endpoints", 1, 0).ray.origin.w = 4.0f;
    AddCase(cases, "negative TMin", 1, 0).ray.origin.w = -0.1f;
    AddCase(cases, "NaN TMin", 1, 0).ray.origin.w = Limit<f32>::s_QuietNaN;
    AddCase(cases, "zero query budget", 1, 0).ray.maxQueries = 0u;
    AddCase(cases, "large valid query budget", 1, 1).ray.maxQueries = Limit<u32>::s_Max;
    AddCase(cases, "missing scene selector", 1, 0).missingSlot = true;
    AddCase(cases, "incomplete scene bounds", 1, 0).flags = 0u;
    AddCase(cases, "inverted bounds", 0, 0).ray.boundsMin.x = 3.0f;
    AddCase(cases, "nonfinite bounds", 0, 0).ray.boundsMax.x = Limit<f32>::s_Infinity;
    AddCase(cases, "NaN bounds", 0, 0).ray.boundsMin.y = Limit<f32>::s_QuietNaN;
    AddCase(cases, "valid empty scene", 1, 1).transparentCount = 0u;
    auto& emptyNoBounds = AddCase(cases, "empty scene does not read bound vectors", 0, 1);
    emptyNoBounds.transparentCount = 0u;
    emptyNoBounds.ray.boundsMax.x = Limit<f32>::s_QuietNaN;
    auto& incompleteEmpty = AddCase(cases, "empty count cannot bypass incomplete metadata", 1, 0);
    incompleteEmpty.transparentCount = 0u;
    incompleteEmpty.flags = 0u;
    auto& emptyNoBudget = AddCase(cases, "empty count cannot bypass query budget", 1, 0);
    emptyNoBudget.transparentCount = 0u;
    emptyNoBudget.ray.maxQueries = 0u;
    auto& overflowingEndpoint = AddCase(cases, "endpoint overflow", 0, 0);
    overflowingEndpoint.ray.origin.x = Limit<f32>::s_Max;
    overflowingEndpoint.ray.direction = { 1.0f, 0.0f, 0.0f, Limit<f32>::s_Max };
    // This deterministic grid is checked by an independent FP64 slab oracle; inconclusive misses may stay false.
    for(const f32 x : { -3.0f, -1.0f, 1.0f, 3.0f }){
        for(const f32 y : { -2.0f, 0.0f, 2.0f }){
            for(const f32 dx : { -1.0f, 0.0f, 1.0f }){
                Case testCase;
                testCase.ray.origin = { x, y, -3.0f, 0.01f };
                testCase.ray.direction = { dx, 0.0f, 1.0f, 6.0f };
                cases.push_back(testCase);
            }
        }
    }
}

// Independent ray/box slab intersection in FP64, including t=0 and the finite endpoint; it does not mirror the GPU envelope proof.
[[nodiscard]] bool IntersectsBounds(const RayInput& ray){
    const f64 length = Sqrt(static_cast<f64>(ray.direction.x) * ray.direction.x
        + static_cast<f64>(ray.direction.y) * ray.direction.y + static_cast<f64>(ray.direction.z) * ray.direction.z);
    f64 nearDistance = 0.0;
    f64 farDistance = ray.direction.w;
    for(u32 axis = 0u; axis < 3u; ++axis){
        const f64 origin = ray.origin.raw[axis];
        const f64 direction = ray.direction.raw[axis] / length;
        if(direction == 0.0){
            if(origin < ray.boundsMin.raw[axis] || origin > ray.boundsMax.raw[axis])
                return false;
            continue;
        }
        f64 first = (static_cast<f64>(ray.boundsMin.raw[axis]) - origin) / direction;
        f64 last = (static_cast<f64>(ray.boundsMax.raw[axis]) - origin) / direction;
        if(first > last)
            Swap(first, last);
        nearDistance = Max(nearDistance, first);
        farDistance = Min(farDistance, last);
        if(nearDistance > farDistance)
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class OpticalBoundsKernelTest : public ReflectionKernelTest{
protected:
    [[nodiscard]] bool loadBoundsKernel(Alloc::ScratchArena& scratchArena, ComputePipelineHandle& outPipeline);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool OpticalBoundsKernelTest::loadBoundsKernel(Alloc::ScratchArena& scratchArena, ComputePipelineHandle& outPipeline){
    auto& graphicsDevice = device();
    auto& heap = graphicsDevice.getDescriptorHeap();
    auto& memoryArena = arena();
    const Path sourceRoot(memoryArena, NWB_REFLECTION_KERNEL_SOURCE_ROOT);
    const Path assetRoot = sourceRoot / "tests/smoke/reflection_kernel/assets";
    const Path outputRoot(memoryArena, NWB_REFLECTION_KERNEL_OUTPUT_ROOT);
    ErrorCode directoryError;
    if(!CreateDirectories(outputRoot, directoryError) && directoryError)
        return false;
    Impl::ShaderCook shaderCook(memoryArena);
    Impl::ShaderCook::ShaderEntry entry(memoryArena);
    Impl::ShaderCook::CookVector<u8> bytecode(memoryArena);
    {
        const Common::LoggerRegistrationGuard cookLogger(*s_logger, Common::LoggerBreakPolicy::BreakOnFatal);
        const bool cooked = [&]{
            if(!shaderCook.parseShaderMeta(assetRoot / "optical_bounds_cs.nwb", entry, scratchArena))
                return false;
            Impl::ShaderCook::CookVector<Path> includes(memoryArena);
            includes.push_back(sourceRoot / "impl/assets/graphics");
            const Path sourcePath = assetRoot / "optical_bounds_cs.slang";
            Impl::ShaderCook::CookVector<Path> dependencies(memoryArena);
            if(!shaderCook.gatherShaderDependencies(sourcePath, includes, dependencies, scratchArena))
                return false;
            const Impl::ShaderCook::ShaderCompilerRequest request{
                .shaderName = "tests/reflection_kernel/optical_bounds_cs",
                .stage = entry.stage.view(),
                .targetProfile = entry.targetProfile.view(),
                .entryPoint = AStringView(entry.entryPoint.data(), entry.entryPoint.size()),
                .variantName = "default",
                .defines = nullptr,
                .includeDirectories = includes,
                .dependencies = dependencies,
                .sourcePath = sourcePath,
                .outputPath = outputRoot / "optical_bounds_cs.spv",
                .defineCount = 0u,
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
    layoutDesc.setVisibility(ShaderType::Compute).addItem(BindingLayoutItem::PushConstants(0u, sizeof(__hidden_optical_bounds_tests::PushConstants)));
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


TEST_F(OpticalBoundsKernelTest, ProductionExteriorProofRejectsUncertaintyAndNeverAcceptsIntersectingSegments){
    using namespace __hidden_optical_bounds_tests;
    auto& graphicsDevice = device();
    auto& heap = graphicsDevice.getDescriptorHeap();
    Alloc::ScratchArena scratchArena(Name("tests/smoke/reflection_kernel/optical_bounds"));
    Cases cases(scratchArena);
    BuildCases(cases);
    ASSERT_GT(cases.size(), 64u);
    ASSERT_LT(cases.size(), 128u);
    ComputePipelineHandle pipeline;
    ASSERT_TRUE(loadBoundsKernel(scratchArena, pipeline));
    Vector<BufferHandle, Alloc::ScratchArena> headers(cases.size(), scratchArena);
    Vector<GpuDescriptorHandle, Alloc::ScratchArena> descriptors(cases.size() + 2u, scratchArena);
    Vector<RayInput, Alloc::ScratchArena> inputs(scratchArena);
    inputs.reserve(cases.size());
    ScopeExit releaseDescriptors([&]()noexcept{
        for(const GpuDescriptorHandle descriptor : descriptors){
            if(descriptor.valid())
                heap.free(descriptor);
        }
        heap.collectRetired();
    });
    const CommandListHandle commandList = graphicsDevice.createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
    for(usize index = 0u; index < cases.size(); ++index){
        Case& testCase = cases[index];
        BufferDesc desc;
        desc.setByteSize(sizeof(Metadata)).setCanHaveRawViews(true).setInitialState(ResourceStates::Common).setKeepInitialState(true);
        headers[index] = graphicsDevice.createBuffer(desc);
        ASSERT_TRUE(headers[index]);
        descriptors[index] = heap.allocate(GpuDescriptorClass::StorageBuffer);
        ASSERT_TRUE(descriptors[index].valid());
        ASSERT_TRUE(heap.write(descriptors[index], DescriptorWriteItem::RawBuffer_SRV(0u, headers[index].get())));
        const Metadata metadata{
            { testCase.ray.boundsMin.x, testCase.ray.boundsMin.y, testCase.ray.boundsMin.z }, testCase.transparentCount,
            { testCase.ray.boundsMax.x, testCase.ray.boundsMax.y, testCase.ray.boundsMax.z }, testCase.flags
        };
        ASSERT_TRUE(commandList->tryWriteBuffer(*headers[index], &metadata, sizeof(metadata)));
        commandList->setBufferState(headers[index].get(), ResourceStates::ShaderResource);
        testCase.ray.metadataSlot = testCase.missingSlot ? Limit<u32>::s_Max : descriptors[index].slot();
        inputs.push_back(testCase.ray);
    }
    BufferDesc inputDesc;
    inputDesc
        .setByteSize(inputs.size() * sizeof(RayInput))
        .setCanHaveRawViews(true)
        .setInitialState(ResourceStates::Common)
        .setKeepInitialState(true)
    ;
    const BufferHandle input = graphicsDevice.createBuffer(inputDesc);
    ASSERT_TRUE(input);
    BufferDesc outputDesc;
    outputDesc.setByteSize(128u * sizeof(Observation)).setCanHaveRawViews(true).setCanHaveUAVs(true).setCpuAccess(CpuAccessMode::Read);
    const BufferHandle output = graphicsDevice.createBuffer(outputDesc);
    ASSERT_TRUE(output);
    GpuDescriptorHandle& inputDescriptor = descriptors[cases.size()];
    GpuDescriptorHandle& outputDescriptor = descriptors[cases.size() + 1u];
    inputDescriptor = heap.allocate(GpuDescriptorClass::StorageBuffer);
    outputDescriptor = heap.allocate(GpuDescriptorClass::StorageBuffer);
    ASSERT_TRUE(inputDescriptor.valid());
    ASSERT_TRUE(outputDescriptor.valid());
    ASSERT_TRUE(heap.write(inputDescriptor, DescriptorWriteItem::RawBuffer_SRV(0u, input.get())));
    ASSERT_TRUE(heap.write(outputDescriptor, DescriptorWriteItem::RawBuffer_UAV(0u, output.get())));
    Observation sentinels[128];
    for(Observation& observation : sentinels)
        observation = { 0x5a5a5a5au, 0xa5a5a5a5u };
    ASSERT_TRUE(commandList->tryWriteBuffer(*input, inputs.data(), inputs.size() * sizeof(RayInput)));
    ASSERT_TRUE(commandList->tryWriteBuffer(*output, sentinels, sizeof(sentinels)));
    commandList->setBufferState(input.get(), ResourceStates::ShaderResource);
    commandList->setBufferState(output.get(), ResourceStates::UnorderedAccess);
    commandList->commitBarriers();
    ComputeState state;
    state.setPipeline(pipeline.get());
    commandList->setComputeState(state);
    heap.bindCompute(*commandList, *pipeline);
    const PushConstants push{ inputDescriptor.slot(), outputDescriptor.slot(), static_cast<u32>(cases.size()) };
    commandList->setPushConstants(&push, sizeof(push));
    commandList->dispatch(2u, 1u, 1u);
    commandList->close();
    ASSERT_FALSE(commandList->commandRecordingFailed());
    CommandList* const submitted[] = { commandList.get() };
    const QueueSubmissionToken token = graphicsDevice.executeCommandLists(
        submitted, LengthOf(submitted), commandList->getDescription().physicalQueue, QueueSubmissionDesc{}
    );
    ASSERT_TRUE(token.valid());
    ASSERT_TRUE(graphicsDevice.waitForIdle());
    const Observation* const mapped = static_cast<const Observation*>(graphicsDevice.mapBuffer(*output, CpuAccessMode::Read));
    ASSERT_NE(mapped, nullptr);
    ScopeExit unmap([&]()noexcept{ graphicsDevice.unmapBuffer(*output); });
    usize provenOutside = 0u;
    for(usize index = 0u; index < cases.size(); ++index){
        const Case& testCase = cases[index];
        SCOPED_TRACE(index);
        SCOPED_TRACE(testCase.label.data());
        EXPECT_LE(mapped[index].outside, 1u);
        EXPECT_LE(mapped[index].exterior, 1u);
        if(testCase.expectedOutside >= 0)
            EXPECT_EQ(mapped[index].outside, static_cast<u32>(testCase.expectedOutside));
        if(testCase.expectedExterior >= 0)
            EXPECT_EQ(mapped[index].exterior, static_cast<u32>(testCase.expectedExterior));
        if(mapped[index].outside != 0u){
            ++provenOutside;
            EXPECT_FALSE(IntersectsBounds(testCase.ray));
        }
        if(mapped[index].exterior != 0u && testCase.transparentCount != 0u)
            EXPECT_FALSE(IntersectsBounds(testCase.ray));
    }
    EXPECT_GT(provenOutside, 10u);
    for(usize index = cases.size(); index < LengthOf(sentinels); ++index){
        EXPECT_EQ(mapped[index].outside, sentinels[index].outside);
        EXPECT_EQ(mapped[index].exterior, sentinels[index].exterior);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

