// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "reflection_kernel_fixture.h"

#include <impl/assets_csg/cook.h>
#include <impl/assets_shader/cook.h>
#include <impl/assets/graphics/bvh/constants.h>
#include <impl/assets/graphics/mesh/runtime_constants.h>
#include <impl/assets/graphics/raytrace/optical_scene_constants.h>
#include <impl/assets/graphics/shadow/constants.h>

#include <global/bit.h>
#include <global/filesystem.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{
namespace __hidden_refraction_hit_kernel_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct Attribute{
    u32 normal[2] = { 0x34003800u, 0x00003c00u };
    Float2U uv{};
};

struct Material{
    u32 modelId = 0u;
    u32 flags = NWB_RT_INSTANCE_MATERIAL_FLAG_TRANSPARENT;
    u32 shadingModelId = 7u;
    u32 materialOffset = 0u;
    u32 meshInstanceIndex = 0u;
    u32 indexSlot = 0u;
    u32 attributeSlot = 0u;
    u32 positionSlot = 0u;
    u32 nodeSlot = 0xffffffffu;
};

struct MeshInstance{
    Float4U rotation{ 0.0f, 0.0f, 0.0f, 1.0f };
    Float4U translation{};
    Float4U scale{ 1.0f, 1.0f, 1.0f, 0.0f };
    u32 geometryHeapSlots[NWB_MESH_INSTANCE_GEOMETRY_SLOT_COUNT]{};
};

struct RayInput{
    Float4U originTMin{ 0.125f, 0.25f, 0.0f, 0.001f };
    Float4U directionTMax{ 0.0f, 0.0f, 1.0f, 100.0f };
    u32 slots[4]{};
    u32 operation[4] = { 0u, 0u, 0x3fc00000u, 0u };
};

struct PushConstants{
    u32 inputSlot;
    u32 outputSlot;
    u32 outputOffset;
    u32 reserved = 0u;
};

struct Observation{
    u32 words[36];
};

struct Case{
    AStringView name;
    RayInput ray;
    u32 model = 0u;
    u32 planeCount = 1u;
    bool flipWinding = false;
    bool reversedInstanceOrder = false;
    bool transformed = false;
    bool degenerate = false;
    bool invalidNormal = false;
    bool transparent = true;
    u32 expectedHit = 1u;
    u32 expectedInstance = 0u;
    i32 expectedFront = 1;
    f32 expectedDistance = 1.0f;
    u32 expectedOriginalReconstruction = 1u;
    u32 expectedCandidateReconstruction = 1u;
};

using Cases = Vector<Case, Alloc::ScratchArena>;
static_assert(sizeof(Attribute) == NWB_RAYTRACE_VERTEX_ATTRIBUTE_STRIDE_BYTES);
static_assert(sizeof(Material) == 36u);
static_assert(sizeof(MeshInstance) == 96u);
static_assert(sizeof(RayInput) == 64u);
static_assert(sizeof(PushConstants) == 16u);
static_assert(sizeof(Observation) == 144u);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Case& AddCase(Cases& cases, const AStringView name){
    cases.push_back(Case{ .name = name, .ray = {} });
    return cases.back();
}

