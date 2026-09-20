// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shadow_kernel_fixture.h"

#include <impl/assets/graphics/bvh/constants.h>
#include <impl/assets/graphics/shadow/constants.h>
#include <impl/assets_csg/cook.h>
#include <impl/assets_shader/cook.h>

#include <global/filesystem.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_streaming_traversal_kernel_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct Node{
    Float3U minimum;
    u32 left;
    Float3U maximum;
    u32 right;
};

struct Instance{
    Float4U inverseRows[3];
    u32 reservedMeshIndex = 0u;
    u32 primitiveCount = 0u;
    u32 reserved[2]{};
};

struct Material{
    u32 modelId = 0u;
    u32 flags = NWB_RT_INSTANCE_MATERIAL_FLAG_TRANSPARENT;
    u32 shadingModelId = 0u;
    u32 materialOffset = 0u;
    u32 meshInstanceIndex = 0u;
    u32 indexSlot = 0u;
    u32 attributeSlot = 0u;
    u32 positionSlot = 0u;
    u32 nodeSlot = 0u;
};

struct Attribute{
    u32 normal[2] = { 0u, 0x00003c00u };
    Float2U uv{};
};

struct ContextSlots{
    u32 scene[4]{};
    u32 material[4]{};
};

struct Push{
    u32 materialContextSlotsHeapSlot;
    u32 outputSlot;
    u32 instanceCount;
    u32 reserved;
    Float4U origin;
    Float4U direction;
};

struct Case{
    AStringView label;
    u32 count;
    f32 spacing = 0.0f;
    f32 scale = 1.0f;
    f32 originZ = 0.0f;
    f32 rayX = 0.125f;
    f32 rayY = 0.25f;
    f32 tMax = 100.0f;
    u32 lobes = 1u;
    bool alternateTint = false;
    bool mirrored = false;
    bool reverse = false;
    bool opaqueLast = false;
    bool capDifference = false;
};

namespace BufferIndex{
    enum Enum : u32{ Scene, Mesh, Positions, Indices, Attributes, Instances, Materials, Context, Candidate, Reference, Count };
};

static_assert(sizeof(Node) == 32u);
static_assert(sizeof(Instance) == 64u);
static_assert(sizeof(Material) == 36u);
static_assert(sizeof(Attribute) == 16u);
static_assert(sizeof(ContextSlots) == 32u);
static_assert(sizeof(Push) == 48u);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Float3U Minimum(const Float3U& a, const Float3U& b){
    return { Min(a.x, b.x), Min(a.y, b.y), Min(a.z, b.z) };
}

[[nodiscard]] Float3U Maximum(const Float3U& a, const Float3U& b){
    return { Max(a.x, b.x), Max(a.y, b.y), Max(a.z, b.z) };
}

[[nodiscard]] u32 AppendTree(
    Vector<Node, Alloc::ScratchArena>& nodes,
    const Vector<Node, Alloc::ScratchArena>& leaves,
    const u32 first,
    const u32 count){
    const u32 index = static_cast<u32>(nodes.size());
    nodes.push_back(Node{});
    if(count == 1u){
        nodes[index] = leaves[first];
        return index;
    }
    const u32 leftCount = count / 2u;
    const u32 left = AppendTree(nodes, leaves, first, leftCount);
    const u32 right = AppendTree(nodes, leaves, first + leftCount, count - leftCount);
    nodes[index] = {
        Minimum(nodes[left].minimum, nodes[right].minimum), left,
        Maximum(nodes[left].maximum, nodes[right].maximum),
        right | ((nodes[left].right | nodes[right].right) & NWB_BVH_TRANSPARENT_SUBTREE_FLAG),
    };
    return index;
}

