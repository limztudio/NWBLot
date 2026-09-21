// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shadow_kernel_fixture.h"

#include <impl/assets/graphics/bvh/constants.h>
#include <impl/assets/graphics/bvh/scene_refit_constants.h>
#include <impl/assets_csg/cook.h>
#include <impl/assets_shader/cook.h>

#include <global/filesystem.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


constexpr u32 s_ExpectedDualCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_scene_refit_kernel_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct Node{
    Float3U minimum;
    u32 left;
    Float3U maximum;
    u32 right;
};

struct RefitInput{
    Float4U rows[3];
    u32 meshNodeSlot = 0u;
    u32 reserved[3]{};
};

struct Instance{
    Float4U inverseRows[3];
    u32 reservedMeshIndex = 0u;
    u32 primitiveCount = 1u;
    u32 padding[2]{};
};

struct Material{
    u32 modelId = 0u;
    u32 flags = 0u;
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

struct RefitPush{
    u32 nodeCount;
    u32 sceneNodeSlot;
    u32 instanceInputSlot;
    u32 instanceCount;
};

struct TracePush{
    u32 materialContextSlotsHeapSlot;
    u32 outputSlot;
    u32 instanceCount;
    u32 outputWordOffset;
    Float4U origin;
    Float4U direction;
};

struct Observation{
    f32 transmission[3];
    u32 directStatus;
};

struct Bounds{
    f64 minimum[3]{};
    f64 maximum[3]{};
};

struct Case{
    AStringView label;
    u32 instanceCount;
    bool mixedStatic = false;
    bool mirrored = false;
    bool invalidRoot = false;
    bool overflowingTransform = false;
    bool subnormalRoot = false;
};

namespace BufferIndex{
    enum Enum : u32{
        Scene = 0u, Inputs, DynamicRoot, StaticRoot, DynamicPositions, StaticPositions,
        Indices, Attributes, Instances, Materials, Context, Output, Count,
    };
};

inline constexpr f64 s_FloatEpsilon = 1.1920928955078125e-7;

static_assert(sizeof(Node) == 32u);
static_assert(sizeof(RefitInput) == NWB_SCENE_BVH_REFIT_INSTANCE_BYTES);
static_assert(offsetof(RefitInput, meshNodeSlot) == NWB_SCENE_BVH_REFIT_MESH_SLOT_OFFSET);
static_assert(sizeof(Instance) == 64u);
static_assert(sizeof(Material) == 36u);
static_assert(sizeof(Attribute) == 16u);
static_assert(sizeof(ContextSlots) == 32u);
static_assert(sizeof(RefitPush) == NWB_SCENE_BVH_REFIT_PUSH_BYTES);
static_assert(sizeof(TracePush) == 48u);
static_assert(sizeof(Observation) == 16u);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] f64 Component(const Float3U& value, const usize axis){
    return axis == 0u ? value.x : axis == 1u ? value.y : value.z;
}

[[nodiscard]] Bounds TransformCorners(const Node& local, const RefitInput& input){
    Bounds result;
    for(usize axis = 0u; axis < 3u; ++axis){
        result.minimum[axis] = Limit<f64>::s_Max;
        result.maximum[axis] = -Limit<f64>::s_Max;
    }
    // Independent FP64 corner oracle; no interval-product selection or shader padding formula is reproduced.
    for(u32 corner = 0u; corner < 8u; ++corner){
        const f64 x = (corner & 1u) != 0u ? local.maximum.x : local.minimum.x;
        const f64 y = (corner & s_ExpectedDualCount) != 0u ? local.maximum.y : local.minimum.y;
        const f64 z = (corner & 4u) != 0u ? local.maximum.z : local.minimum.z;
        for(usize axis = 0u; axis < 3u; ++axis){
            const Float4U& row = input.rows[axis];
            const f64 point = static_cast<f64>(row.w) + static_cast<f64>(row.x) * x
                + static_cast<f64>(row.y) * y + static_cast<f64>(row.z) * z;
            result.minimum[axis] = Min(result.minimum[axis], point);
            result.maximum[axis] = Max(result.maximum[axis], point);
        }
    }
    return result;
}