static void BuildCases(Cases& cases){
    cases.reserve(40u);
    for(u32 operation = 0u; operation <= 2u; ++operation){
        for(u32 reversed = 0u; reversed <= 1u; ++reversed){
            for(u32 model = 0u; model <= 2u; ++model){
                Case& testCase = AddCase(cases, "front/back interface with opaque, quarter or zero alpha");
                testCase.ray.operation[0] = operation;
                testCase.flipWinding = reversed != 0u;
                testCase.model = model;
                const bool admitted = operation == 0u || (operation == 1u ? reversed == 0u : reversed != 0u);
                testCase.expectedHit = admitted ? 1u : 0u;
                testCase.expectedFront = reversed == 0u ? 1 : 0;
                testCase.expectedCandidateReconstruction = admitted ? 1u : 0u;
            }
        }
    }
    auto& wrongInstance = AddCase(cases, "closest wrong instance must not expose a farther primary exit");
    wrongInstance.ray.operation[0] = 2u;
    wrongInstance.ray.operation[1] = 1u;
    wrongInstance.planeCount = 2u;
    wrongInstance.flipWinding = true;
    wrongInstance.expectedHit = 0u;
    wrongInstance.expectedCandidateReconstruction = 0u;
    const Case wrongInstanceForward = wrongInstance;
    cases.push_back(wrongInstanceForward);
    cases.back().name = "reversed TLAS order retains closest wrong-instance rejection";
    cases.back().reversedInstanceOrder = true;
    cases.push_back(wrongInstanceForward);
    cases.back().name = "zero-alpha wrong instance still blocks the farther primary exit";
    cases.back().model = 2u;
    auto& zeroAlphaClosest = AddCase(cases, "zero-alpha closest surface is not filtered before the next instance");
    zeroAlphaClosest.model = 2u;
    zeroAlphaClosest.planeCount = 2u;
    auto& behindRange = AddCase(cases, "finite range ends before first triangle");
    behindRange.ray.directionTMax.w = 0.5f;
    behindRange.expectedHit = 0u;
    behindRange.expectedOriginalReconstruction = behindRange.expectedCandidateReconstruction = 0u;
    auto& clippedNear = AddCase(cases, "near range excludes the first instance");
    clippedNear.planeCount = 2u;
    clippedNear.ray.originTMin.w = 1.125f;
    clippedNear.ray.operation[0] = 1u;
    clippedNear.expectedInstance = 1u;
    clippedNear.expectedDistance = 2.0f;
    auto& parallel = AddCase(cases, "parallel ray misses all geometry");
    parallel.ray.directionTMax.x = 1.0f;
    parallel.ray.directionTMax.z = 0.0f;
    parallel.expectedHit = 0u;
    parallel.expectedOriginalReconstruction = parallel.expectedCandidateReconstruction = 0u;
    auto& nonunit = AddCase(cases, "stored nonunit ray is not normalized");
    nonunit.ray.directionTMax.z = 2.0f;
    nonunit.expectedDistance = 0.5f;
    auto& reverse = AddCase(cases, "back-face exit from the opposite ray direction");
    reverse.ray.originTMin.z = 3.0f;
    reverse.ray.directionTMax.z = -1.0f;
    reverse.ray.operation[0] = 2u;
    reverse.expectedFront = 0;
    reverse.expectedDistance = 2.0f;
    auto& degenerate = AddCase(cases, "zero-area triangles remain a geometric miss");
    degenerate.degenerate = true;
    degenerate.expectedHit = 0u;
    degenerate.expectedOriginalReconstruction = degenerate.expectedCandidateReconstruction = 0u;
    auto& invalidNormal = AddCase(cases, "invalid attribute normal preserves reconstruction fallback");
    invalidNormal.invalidNormal = true;
    auto& transformed = AddCase(cases, "mirrored nonuniform instance preserves committed transform inputs");
    transformed.transformed = true;
    transformed.expectedFront = -1;
    auto& opaque = AddCase(cases, "opaque instance retains the common closest-surface contract");
    opaque.transparent = false;
    for(u32 model = 3u; model <= 5u; ++model){
        Case& testCase = AddCase(cases, "entry IOR validation stays after material evaluation");
        testCase.ray.operation[0] = 1u;
        testCase.model = model;
        // Existing comparisons reject unit/Inf IOR; their NaN behavior is deliberately preserved by this optimization.
        testCase.expectedHit = model == 4u ? 1u : 0u;
    }
    auto& mismatch = AddCase(cases, "capture IOR mismatch retains screen fallback");
    mismatch.ray.operation[0] = 1u;
    mismatch.ray.operation[2] = BitCast<u32>(1.75f);
    mismatch.expectedHit = 0u;
}

