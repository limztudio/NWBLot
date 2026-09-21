// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shadow_kernel_fixture.h"

#include <impl/assets/graphics/bvh/constants.h>
#include <impl/assets/graphics/shadow/constants.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_shadow_traversal_kernel_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Only the resource ABI is mirrored here. Ray traversal, intersection, crossing integration, and finalization compile from production Slang.
struct Node{
    Float3U minimum;
    u32 left;
    Float3U maximum;
    u32 right;
};

struct Instance{
    Float4U rows[3] = { { 1.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f, 0.0f } };
    u32 reservedMeshIndex = 0u;
    u32 primitiveCount = 4u;
    u32 padding[2]{};
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
    Float2U uv{ 0.0f, 0.0f };
};

struct ContextSlots{
    u32 scene[4]{};
    u32 material[4]{};
};

struct PushConstants{
    u32 materialContextSlot;
    u32 outputSlot;
    u32 instanceCount;
};

struct Observation{
    u32 status;
    u32 crossingCount;
    f32 chordLength;
    f32 transmission[3];
    u32 overflow;
    f32 interfaceTransmittance;
};

static_assert(sizeof(Node) == 32u);
static_assert(sizeof(Instance) == 64u);
static_assert(sizeof(Material) == 36u);
static_assert(sizeof(Attribute) == 16u);
static_assert(sizeof(ContextSlots) == 32u);
static_assert(sizeof(PushConstants) == 12u);
static_assert(sizeof(Observation) == 32u);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(ShadowKernelTest, CompletedCrossingsSurviveTheLastInternalSubtreeMiss){
    using namespace __hidden_shadow_traversal_kernel_tests;
    Alloc::ScratchArena scratchArena(Name("tests/smoke/shadow_kernel/traversal"));
    ComputePipelineHandle pipeline;
    ASSERT_TRUE(loadTraversalKernel(scratchArena, pipeline));
    auto& graphicsDevice = device();
    auto& heap = graphicsDevice.getDescriptorHeap();

    // The near subtree intersects two boundaries at z=1 and z=2. The deferred far parent's AABB intersects the ray,
    // but both child boxes miss at x=0. Exhausting that last internal subtree must preserve the earlier crossings.
    const Node nodes[] = {
        { { -3.0f, -1.0f, 1.0f }, 1u, { 3.0f, 1.0f, 5.0f }, s_ExpectedDualCount },
        { { -1.0f, -1.0f, 1.0f }, 3u, { 1.0f, 1.0f, 2.0f }, 4u },
        { { -3.0f, -1.0f, 5.0f }, 5u, { 3.0f, 1.0f, 5.0f }, 6u },
        { { -1.0f, -1.0f, 1.0f }, NWB_BVH_LEAF_FLAG | 0u, { 1.0f, 1.0f, 1.0f }, 1u },
        { { -1.0f, -1.0f, 2.0f }, NWB_BVH_LEAF_FLAG | 1u, { 1.0f, 1.0f, 2.0f }, 1u },
        { { -3.0f, -1.0f, 5.0f }, NWB_BVH_LEAF_FLAG | s_ExpectedDualCount, { -2.0f, 1.0f, 5.0f }, 1u },
        { { 2.0f, -1.0f, 5.0f }, NWB_BVH_LEAF_FLAG | 3u, { 3.0f, 1.0f, 5.0f }, 1u },
    };
    const Float3U positions[] = {
        { -1.0f, -1.0f, 1.0f }, { 1.0f, -1.0f, 1.0f }, { 0.0f, 1.0f, 1.0f },
        { 0.0f, 1.0f, 2.0f }, { 1.0f, -1.0f, 2.0f }, { -1.0f, -1.0f, 2.0f },
        { -3.0f, -1.0f, 5.0f }, { -2.0f, -1.0f, 5.0f }, { -2.5f, 1.0f, 5.0f },
        { 2.0f, -1.0f, 5.0f }, { 3.0f, -1.0f, 5.0f }, { 2.5f, 1.0f, 5.0f },
    };
    const u32 indices[] = { 0u, 1u, s_ExpectedDualCount, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u };
    const Attribute attributes[12]{};
    const Instance instances[2]{};
    Material materials[2];
    materials[1].flags = 0u;
    ContextSlots context;
    const void* const sources[] = { positions, indices, attributes, nodes, instances, materials, &context };
    const usize sizes[] = {
        sizeof(positions), sizeof(indices), sizeof(attributes), sizeof(nodes), sizeof(instances), sizeof(materials), sizeof(context)
    };
    constexpr usize s_InputCount = LengthOf(sources);
    constexpr usize s_ContextIndex = 6u;
    BufferHandle inputs[s_InputCount];
    BufferHandle output;
    GpuDescriptorHandle descriptors[s_InputCount + 1u]{};
    ScopeExit releaseDescriptors([&]()noexcept{
        for(const GpuDescriptorHandle descriptor : descriptors){
            if(descriptor.valid())
                heap.free(descriptor);
        }
        heap.collectRetired();
    });
    for(usize index = 0u; index < s_InputCount; ++index){
        BufferDesc desc;
        desc.setByteSize(sizes[index]).setInitialState(ResourceStates::Common).setKeepInitialState(true);
        if(index == s_ContextIndex)
            desc.setIsConstantBuffer(true);
        else
            desc.setCanHaveRawViews(true);
        inputs[index] = graphicsDevice.createBuffer(desc);
        ASSERT_TRUE(inputs[index]);
        descriptors[index] = heap.allocate(index == s_ContextIndex ? GpuDescriptorClass::UniformBuffer : GpuDescriptorClass::StorageBuffer);
        ASSERT_TRUE(descriptors[index].valid());
        const DescriptorWriteItem item = index == s_ContextIndex
            ? DescriptorWriteItem::ConstantBuffer(0u, inputs[index].get())
            : DescriptorWriteItem::RawBuffer_SRV(0u, inputs[index].get());
        ASSERT_TRUE(heap.write(descriptors[index], item));
    }
    for(Material& material : materials){
        material.positionSlot = descriptors[0].slot();
        material.indexSlot = descriptors[1].slot();
        material.attributeSlot = descriptors[2].slot();
        material.nodeSlot = descriptors[3].slot();
    }
    context.scene[0] = descriptors[3].slot();
    context.scene[1] = descriptors[4].slot();
    context.scene[2] = descriptors[5].slot();
    context.scene[3] = descriptors[0].slot();
    context.material[0] = descriptors[0].slot();
    constexpr usize s_CaseCount = 6u;
    BufferDesc outputDesc;
    outputDesc
        .setByteSize(sizeof(Observation) * s_CaseCount)
        .setCanHaveRawViews(true)
        .setCanHaveUAVs(true)
        .setCpuAccess(CpuAccessMode::Read)
        .setInitialState(ResourceStates::Common)
        .setKeepInitialState(true)
    ;
    output = graphicsDevice.createBuffer(outputDesc);
    ASSERT_TRUE(output);
    descriptors[s_InputCount] = heap.allocate(GpuDescriptorClass::StorageBuffer);
    ASSERT_TRUE(descriptors[s_InputCount].valid());
    ASSERT_TRUE(heap.write(descriptors[s_InputCount], DescriptorWriteItem::RawBuffer_UAV(0u, output.get())));
    const CommandListHandle commandList = graphicsDevice.createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
    for(usize index = 0u; index < s_InputCount; ++index){
        ASSERT_TRUE(commandList->tryWriteBuffer(*inputs[index], sources[index], sizes[index]));
        commandList->setBufferState(
            inputs[index].get(), index == s_ContextIndex ? ResourceStates::ConstantBuffer : ResourceStates::ShaderResource
        );
    }
    Observation sentinels[s_CaseCount];
    NWB_MEMSET(sentinels, 0xa5, sizeof(sentinels));
    ASSERT_TRUE(commandList->tryWriteBuffer(*output, sentinels, sizeof(sentinels)));
    commandList->setBufferState(output.get(), ResourceStates::UnorderedAccess, true);
    commandList->commitBarriers();
    ComputeState state;
    state.setPipeline(pipeline.get());
    commandList->setComputeState(state);
    heap.bindCompute(*commandList, *pipeline);
    const PushConstants push{ descriptors[s_ContextIndex].slot(), descriptors[s_InputCount].slot(), s_ExpectedDualCount };
    commandList->setPushConstants(&push, sizeof(push));
    commandList->dispatch(1u, 1u, 1u);
    ASSERT_FALSE(commandList->commandRecordingFailed());
    commandList->close();
    ASSERT_FALSE(commandList->commandRecordingFailed());
    CommandList* const commandLists[] = { commandList.get() };
    const QueueSubmissionToken token = graphicsDevice.executeCommandLists(
        commandLists, LengthOf(commandLists), commandList->getDescription().physicalQueue, QueueSubmissionDesc{}
    );
    ASSERT_TRUE(token.valid());
    ASSERT_TRUE(graphicsDevice.waitForIdle());
    const Observation* const actual = static_cast<const Observation*>(graphicsDevice.mapBuffer(*output, CpuAccessMode::Read));
    ASSERT_NE(actual, nullptr);
    ScopeExit unmap([&]()noexcept{ graphicsDevice.unmapBuffer(*output); });
    for(usize index = 0u; index < s_CaseCount; ++index){
        SCOPED_TRACE(index);
        const bool paired = index < s_ExpectedDualCount;
        const bool blocked = index == 4u;
        const bool singleton = index == 5u;
        EXPECT_EQ(actual[index].status, paired || singleton ? 0u : blocked ? s_ExpectedDualCount : 1u);
        EXPECT_EQ(actual[index].crossingCount, paired ? s_ExpectedDualCount : singleton ? 1u : 0u);
        EXPECT_EQ(actual[index].overflow, 0u);
        EXPECT_NEAR(actual[index].interfaceTransmittance, paired ? 0.9216f : 1.0f, 0.001f);
        EXPECT_FLOAT_EQ(actual[index].chordLength, paired ? 1.0f : 0.0f);
        // One unit of absorption and two normal-incidence Fresnel boundaries: tint * (1 - 0.04)^2.
        constexpr f32 s_PairedTransmission[] = { 0.4608f, 0.6912f, 0.9216f };
        for(usize channel = 0u; channel < 3u; ++channel){
            const f32 expected = paired ? s_PairedTransmission[channel] : blocked ? 0.0f : 1.0f;
            EXPECT_NEAR(actual[index].transmission[channel], expected, 0.002f);
        }
    }
    for(usize channel = 0u; channel < 3u; ++channel)
        EXPECT_FLOAT_EQ(actual[0].transmission[channel], actual[1].transmission[channel]);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