[[nodiscard]] Bounds Unite(const Bounds& first, const Bounds& second){
    Bounds result;
    for(usize axis = 0u; axis < 3u; ++axis){
        result.minimum[axis] = Min(first.minimum[axis], second.minimum[axis]);
        result.maximum[axis] = Max(first.maximum[axis], second.maximum[axis]);
    }
    return result;
}

void StoreBounds(Node& node, const Bounds& bounds){
    node.minimum = { static_cast<f32>(bounds.minimum[0]), static_cast<f32>(bounds.minimum[1]), static_cast<f32>(bounds.minimum[2]) };
    node.maximum = { static_cast<f32>(bounds.maximum[0]), static_cast<f32>(bounds.maximum[1]), static_cast<f32>(bounds.maximum[2]) };
}

[[nodiscard]] u32 AppendSceneTree(
    Vector<Node, Alloc::ScratchArena>& nodes,
    const Vector<RefitInput, Alloc::ScratchArena>& inputs,
    const Node& oldRoot,
    const u32 first,
    const u32 count){
    const u32 index = static_cast<u32>(nodes.size());
    nodes.push_back(Node{});
    if(count == 1u){
        StoreBounds(nodes[index], TransformCorners(oldRoot, inputs[first]));
        nodes[index].left = NWB_BVH_LEAF_FLAG | first;
        // Mixed high-bit tags exercise exact metadata preservation without changing this opaque trace fixture.
        nodes[index].right = 1u | ((first & 1u) == 0u ? NWB_BVH_TRANSPARENT_SUBTREE_FLAG : 0u);
        return index;
    }
    const u32 leftCount = count / s_ExpectedDualCount;
    const u32 left = AppendSceneTree(nodes, inputs, oldRoot, first, leftCount);
    const u32 right = AppendSceneTree(nodes, inputs, oldRoot, first + leftCount, count - leftCount);
    nodes[index].minimum = {
        Min(nodes[left].minimum.x, nodes[right].minimum.x),
        Min(nodes[left].minimum.y, nodes[right].minimum.y),
        Min(nodes[left].minimum.z, nodes[right].minimum.z),
    };
    nodes[index].maximum = {
        Max(nodes[left].maximum.x, nodes[right].maximum.x),
        Max(nodes[left].maximum.y, nodes[right].maximum.y),
        Max(nodes[left].maximum.z, nodes[right].maximum.z),
    };
    nodes[index].left = left;
    nodes[index].right = right | ((nodes[left].right | nodes[right].right) & NWB_BVH_TRANSPARENT_SUBTREE_FLAG);
    return index;
}

void SetTransform(RefitInput& input, Instance& instance, const u32 index, const bool mirrored){
    const f32 x = index == 0u ? 0.0f : 64.0f + static_cast<f32>(index) * 7.0f;
    const f32 sx = mirrored ? -2.0f : 1.0f;
    const f32 sy = mirrored ? 0.5f : 1.0f;
    const f32 sz = mirrored ? 3.0f : 1.0f;
    const f32 y = mirrored ? -4.0f : 0.0f;
    const f32 z = mirrored ? 2.0f : 0.0f;
    input.rows[0] = { sx, 0.0f, 0.0f, x };
    input.rows[1] = { 0.0f, sy, 0.0f, y };
    input.rows[2] = { 0.0f, 0.0f, sz, z };
    instance.inverseRows[0] = { 1.0f / sx, 0.0f, 0.0f, -x / sx };
    instance.inverseRows[1] = { 0.0f, 1.0f / sy, 0.0f, -y / sy };
    instance.inverseRows[2] = { 0.0f, 0.0f, 1.0f / sz, -z / sz };
}