static void RunCase(
    GraphicsBackend::Device& device,
    Alloc::GlobalArena& memoryArena,
    ComputePipeline& original,
    ComputePipeline& candidate,
    const Case& testCase,
    Alloc::ScratchArena& scratchArena){
    SCOPED_TRACE(testCase.name.data());
    SCOPED_TRACE(testCase.ray.operation[0]);
    SCOPED_TRACE(testCase.flipWinding);
    SCOPED_TRACE(testCase.model);
    auto& heap = device.getDescriptorHeap();
    GpuDescriptorHandle descriptors[9]{};
    ScopeExit releaseDescriptors([&]()noexcept{
        for(const GpuDescriptorHandle descriptor : descriptors){
            if(descriptor.valid())
                heap.free(descriptor);
        }
        heap.collectRetired();
    });
    const f32 extent = testCase.degenerate ? 0.0f : 2.0f;
    const Float3U positions[] = { { -extent, -extent, 0.0f }, { extent, -extent, 0.0f }, { extent, extent, 0.0f }, { -extent, extent, 0.0f } };
    u32 indices[] = { 0u, 2u, 1u, 0u, 3u, 2u };
    if(testCase.flipWinding){
        Swap(indices[1], indices[2]);
        Swap(indices[4], indices[5]);
    }
    Attribute attributes[LengthOf(indices)];
    for(u32 index = 0u; index < LengthOf(indices); ++index){
        attributes[index].uv = { positions[indices[index]].x * 0.25f + 0.5f, positions[indices[index]].y * 0.25f + 0.5f };
        if(testCase.invalidNormal)
            attributes[index].normal[0] = 0x34007e00u;
    }
    Vector<MeshInstance, Alloc::ScratchArena> instances(testCase.planeCount, scratchArena);
    Vector<Material, Alloc::ScratchArena> materials(testCase.planeCount, scratchArena);
    Vector<RayTracingInstanceDesc, Alloc::ScratchArena> hardwareInstances(testCase.planeCount, scratchArena);
    const u32 typedWords = 0u;
    RayInput ray = testCase.ray;
    const void* const data[] = { positions, indices, attributes, instances.data(), materials.data(), &typedWords, &ray };
    const usize sizes[] = {
        sizeof(positions), sizeof(indices), sizeof(attributes), instances.size() * sizeof(MeshInstance),
        materials.size() * sizeof(Material), sizeof(typedWords), sizeof(ray)
    };
    BufferHandle buffers[7];
    for(u32 index = 0u; index < LengthOf(buffers); ++index){
        BufferDesc desc;
        desc.setByteSize(sizes[index]).setInitialState(ResourceStates::Common).setKeepInitialState(true);
        if(index == 6u)
            desc.setIsConstantBuffer(true);
        else
            desc.setCanHaveRawViews(true);
        if(index < 2u)
            desc.setIsAccelStructBuildInput(true);
        buffers[index] = device.createBuffer(desc);
        ASSERT_TRUE(buffers[index]);
        descriptors[index] = heap.allocate(index == 6u ? GpuDescriptorClass::UniformBuffer : GpuDescriptorClass::StorageBuffer);
        ASSERT_TRUE(descriptors[index].valid());
        const DescriptorWriteItem view = index == 6u
            ? DescriptorWriteItem::ConstantBuffer(0u, buffers[index].get())
            : DescriptorWriteItem::RawBuffer_SRV(0u, buffers[index].get())
        ;
        ASSERT_TRUE(heap.write(descriptors[index], view));
    }
    RayTracingGeometryTriangles triangles;
    triangles
        .setVertexBuffer(buffers[0].get())
        .setVertexFormat(Format::RGB32_FLOAT)
        .setVertexStride(sizeof(Float3U))
        .setVertexCount(static_cast<u32>(LengthOf(positions)))
        .setIndexBuffer(buffers[1].get())
        .setIndexFormat(Format::R32_UINT)
        .setIndexCount(static_cast<u32>(LengthOf(indices)))
    ;
    RayTracingGeometryDesc geometry;
    geometry.setTriangles(triangles);
    RayTracingAccelStructDesc blasDesc(memoryArena);
    blasDesc.addBottomLevelGeometry(geometry);
    const RayTracingAccelStructHandle blas = device.createAccelStruct(blasDesc);
    ASSERT_TRUE(blas);
    for(u32 index = 0u; index < testCase.planeCount; ++index){
        const f32 z = static_cast<f32>(index + 1u);
        instances[index].translation.z = z;
        if(testCase.transformed)
            instances[index].scale = { -2.0f, 1.5f, 0.5f, 0.0f };
        Material& material = materials[index];
        material.modelId = testCase.model;
        material.flags = testCase.transparent ? NWB_RT_INSTANCE_MATERIAL_FLAG_TRANSPARENT : 0u;
        material.meshInstanceIndex = index;
        material.positionSlot = descriptors[0].slot();
        material.indexSlot = descriptors[1].slot();
        material.attributeSlot = descriptors[2].slot();
        AffineTransform transform = s_identityTransform;
        transform._11 = instances[index].scale.x;
        transform._22 = instances[index].scale.y;
        transform._33 = instances[index].scale.z;
        transform._34 = z;
        hardwareInstances[index].setBLAS(blas.get()).setInstanceID(index).setTransform(transform).setInstanceMask(0xffu);
    }
    if(testCase.reversedInstanceOrder && hardwareInstances.size() == 2u)
        Swap(hardwareInstances[0], hardwareInstances[1]);
    ray.slots[0] = descriptors[4].slot();
    ray.slots[1] = descriptors[3].slot();
    ray.slots[2] = descriptors[5].slot();
    RayTracingAccelStructDesc tlasDesc(memoryArena);
    tlasDesc.setTopLevelMaxInstances(testCase.planeCount);
    const RayTracingAccelStructHandle tlas = device.createAccelStruct(tlasDesc);
    ASSERT_TRUE(tlas);
    descriptors[7] = heap.allocate(GpuDescriptorClass::AccelStruct);
    ASSERT_TRUE(descriptors[7].valid());
    ASSERT_TRUE(heap.write(descriptors[7], DescriptorWriteItem::RayTracingAccelStruct(0u, tlas.get())));
    Observation initial[4];
    NWB_MEMSET(initial, 0xa5, sizeof(initial));
    initial[1].words[32] = initial[2].words[32] = 0u;
    BufferDesc outputDesc;
    outputDesc
        .setByteSize(sizeof(initial))
        .setCanHaveRawViews(true)
        .setCanHaveUAVs(true)
        .setCpuAccess(CpuAccessMode::Read)
        .setInitialState(ResourceStates::Common)
        .setKeepInitialState(true)
    ;
    const BufferHandle output = device.createBuffer(outputDesc);
    ASSERT_TRUE(output);
    descriptors[8] = heap.allocate(GpuDescriptorClass::StorageBuffer);
    ASSERT_TRUE(descriptors[8].valid());
    ASSERT_TRUE(heap.write(descriptors[8], DescriptorWriteItem::RawBuffer_UAV(0u, output.get())));
    const CommandListHandle commands = device.createCommandList();
    ASSERT_TRUE(commands);
    commands->open();
    for(u32 index = 0u; index < LengthOf(buffers); ++index)
        ASSERT_TRUE(commands->tryWriteBuffer(*buffers[index], data[index], sizes[index]));
    commands->buildBottomLevelAccelStruct(blas.get(), &geometry, 1u, RayTracingAccelStructBuildFlags::None);
    commands->buildTopLevelAccelStruct(tlas.get(), hardwareInstances.data(), hardwareInstances.size(), RayTracingAccelStructBuildFlags::None);
    commands->setAccelStructState(tlas.get(), ResourceStates::AccelStructRead);
    for(u32 index = 0u; index < LengthOf(buffers); ++index)
        commands->setBufferState(buffers[index].get(), index == 6u ? ResourceStates::ConstantBuffer : ResourceStates::ShaderResource);
    ASSERT_TRUE(commands->tryWriteBuffer(*output, initial, sizeof(initial)));
    commands->setBufferState(output.get(), ResourceStates::UnorderedAccess, true);
    commands->commitBarriers();
    ComputePipeline* const pipelines[] = { &original, &candidate };
    for(u32 index = 0u; index < LengthOf(pipelines); ++index){
        const PushConstants push{ descriptors[6].slot(), descriptors[8].slot(), (index + 1u) * static_cast<u32>(sizeof(Observation)) };
        if(index != 0u){
            commands->setBufferState(output.get(), ResourceStates::UnorderedAccess, true);
            commands->commitBarriers();
        }
        commands->setComputeState(ComputeState().setPipeline(pipelines[index]));
        heap.bindCompute(*commands, *pipelines[index], descriptors[7]);
        commands->setPushConstants(&push, sizeof(push));
        commands->dispatch(1u, 1u, 1u);
    }
    ASSERT_FALSE(commands->commandRecordingFailed());
    commands->close();
    ASSERT_FALSE(commands->commandRecordingFailed());
    CommandList* const lists[] = { commands.get() };
    const QueueSubmissionToken token = device.executeCommandLists(lists, LengthOf(lists), commands->getDescription().physicalQueue, QueueSubmissionDesc{});
    ASSERT_TRUE(token.valid());
    ASSERT_TRUE(device.waitForIdle());
    const Observation* const observed = static_cast<const Observation*>(device.mapBuffer(*output, CpuAccessMode::Read));
    ASSERT_NE(observed, nullptr);
    ScopeExit unmap([&]()noexcept{ device.unmapBuffer(*output); });
    EXPECT_EQ(NWB_MEMCMP(&observed[0], &initial[0], sizeof(Observation)), 0);
    EXPECT_EQ(NWB_MEMCMP(&observed[3], &initial[3], sizeof(Observation)), 0);
    for(u32 word = 0u; word < 32u; ++word)
        EXPECT_EQ(observed[1].words[word], observed[2].words[word]) << "retained geometry/surface word " << word;
    EXPECT_EQ(observed[1].words[32], testCase.expectedOriginalReconstruction);
    EXPECT_EQ(observed[2].words[32], testCase.expectedCandidateReconstruction);
    for(u32 variant = 1u; variant <= 2u; ++variant){
        const Observation& result = observed[variant];
        EXPECT_EQ(result.words[0], testCase.expectedHit);
        EXPECT_EQ(result.words[33], 1u);
        EXPECT_EQ(result.words[34], 0u);
        EXPECT_EQ(result.words[35], 0u);
        if(testCase.expectedHit != 0u){
            if(testCase.expectedFront >= 0)
                EXPECT_EQ(result.words[1], static_cast<u32>(testCase.expectedFront));
            EXPECT_EQ(result.words[2], testCase.expectedInstance);
            EXPECT_FLOAT_EQ(BitCast<f32>(result.words[6]), testCase.expectedDistance);
            EXPECT_EQ(result.words[4], testCase.transparent ? NWB_RT_INSTANCE_MATERIAL_FLAG_TRANSPARENT : 0u);
            EXPECT_EQ(result.words[5], 7u);
            // Half-exact material values show alpha=0 still commits the same geometric surface.
            const f32 expectedCoverage = testCase.model == 1u ? 0.25f : (testCase.model == 2u ? 0.0f : 1.0f);
            EXPECT_FLOAT_EQ(BitCast<f32>(result.words[23]), expectedCoverage);
        }
        else{
            for(u32 word = 0u; word < 32u; ++word)
                EXPECT_EQ(result.words[word], 0u);
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RefractionHitKernelTest : public ReflectionKernelTest{
protected:
    virtual void SetUp()override;
    [[nodiscard]] bool loadHitKernel(bool geometryFirst, Alloc::ScratchArena& scratchArena, ComputePipelineHandle& outPipeline);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RefractionHitKernelTest::SetUp(){
    if(
        !device().queryFeatureSupport(Feature::RayTracingAccelStruct)
        || !device().queryFeatureSupport(Feature::RayQuery) || !device().getDescriptorHeap().hasAccelStructLayout()
    )
        GTEST_SKIP() << "Native refraction interface tests require acceleration structures and hardware ray queries.";
}

bool RefractionHitKernelTest::loadHitKernel(
    const bool geometryFirst,
    Alloc::ScratchArena& scratchArena,
    ComputePipelineHandle& outPipeline){
    constexpr AStringView name = "refraction_hit_cs";
    const AStringView variant = geometryFirst ? "NWB_TEST_REFRACTION_GEOMETRY_FIRST=1" : "NWB_TEST_REFRACTION_GEOMETRY_FIRST=0";
    const Impl::ShaderCook::ShaderMacroDefinition definition{ "NWB_TEST_REFRACTION_GEOMETRY_FIRST", geometryFirst ? "1" : "0" };
    auto& graphicsDevice = device();
    auto& heap = graphicsDevice.getDescriptorHeap();
    auto& memoryArena = arena();
    const Path sourceRoot(memoryArena, NWB_REFLECTION_KERNEL_SOURCE_ROOT);
    const Path kernelRoot = sourceRoot / "tests/smoke/reflection_kernel/assets/refraction_hit";
    Path sourcePath = kernelRoot / name;
    sourcePath.replace_extension(".slang");
    Path metadataPath = kernelRoot / name;
    metadataPath.replace_extension(".nwb");
    const Path outputRoot(memoryArena, NWB_REFLECTION_KERNEL_OUTPUT_ROOT);
    ErrorCode directoryError;
    if(!CreateDirectories(outputRoot, directoryError) && directoryError)
        return false;
    Path outputPath = outputRoot / (geometryFirst ? "refraction_hit_candidate.spv" : "refraction_hit_reference.spv");
    outputPath.replace_extension(".spv");
    Impl::ShaderCook shaderCook(memoryArena);
    Impl::ShaderCook::ShaderEntry entry(memoryArena);
    Impl::ShaderCook::CookVector<u8> bytecode(memoryArena);
    {
        const Common::LoggerRegistrationGuard cookLogger(*s_logger, Common::LoggerBreakPolicy::BreakOnFatal);
        const bool cooked = [&]{
            if(!shaderCook.parseShaderMeta(metadataPath, entry, scratchArena))
                return false;
            if(!shaderCook.validateVariantSignature(name, variant, entry.defineValues, scratchArena))
                return false;
            Impl::ShaderCook::CookVector<Path> includes(memoryArena);
            includes.push_back(kernelRoot);
            includes.push_back(sourceRoot / "impl/assets/graphics/raytrace");
            includes.push_back(sourceRoot / "impl/assets/graphics");
            const Impl::AssetsCsgCook::CsgShapeCookEntryVector noShapes(memoryArena);
            Path generatedCsgRoot(memoryArena);
            if(!Impl::AssetsCsgCook::EmitCsgShapeModuleIncludes(
                outputRoot, "refraction_hit", noShapes, generatedCsgRoot, scratchArena
            ))
                return false;
            includes.push_back(generatedCsgRoot);
            Impl::ShaderCook::CookVector<Path> dependencies(memoryArena);
            if(!shaderCook.gatherShaderDependencies(sourcePath, includes, dependencies, scratchArena))
                return false;
            const Impl::ShaderCook::ShaderCompilerRequest request{
                .shaderName = name,
                .stage = entry.stage.view(),
                .targetProfile = entry.targetProfile.view(),
                .entryPoint = AStringView(entry.entryPoint.data(), entry.entryPoint.size()),
                .variantName = variant,
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
    layoutDesc.setVisibility(ShaderType::Compute).addItem(BindingLayoutItem::PushConstants(0u, 16u));
    const BindingLayoutHandle layout = graphicsDevice.createBindingLayout(layoutDesc);
    if(!layout)
        return false;
    ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(shader)
        .addBindingLayout(layout)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
        .addBindingLayout(heap.getAccelStructLayout())
    ;
    outPipeline = graphicsDevice.createComputePipeline(pipelineDesc);
    return outPipeline.get() != nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(RefractionHitKernelTest, ClosestInterfaceAdmissionPreservesHitsAndSkipsRejectedMaterialReconstruction){
    using namespace __hidden_refraction_hit_kernel_tests;
    Alloc::ScratchArena scratchArena(Name("tests/smoke/reflection_kernel/refraction_hit"));
    const u64 initialErrors = s_logger->errorCount();
    auto run = [&](){
        ComputePipelineHandle original;
        ComputePipelineHandle candidate;
        ASSERT_TRUE(loadHitKernel(false, scratchArena, original));
        ASSERT_TRUE(loadHitKernel(true, scratchArena, candidate));
        Cases cases(scratchArena);
        BuildCases(cases);
        for(const Case& testCase : cases)
            ASSERT_NO_FATAL_FAILURE(RunCase(device(), arena(), *original, *candidate, testCase, scratchArena));
    };
    run();
    const TStringView validationPrefix = NWB_TEXT("Vulkan debug: [severity=error");
    const bool validationFailed = s_logger->sawMessageContaining(validationPrefix);
    const bool backendFailed = s_logger->errorCount() != initialErrors;
    if(HasFailure() || validationFailed || backendFailed){
        s_logger->emitErrorsToStderr();
        s_logger->emitMessagesContainingToStderr(validationPrefix);
    }
    EXPECT_FALSE(backendFailed);
    EXPECT_FALSE(validationFailed);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