void AppendBox(
    const u32 lobe,
    Vector<Float3U, Alloc::ScratchArena>& positions,
    Vector<u32, Alloc::ScratchArena>& indices,
    Vector<Node, Alloc::ScratchArena>& leaves){
    const f32 z = 1.0f + 2.0f * static_cast<f32>(lobe);
    const Float3U corners[] = {
        { -1.0f, -1.0f, z }, { 1.0f, -1.0f, z }, { 1.0f, 1.0f, z }, { -1.0f, 1.0f, z },
        { -1.0f, -1.0f, z + 1.0f }, { 1.0f, -1.0f, z + 1.0f },
        { 1.0f, 1.0f, z + 1.0f }, { -1.0f, 1.0f, z + 1.0f },
    };
    const u32 triangles[] = {
        0u, 2u, 1u, 0u, 3u, 2u, 4u, 5u, 6u, 4u, 6u, 7u,
        0u, 1u, 5u, 0u, 5u, 4u, 3u, 7u, 6u, 3u, 6u, 2u,
        0u, 4u, 7u, 0u, 7u, 3u, 1u, 2u, 6u, 1u, 6u, 5u,
    };
    const u32 base = static_cast<u32>(positions.size());
    for(const Float3U& corner : corners)
        positions.push_back(corner);
    for(usize triangle = 0u; triangle < LengthOf(triangles); triangle += 3u){
        const Float3U& a = corners[triangles[triangle]];
        const Float3U& b = corners[triangles[triangle + 1u]];
        const Float3U& c = corners[triangles[triangle + 2u]];
        const u32 primitive = static_cast<u32>(indices.size() / 3u);
        leaves.push_back(Node{ Minimum(Minimum(a, b), c), NWB_BVH_LEAF_FLAG | primitive, Maximum(Maximum(a, b), c), 1u });
        for(usize corner = 0u; corner < 3u; ++corner)
            indices.push_back(base + triangles[triangle + corner]);
    }
}