void ExpectBounds(const Node& actual, const Bounds& exact, const f64 tolerance){
    for(usize axis = 0u; axis < 3u; ++axis){
        const f64 low = Component(actual.minimum, axis);
        const f64 high = Component(actual.maximum, axis);
        EXPECT_TRUE(IsFinite(low) && IsFinite(high));
        EXPECT_LE(low, exact.minimum[axis]);
        EXPECT_GE(high, exact.maximum[axis]);
        EXPECT_LE(exact.minimum[axis] - low, tolerance);
        EXPECT_LE(high - exact.maximum[axis], tolerance);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SceneRefitKernelTest : public DescriptorBufferRoundTripTest{
protected:
    static void SetUpTestSuite(){
        s_logger.emplace();
        s_loggerGuard.emplace(*s_logger);
        s_scope = MakeUnique<HeadlessGraphicsScope>();
        ASSERT_TRUE(s_scope->graphics().setHardwareRayTracingPolicy(HardwareRayTracingPolicy::Disabled));
        if(!s_scope->initialize()){
            GTEST_SKIP() << "Scene-refit regression requires a validation-backed descriptor-buffer device.";
            return;
        }
        s_validationBackedDeviceInitialized = true;
        auto& graphicsDevice = device();
        EXPECT_FALSE(graphicsDevice.queryFeatureSupport(Feature::RayQuery));
        EXPECT_FALSE(graphicsDevice.queryFeatureSupport(Feature::RayTracingPipeline));
        EXPECT_FALSE(graphicsDevice.queryFeatureSupport(Feature::RayTracingAccelStruct));
        EXPECT_EQ(graphicsDevice.getDescriptorHeap().lifecycleStatistics().accelStructCapacity, 0u);
        EXPECT_FALSE(graphicsDevice.getDescriptorHeap().hasAccelStructLayout());
    }

    [[nodiscard]] bool loadKernel(
        Alloc::ScratchArena& scratchArena,
        const bool traversal,
        ComputePipelineHandle& pipeline){
        const Path sourceRoot(arena(), NWB_SHADOW_KERNEL_SOURCE_ROOT);
        const Path outputRoot(arena(), NWB_SHADOW_KERNEL_OUTPUT_ROOT);
        const Path testRoot = sourceRoot / "tests/smoke/shadow_kernel/assets";
        const Path engineRoot = sourceRoot / "impl/assets/graphics";
        const Path source = traversal ? testRoot / "scene_refit_traversal_cs.slang" : engineRoot / "bvh/scene_refit_cs.slang";
        const Path metadata = traversal ? testRoot / "scene_refit_traversal_cs.nwb" : engineRoot / "bvh/scene_refit_cs.nwb";
        ErrorCode error;
        if(!CreateDirectories(outputRoot, error) && error)
            return false;
        const Path output = outputRoot / (traversal ? "scene_refit_traversal_cs.spv" : "scene_refit_cs.spv");
        Impl::ShaderCook cook(arena());
        Impl::ShaderCook::ShaderEntry entry(arena());
        Impl::ShaderCook::CookVector<u8> bytes(arena());
        const bool cooked = [&]{
            if(!cook.parseShaderMeta(metadata, entry, scratchArena))
                return false;
            Impl::ShaderCook::CookVector<Path> includes(arena());
            includes.push_back(testRoot);
            includes.push_back(engineRoot);
            const Impl::AssetsCsgCook::CsgShapeCookEntryVector noShapes(arena());
            Path generatedRoot(arena());
            if(!Impl::AssetsCsgCook::EmitCsgShapeModuleIncludes(outputRoot, "scene_refit", noShapes, generatedRoot, scratchArena))
                return false;
            includes.push_back(generatedRoot);
            Impl::ShaderCook::CookVector<Path> dependencies(arena());
            if(!cook.gatherShaderDependencies(source, includes, dependencies, scratchArena))
                return false;
            const Impl::ShaderCook::ShaderCompilerRequest request{
                .shaderName = traversal ? "tests/shadow_kernel/scene_refit_traversal_cs" : "engine/graphics/bvh/scene_refit_cs",
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
        layoutDesc.setVisibility(ShaderType::Compute).addItem(BindingLayoutItem::PushConstants(0u, traversal ? 48u : NWB_SCENE_BVH_REFIT_PUSH_BYTES));
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

    void runCase(
        const __hidden_scene_refit_kernel_tests::Case& testCase,
        ComputePipeline& refitPipeline,
        ComputePipeline& tracePipeline);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void SceneRefitKernelTest::runCase(
    const __hidden_scene_refit_kernel_tests::Case& testCase,
    ComputePipeline& refitPipeline,
    ComputePipeline& tracePipeline){
    using namespace __hidden_scene_refit_kernel_tests;
    SCOPED_TRACE(::testing::Message() << testCase.label.data());
    Alloc::ScratchArena scratchArena(Name("tests/shadow_kernel/scene_refit"));
    auto& graphicsDevice = device();
    auto& heap = graphicsDevice.getDescriptorHeap();
    const u32 count = testCase.instanceCount;
    const u32 nodeCount = count == 0u ? 0u : count * s_ExpectedDualCount - 1u;
    Node roots[] = {
        { { 9.0f, -1.0f, 1.0f }, NWB_BVH_LEAF_FLAG, { 11.0f, 1.0f, 1.0f }, 1u },
        { { -1.0f, -1.0f, 1.0f }, NWB_BVH_LEAF_FLAG, { 1.0f, 1.0f, 1.0f }, 1u },
    };
    const Float3U dynamicPositions[] = { { 9.0f, -1.0f, 1.0f }, { 11.0f, -1.0f, 1.0f }, { 10.0f, 1.0f, 1.0f } };
    const Float3U staticPositions[] = { { -1.0f, -1.0f, 1.0f }, { 1.0f, -1.0f, 1.0f }, { 0.0f, 1.0f, 1.0f } };
    const u32 indices[] = { 0u, 1u, s_ExpectedDualCount };
    const Attribute attributes[3]{};
    Vector<RefitInput, Alloc::ScratchArena> inputs(Max(count, 1u), scratchArena);
    Vector<Instance, Alloc::ScratchArena> instances(Max(count, 1u), scratchArena);
    Vector<Material, Alloc::ScratchArena> materials(Max(count, 1u), scratchArena);
    for(u32 index = 0u; index < count; ++index)
        SetTransform(inputs[index], instances[index], index, testCase.mirrored);
    Vector<Node, Alloc::ScratchArena> nodes(scratchArena);
    nodes.reserve(nodeCount + 1u);
    if(count != 0u)
        ASSERT_EQ(AppendSceneTree(nodes, inputs, roots[1], 0u, count), 0u);
    Node guard;
    NWB_MEMSET(&guard, 0xa5, sizeof(guard));
    nodes.push_back(guard);
    if(testCase.invalidRoot)
        roots[0].minimum.x = Limit<f32>::s_Max;
    if(testCase.overflowingTransform)
        inputs[0].rows[0].x = Limit<f32>::s_Max;
    if(testCase.subnormalRoot){
        roots[0].minimum = { -1.0e-39f, -1.0e-39f, -1.0e-39f };
        roots[0].maximum = { 1.0e-39f, 1.0e-39f, 1.0e-39f };
        inputs[0].rows[0] = { 1.0e38f, 0.0f, 0.0f, 0.0f };
        inputs[0].rows[1] = { 0.0f, -1.0e38f, 0.0f, 0.0f };
        inputs[0].rows[2] = { 0.0f, 0.0f, 1.0e38f, 0.0f };
    }
    ContextSlots context;
    Observation observations[3];
    NWB_MEMSET(observations, 0xa5, sizeof(observations));
    const void* sources[BufferIndex::Count] = {
        nodes.data(), inputs.data(), &roots[0], &roots[1], dynamicPositions, staticPositions,
        indices, attributes, instances.data(), materials.data(), &context, observations,
    };
    const usize byteSizes[BufferIndex::Count] = {
        nodes.size() * sizeof(Node), inputs.size() * sizeof(RefitInput), sizeof(Node), sizeof(Node),
        sizeof(dynamicPositions), sizeof(staticPositions), sizeof(indices), sizeof(attributes),
        instances.size() * sizeof(Instance), materials.size() * sizeof(Material), sizeof(context), sizeof(observations),
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
        const bool output = index == BufferIndex::Scene || index == BufferIndex::Output;
        BufferDesc desc;
        desc.setByteSize(byteSizes[index]).setInitialState(ResourceStates::Common).setKeepInitialState(true);
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
    for(u32 index = 0u; index < count; ++index){
        const bool fixed = testCase.mixedStatic && (index & 1u) != 0u;
        inputs[index].meshNodeSlot = slots[fixed ? BufferIndex::StaticRoot : BufferIndex::DynamicRoot].slot();
        Material& material = materials[index];
        material.meshInstanceIndex = index;
        material.indexSlot = slots[BufferIndex::Indices].slot();
        material.attributeSlot = slots[BufferIndex::Attributes].slot();
        material.positionSlot = slots[fixed ? BufferIndex::StaticPositions : BufferIndex::DynamicPositions].slot();
        material.nodeSlot = inputs[index].meshNodeSlot;
    }
    context.scene[0] = slots[BufferIndex::Scene].slot();
    context.scene[1] = slots[BufferIndex::Instances].slot();
    context.scene[2] = slots[BufferIndex::Materials].slot();
    context.scene[3] = slots[BufferIndex::DynamicPositions].slot();
    context.material[0] = slots[BufferIndex::DynamicPositions].slot();
    const bool trace = count != 0u && !testCase.invalidRoot && !testCase.overflowingTransform && !testCase.subnormalRoot;
    const CommandListHandle commands = graphicsDevice.createCommandList();
    ASSERT_TRUE(commands);
    commands->open();
    for(u32 index = 0u; index < BufferIndex::Count; ++index){
        ASSERT_TRUE(commands->tryWriteBuffer(*buffers[index], sources[index], byteSizes[index]));
        commands->setBufferState(buffers[index].get(), index == BufferIndex::Context ? ResourceStates::ConstantBuffer
            : index == BufferIndex::Output ? ResourceStates::UnorderedAccess : ResourceStates::ShaderResource);
    }
    commands->commitBarriers();
    const auto recordTrace = [&](const u32 phase){
        ComputeState state;
        state.setPipeline(&tracePipeline);
        commands->setComputeState(state);
        heap.bindCompute(*commands, tracePipeline);
        const TracePush push{
            slots[BufferIndex::Context].slot(), slots[BufferIndex::Output].slot(), count, phase * 4u,
            { testCase.mirrored ? -20.0f : 10.0f, testCase.mirrored ? -4.0f : 0.0f, 0.0f, 0.0f },
            { 0.0f, 0.0f, 1.0f, 10.0f },
        };
        commands->setPushConstants(&push, sizeof(push));
        commands->dispatch(1u, 1u, 1u);
    };
    if(trace)
        recordTrace(0u);
    commands->setBufferState(buffers[BufferIndex::Scene].get(), ResourceStates::UnorderedAccess, true);
    commands->commitBarriers();
    ComputeState refitState;
    refitState.setPipeline(&refitPipeline);
    commands->setComputeState(refitState);
    heap.bindCompute(*commands, refitPipeline);
    const RefitPush push{ nodeCount, slots[BufferIndex::Scene].slot(), slots[BufferIndex::Inputs].slot(), count };
    commands->setPushConstants(&push, sizeof(push));
    commands->dispatch(1u, 1u, 1u);
    commands->setBufferState(buffers[BufferIndex::Scene].get(), ResourceStates::ShaderResource);
    commands->setBufferState(buffers[BufferIndex::Output].get(), ResourceStates::UnorderedAccess, true);
    commands->commitBarriers();
    if(trace)
        recordTrace(1u);
    ASSERT_FALSE(commands->commandRecordingFailed());
    commands->close();
    ASSERT_FALSE(commands->commandRecordingFailed());
    CommandList* const lists[] = { commands.get() };
    const QueueSubmissionToken token = graphicsDevice.executeCommandLists(
        lists, LengthOf(lists), commands->getDescription().physicalQueue, QueueSubmissionDesc{}
    );
    ASSERT_TRUE(token.valid());
    ASSERT_TRUE(graphicsDevice.waitForIdle());
    const Node* actual = static_cast<const Node*>(graphicsDevice.mapBuffer(*buffers[BufferIndex::Scene], CpuAccessMode::Read));
    ASSERT_NE(actual, nullptr);
    ScopeExit unmapScene([&]()noexcept{ graphicsDevice.unmapBuffer(*buffers[BufferIndex::Scene]); });
    EXPECT_EQ(NWB_MEMCMP(&actual[nodeCount], &guard, sizeof(guard)), 0);
    Vector<Bounds, Alloc::ScratchArena> expected(Max(nodeCount, 1u), scratchArena);
    for(u32 reverse = nodeCount; reverse != 0u; --reverse){
        const u32 index = reverse - 1u;
        SCOPED_TRACE(index);
        const Node& before = nodes[index];
        EXPECT_EQ(actual[index].left, before.left);
        EXPECT_EQ(actual[index].right, before.right);
        if((before.left & NWB_BVH_LEAF_FLAG) != 0u){
            const u32 instance = before.left & NWB_BVH_CHILD_INDEX_MASK;
            const bool fixed = testCase.mixedStatic && (instance & 1u) != 0u;
            if((testCase.invalidRoot && !fixed) || (testCase.overflowingTransform && instance == 0u)){
                for(usize axis = 0u; axis < 3u; ++axis){
                    expected[index].minimum[axis] = -static_cast<f64>(Limit<f32>::s_Max);
                    expected[index].maximum[axis] = static_cast<f64>(Limit<f32>::s_Max);
                }
            }
            else
                expected[index] = TransformCorners(roots[fixed ? 1u : 0u], inputs[instance]);
        }
        else
            expected[index] = Unite(expected[before.left], expected[before.right & NWB_BVH_CHILD_INDEX_MASK]);
        // Each leaf is padded once, then ancestors only select extrema. Allow a 64-epsilon scene-scale
        // forward-error bound with exact FP64 corner containment; oversized boxes fail every finite case.
        const f64 scale = 64.0 + static_cast<f64>(count) * 7.0;
        f64 tolerance = scale * 64.0 * s_FloatEpsilon;
        if(testCase.subnormalRoot){
            // Each diagonal affine row can magnify one flushed subnormal coordinate by at most this amount.
            constexpr f64 smallestNormal = 1.17549435082228750797e-38;
            const f64 representationError = static_cast<f64>(inputs[0].rows[0].x) * smallestNormal;
            tolerance += representationError * (1.0 + 32.0 * s_FloatEpsilon);
        }
        ExpectBounds(actual[index], expected[index], tolerance);
    }
    const Observation* observed = static_cast<const Observation*>(graphicsDevice.mapBuffer(*buffers[BufferIndex::Output], CpuAccessMode::Read));
    ASSERT_NE(observed, nullptr);
    ScopeExit unmapOutput([&]()noexcept{ graphicsDevice.unmapBuffer(*buffers[BufferIndex::Output]); });
    EXPECT_EQ(NWB_MEMCMP(&observed[2], &observations[2], sizeof(Observation)), 0);
    if(trace){
        EXPECT_EQ(observed[0].directStatus, s_ExpectedDualCount);
        EXPECT_EQ(observed[1].directStatus, s_ExpectedDualCount);
        for(usize channel = 0u; channel < 3u; ++channel){
            EXPECT_FLOAT_EQ(observed[0].transmission[channel], 1.0f);
            EXPECT_FLOAT_EQ(observed[1].transmission[channel], 0.0f);
        }
    }
    else
        EXPECT_EQ(NWB_MEMCMP(observed, observations, sizeof(observations)), 0);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(SceneRefitKernelTest, PosedRootsRefitSceneBoundsBeforeSoftwareTraversal){
    using namespace __hidden_scene_refit_kernel_tests;
    const Common::LoggerRegistrationGuard diagnostics(*s_logger, Common::LoggerBreakPolicy::BreakOnFatal);
    Alloc::ScratchArena scratchArena(Name("tests/shadow_kernel/scene_refit_cook"));
    ComputePipelineHandle refit;
    ComputePipelineHandle trace;
    ASSERT_TRUE(loadKernel(scratchArena, false, refit));
    ASSERT_TRUE(loadKernel(scratchArena, true, trace));
    const Case cases[] = {
        { "singleton moved beyond bind bounds", 1u },
        { "mixed fixed and moving roots", s_ExpectedDualCount, true },
        { "node count crosses workgroup", 65u, true },
        { "strided leaves beyond workgroup", 130u, true },
        { "mirrored nonuniform singleton", 1u, false, true },
        { "mirrored nonuniform strided scene", 130u, true, true },
        { "empty scene leaves guard untouched", 0u },
        { "invalid root becomes conservative", 1u, false, false, true },
        { "invalid child poisons all ancestors", 3u, true, false, true },
        { "overflowing transform becomes conservative", 1u, false, false, false, true },
        { "subnormal root under large finite affine scale", 1u, false, false, false, false, true },
    };
    for(const Case& testCase : cases){
        runCase(testCase, *refit, *trace);
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