// Independent axis-aligned box oracle. The legacy singleton and >4-crossing policies are explicit test cases;
// they are not claimed to represent closed-medium transport from an inside origin or arbitrary nonconvex volume.
[[nodiscard]] Float3U Expected(const Case& testCase){
    f64 transmission[3] = { 1.0, 1.0, 1.0 };
    if(Abs(testCase.rayX) > 1.0f || Abs(testCase.rayY) > 1.0f)
        return { 1.0f, 1.0f, 1.0f };
    for(u32 instance = 0u; instance < testCase.count; ++instance){
        if(testCase.opaqueLast && instance + 1u == testCase.count)
            continue;
        f64 boundaries[6]{};
        u32 crossingCount = 0u;
        for(u32 lobe = 0u; lobe < testCase.lobes; ++lobe){
            for(u32 side = 0u; side < 2u; ++side){
                const f64 distance = static_cast<f64>(1u + lobe * 2u + side) * testCase.scale
                    + static_cast<f64>(instance) * testCase.spacing - testCase.originZ;
                if(distance > 0.0001 && distance < testCase.tMax)
                    boundaries[crossingCount++] = distance;
            }
        }
        f64 chord = 0.0;
        for(u32 index = 0u; index + 1u < crossingCount; index += 2u)
            chord += boundaries[index + 1u] - boundaries[index];
        if(crossingCount > 4u || (crossingCount > 1u && (crossingCount & 1u) != 0u))
            chord = boundaries[crossingCount - 1u] - boundaries[0u];
        const f64 normalizedChord = Min(chord / 0.002, 1.0);
        const f64 fade = normalizedChord * normalizedChord * (3.0 - 2.0 * normalizedChord);
        const f64 interfaceTransmission = 1.0 + (Pow(0.96, static_cast<f64>(crossingCount)) - 1.0) * fade;
        const bool swapped = testCase.alternateTint && (instance & 1u) != 0u;
        const f64 tint[] = { swapped ? 0.75 : 0.5, swapped ? 0.5 : 0.75, 1.0 };
        for(usize channel = 0u; channel < 3u; ++channel)
            transmission[channel] *= interfaceTransmission * Pow(tint[channel], chord);
    }
    return { static_cast<f32>(transmission[0]), static_cast<f32>(transmission[1]), static_cast<f32>(transmission[2]) };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SoftwareStreamingKernelTest : public DescriptorBufferRoundTripTest{
protected:
    static void SetUpTestSuite(){
        s_logger.emplace();
        s_loggerGuard.emplace(*s_logger);
        s_scope = MakeUnique<HeadlessGraphicsScope>();
        ASSERT_TRUE(s_scope->graphics().setHardwareRayTracingPolicy(HardwareRayTracingPolicy::Disabled));
        if(!s_scope->initialize()){
            GTEST_SKIP() << "Software streaming regression requires a validation-backed descriptor-buffer device.";
            return;
        }
        s_validationBackedDeviceInitialized = true;
        EXPECT_FALSE(device().queryFeatureSupport(Feature::RayQuery));
        EXPECT_FALSE(device().queryFeatureSupport(Feature::RayTracingPipeline));
        EXPECT_FALSE(device().queryFeatureSupport(Feature::RayTracingAccelStruct));
        EXPECT_EQ(device().getDescriptorHeap().lifecycleStatistics().accelStructCapacity, 0u);
        EXPECT_FALSE(device().getDescriptorHeap().hasAccelStructLayout());
    }

    [[nodiscard]] bool loadKernel(Alloc::ScratchArena& scratchArena, const bool reference, ComputePipelineHandle& pipeline){
        const Path sourceRoot(arena(), NWB_SHADOW_KERNEL_SOURCE_ROOT);
        const Path outputRoot(arena(), NWB_SHADOW_KERNEL_OUTPUT_ROOT);
        const Path testRoot = sourceRoot / "tests/smoke/shadow_kernel/assets";
        const Path engineRoot = sourceRoot / "impl/assets/graphics";
        const Path source = testRoot / "streaming_traversal_cs.slang";
        ErrorCode error;
        if(!CreateDirectories(outputRoot, error) && error)
            return false;
        const Path output = outputRoot / (reference ? "streaming_reference_cs.spv" : "streaming_candidate_cs.spv");
        Impl::ShaderCook cook(arena());
        Impl::ShaderCook::ShaderEntry entry(arena());
        Impl::ShaderCook::CookVector<u8> bytes(arena());
        const bool cooked = [&]{
            if(!cook.parseShaderMeta(testRoot / "streaming_traversal_cs.nwb", entry, scratchArena))
                return false;
            Impl::ShaderCook::CookVector<Path> includes(arena());
            if(reference)
                includes.push_back(sourceRoot / "tests/smoke/shadow_kernel/reference/software_shadow");
            includes.push_back(testRoot);
            includes.push_back(engineRoot);
            includes.push_back(engineRoot / "shadow");
            const Impl::AssetsCsgCook::CsgShapeCookEntryVector noShapes(arena());
            Path generatedRoot(arena());
            if(!Impl::AssetsCsgCook::EmitCsgShapeModuleIncludes(outputRoot, "streaming", noShapes, generatedRoot, scratchArena))
                return false;
            includes.push_back(generatedRoot);
            Impl::ShaderCook::CookVector<Path> dependencies(arena());
            if(!cook.gatherShaderDependencies(source, includes, dependencies, scratchArena))
                return false;
            const Impl::ShaderCook::ShaderCompilerRequest request{
                .shaderName = "tests/shadow_kernel/streaming_traversal_cs",
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
            return cook.compileVariant(request, bytes) && !bytes.empty();
        }();
        if(!cooked){
            s_logger->emitErrorsToStderr();
            return false;
        }
        ShaderDesc shaderDesc(arena());
        shaderDesc.setShaderType(ShaderType::Compute).setEntryName(AStringView(entry.entryPoint.data(), entry.entryPoint.size()));
        const ShaderHandle shader = device().createShader(shaderDesc, bytes.data(), bytes.size());
        if(!shader)
            return false;
        BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(ShaderType::Compute).addItem(BindingLayoutItem::PushConstants(0u, sizeof(__hidden_streaming_traversal_kernel_tests::Push)));
        const BindingLayoutHandle layout = device().createBindingLayout(layoutDesc);
        if(!layout)
            return false;
        ComputePipelineDesc pipelineDesc;
        pipelineDesc
            .setComputeShader(shader)
            .addBindingLayout(layout)
            .addBindingLayout(device().getDescriptorHeap().getResourceLayout())
            .addBindingLayout(device().getDescriptorHeap().getSamplerLayout())
        ;
        pipeline = device().createComputePipeline(pipelineDesc);
        return pipeline.get() != nullptr;
    }

    void runCase(const __hidden_streaming_traversal_kernel_tests::Case& testCase, ComputePipeline& candidate, ComputePipeline& reference);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void SoftwareStreamingKernelTest::runCase(
    const __hidden_streaming_traversal_kernel_tests::Case& testCase,
    ComputePipeline& candidate,
    ComputePipeline& reference){
    using namespace __hidden_streaming_traversal_kernel_tests;
    SCOPED_TRACE(::testing::Message() << testCase.label.data());
    Alloc::ScratchArena scratchArena(Name("tests/shadow_kernel/streaming"));
    auto& graphicsDevice = device();
    auto& heap = graphicsDevice.getDescriptorHeap();
    Vector<Float3U, Alloc::ScratchArena> positions(scratchArena);
    Vector<u32, Alloc::ScratchArena> indices(scratchArena);
    Vector<Node, Alloc::ScratchArena> triangleLeaves(scratchArena);
    for(u32 lobe = 0u; lobe < testCase.lobes; ++lobe)
        AppendBox(lobe, positions, indices, triangleLeaves);
    Vector<Node, Alloc::ScratchArena> mesh(scratchArena);
    ASSERT_EQ(AppendTree(mesh, triangleLeaves, 0u, static_cast<u32>(triangleLeaves.size())), 0u);
    Vector<Attribute, Alloc::ScratchArena> attributes(indices.size(), scratchArena);
    Vector<Instance, Alloc::ScratchArena> instances(Max(testCase.count, 1u), scratchArena);
    Vector<Material, Alloc::ScratchArena> materials(Max(testCase.count, 1u), scratchArena);
    Vector<Node, Alloc::ScratchArena> instanceLeaves(scratchArena);
    for(u32 index = 0u; index < testCase.count; ++index){
        const f32 shift = static_cast<f32>(index) * testCase.spacing;
        const f32 sign = testCase.mirrored ? -1.0f : 1.0f;
        instances[index].inverseRows[0] = { sign, 0.0f, 0.0f, 0.0f };
        instances[index].inverseRows[1] = { 0.0f, 1.0f, 0.0f, 0.0f };
        instances[index].inverseRows[2] = { 0.0f, 0.0f, 1.0f / testCase.scale, -shift / testCase.scale };
        instances[index].primitiveCount = static_cast<u32>(indices.size() / 3u);
        materials[index].meshInstanceIndex = index;
        materials[index].modelId = testCase.alternateTint ? (index & 1u) : 0u;
        if(testCase.opaqueLast && index + 1u == testCase.count)
            materials[index].flags = 0u;
        instanceLeaves.push_back(Node{
            { -1.0f, -1.0f, testCase.scale + shift }, NWB_BVH_LEAF_FLAG | index,
            { 1.0f, 1.0f, static_cast<f32>(testCase.lobes * 2u) * testCase.scale + shift },
            1u | (materials[index].flags != 0u ? NWB_BVH_TRANSPARENT_SUBTREE_FLAG : 0u),
        });
    }
    if(testCase.reverse){
        for(usize index = 0u; index < instanceLeaves.size() / 2u; ++index)
            Swap(instanceLeaves[index], instanceLeaves[instanceLeaves.size() - 1u - index]);
    }
    Vector<Node, Alloc::ScratchArena> scene(scratchArena);
    if(testCase.count != 0u)
        ASSERT_EQ(AppendTree(scene, instanceLeaves, 0u, testCase.count), 0u);
    else
        scene.push_back(Node{});
    ContextSlots context;
    Float4U sentinel[2];
    NWB_MEMSET(sentinel, 0xa5, sizeof(sentinel));
    const void* sources[BufferIndex::Count] = {
        scene.data(), mesh.data(), positions.data(), indices.data(), attributes.data(),
        instances.data(), materials.data(), &context, sentinel, sentinel,
    };
    const usize sizes[BufferIndex::Count] = {
        scene.size() * sizeof(Node), mesh.size() * sizeof(Node), positions.size() * sizeof(Float3U),
        indices.size() * sizeof(u32), attributes.size() * sizeof(Attribute), instances.size() * sizeof(Instance),
        materials.size() * sizeof(Material), sizeof(context), sizeof(sentinel), sizeof(sentinel),
    };
    BufferHandle buffers[BufferIndex::Count];
    GpuDescriptorHandle slots[BufferIndex::Count]{};
    ScopeExit release([&]()noexcept{
        for(const GpuDescriptorHandle slot : slots){
            if(slot.valid())
                heap.free(slot);
        }
        heap.collectRetired();
    });
    for(u32 index = 0u; index < BufferIndex::Count; ++index){
        const bool constant = index == BufferIndex::Context;
        const bool output = index >= BufferIndex::Candidate;
        BufferDesc desc;
        desc.setByteSize(sizes[index]).setInitialState(ResourceStates::Common).setKeepInitialState(true);
        if(constant)
            desc.setIsConstantBuffer(true);
        else
            desc.setCanHaveRawViews(true);
        if(output)
            desc.setCanHaveUAVs(true).setCpuAccess(CpuAccessMode::Read);
        buffers[index] = graphicsDevice.createBuffer(desc);
        ASSERT_TRUE(buffers[index]);
        slots[index] = heap.allocate(constant ? GpuDescriptorClass::UniformBuffer : GpuDescriptorClass::StorageBuffer);
        ASSERT_TRUE(slots[index].valid());
        const DescriptorWriteItem item = constant ? DescriptorWriteItem::ConstantBuffer(0u, buffers[index].get())
            : output ? DescriptorWriteItem::RawBuffer_UAV(0u, buffers[index].get())
            : DescriptorWriteItem::RawBuffer_SRV(0u, buffers[index].get());
        ASSERT_TRUE(heap.write(slots[index], item));
    }
    for(Material& material : materials){
        material.positionSlot = slots[BufferIndex::Positions].slot();
        material.indexSlot = slots[BufferIndex::Indices].slot();
        material.attributeSlot = slots[BufferIndex::Attributes].slot();
        material.nodeSlot = slots[BufferIndex::Mesh].slot();
    }
    context.scene[0] = slots[BufferIndex::Scene].slot();
    context.scene[1] = slots[BufferIndex::Instances].slot();
    context.scene[2] = slots[BufferIndex::Materials].slot();
    context.scene[3] = slots[BufferIndex::Positions].slot();
    context.material[0] = slots[BufferIndex::Positions].slot();
    const CommandListHandle commands = graphicsDevice.createCommandList();
    ASSERT_TRUE(commands);
    commands->open();
    for(u32 index = 0u; index < BufferIndex::Count; ++index){
        ASSERT_TRUE(commands->tryWriteBuffer(*buffers[index], sources[index], sizes[index]));
        commands->setBufferState(buffers[index].get(), index == BufferIndex::Context ? ResourceStates::ConstantBuffer
            : index >= BufferIndex::Candidate ? ResourceStates::UnorderedAccess : ResourceStates::ShaderResource);
    }
    commands->commitBarriers();
    for(u32 arm = 0u; arm < 2u; ++arm){
        ComputePipeline& pipeline = arm == 0u ? candidate : reference;
        ComputeState state;
        state.setPipeline(&pipeline);
        commands->setComputeState(state);
        heap.bindCompute(*commands, pipeline);
        const Push push{
            slots[BufferIndex::Context].slot(), slots[BufferIndex::Candidate + arm].slot(), testCase.count, 0u,
            { testCase.rayX, testCase.rayY, testCase.originZ, 0.0001f }, { 0.0f, 0.0f, 1.0f, testCase.tMax },
        };
        commands->setPushConstants(&push, sizeof(push));
        commands->dispatch(1u, 1u, 1u);
    }
    ASSERT_FALSE(commands->commandRecordingFailed());
    commands->close();
    ASSERT_FALSE(commands->commandRecordingFailed());
    CommandList* lists[] = { commands.get() };
    const QueueSubmissionToken token = graphicsDevice.executeCommandLists(
        lists, LengthOf(lists), commands->getDescription().physicalQueue, QueueSubmissionDesc{}
    );
    ASSERT_TRUE(token.valid());
    ASSERT_TRUE(graphicsDevice.waitForIdle());
    Float4U actual[2]{};
    for(u32 arm = 0u; arm < 2u; ++arm){
        Buffer& output = *buffers[BufferIndex::Candidate + arm];
        const Float4U* values = static_cast<const Float4U*>(graphicsDevice.mapBuffer(output, CpuAccessMode::Read));
        ASSERT_NE(values, nullptr);
        EXPECT_EQ(NWB_MEMCMP(&values[1], &sentinel[1], sizeof(Float4U)), 0);
        actual[arm] = values[0];
        graphicsDevice.unmapBuffer(output);
        EXPECT_FLOAT_EQ(actual[arm].w, 1.0f);
    }
    const Float3U expected = Expected(testCase);
    const f32 expectedChannels[] = { expected.x, expected.y, expected.z };
    const f32 candidateChannels[] = { actual[0].x, actual[0].y, actual[0].z };
    const f32 referenceChannels[] = { actual[1].x, actual[1].y, actual[1].z };
    for(usize channel = 0u; channel < 3u; ++channel){
        EXPECT_TRUE(IsFinite(candidateChannels[channel]));
        EXPECT_GE(candidateChannels[channel], 0.0f);
        EXPECT_LE(candidateChannels[channel], 1.0f);
        // Up to eight products/sums use half arithmetic; this absolute bound is below the missing-fourth-box error.
        EXPECT_NEAR(candidateChannels[channel], expectedChannels[channel], 0.004f);
        if(testCase.count <= 3u){
            EXPECT_NEAR(referenceChannels[channel], expectedChannels[channel], 0.004f);
            const i32 difference = static_cast<i32>(ConvertFloatToHalf(candidateChannels[channel]))
                - static_cast<i32>(ConvertFloatToHalf(referenceChannels[channel]));
            EXPECT_LE(Abs(difference), 1) << "streaming must remain within one final half ULP of the frozen predecessor";
        }
    }
    if(testCase.capDifference)
        EXPECT_LT(actual[0].z + 0.01f, actual[1].z) << "the fourth and later instances must darken the zero-absorption blue channel through Fresnel";
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(SoftwareStreamingKernelTest, PreservesCompletedOpticsAndAccumulatesEveryInstance){
    using namespace __hidden_streaming_traversal_kernel_tests;
    const Common::LoggerRegistrationGuard diagnostics(*s_logger, Common::LoggerBreakPolicy::BreakOnFatal);
    Alloc::ScratchArena scratchArena(Name("tests/shadow_kernel/streaming_cook"));
    ComputePipelineHandle candidate;
    ComputePipelineHandle reference;
    ASSERT_TRUE(loadKernel(scratchArena, false, candidate));
    ASSERT_TRUE(loadKernel(scratchArena, true, reference));
    const Case cases[] = {
        { "empty scene", 0u },
        { "one closed box", 1u },
        { "two overlapping distinct tints", 2u, 0.25f, 1.0f, 0.0f, 0.125f, 0.25f, 100.0f, 1u, true },
        { "three coincident instances", 3u },
        { "four coincident instances", 4u, 0.0f, 1.0f, 0.0f, 0.125f, 0.25f, 100.0f, 1u, false, false, false, false, true },
        { "eight coincident instances", 8u, 0.0f, 1.0f, 0.0f, 0.125f, 0.25f, 100.0f, 1u, true, false, false, false, true },
        { "six overlapping instances", 6u, 0.125f, 1.0f, 0.0f, 0.125f, 0.25f, 100.0f, 1u, true, false, false, false, true },
        { "five separated instances", 5u, 2.0f, 1.0f, 0.0f, 0.125f, 0.25f, 100.0f, 1u, false, false, false, false, true },
        { "three thin chords retain interface fade", 3u, 0.01f, 0.001f },
        { "four thin chords retain interface fade", 4u, 0.01f, 0.001f, 0.0f, 0.125f, 0.25f, 100.0f, 1u, false, false, false, false, true },
        { "three shared triangle edges merge", 3u, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f },
        { "four shared triangle edges merge", 4u, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 100.0f, 1u, false, false, false, false, true },
        { "mirrored nonuniform instances", 4u, 0.125f, 0.5f, 0.0f, 0.125f, 0.25f, 100.0f, 1u, true, true, false, false, true },
        { "reversed leaf order", 4u, 0.0f, 1.0f, 0.0f, 0.125f, 0.25f, 100.0f, 1u, true, false, true, false, true },
        { "inside singleton policy remains independent", 3u, 0.0f, 1.0f, 1.5f },
        { "inside singletons do not exhaust an instance table", 6u, 0.2f, 1.0f, 1.5f },
        { "finite range retains only complete chords", 3u, 0.5f, 1.0f, 0.0f, 0.125f, 0.25f, 2.25f },
        { "missed geometry remains lit", 4u, 0.0f, 1.0f, 0.0f, 2.0f },
        { "opaque-only scene is pruned", 1u, 0.0f, 1.0f, 0.0f, 0.125f, 0.25f, 100.0f, 1u, false, false, false, true },
        { "opaque instance does not consume transparent contribution", 5u, 0.0f, 1.0f, 0.0f, 0.125f, 0.25f, 100.0f, 1u, false, false, false, true, true },
        { "four boundaries preserve disjoint chords", 3u, 0.0f, 1.0f, 0.0f, 0.125f, 0.25f, 100.0f, 2u },
        { "existing per-mesh overflow span policy", 3u, 0.0f, 1.0f, 0.0f, 0.125f, 0.25f, 100.0f, 3u },
    };
    for(const Case& testCase : cases){
        runCase(testCase, *candidate, *reference);
        if(HasFatalFailure()){
            s_logger->emitErrorsToStderr();
            s_logger->emitMessagesContainingToStderr(NWB_TEXT("Vulkan debug: [severity=error"));
            return;
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

