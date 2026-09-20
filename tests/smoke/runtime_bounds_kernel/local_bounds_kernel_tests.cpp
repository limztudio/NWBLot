// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "runtime_bounds_kernel_fixture.h"

#include <impl/assets/graphics/mesh/runtime_bounds_constants.h>
#include <impl/assets/graphics/raytrace/optical_bounds_finalize_constants.h>
#include <impl/assets/graphics/raytrace/optical_scene_constants.h>
#include <impl/assets_mesh/meshlet_payload_packing.h>

#include <global/bit.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_local_bounds_kernel_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct Bounds{
    Float3U minimum;
    u32 flags;
    Float3U maximum;
    u32 reserved;
};

struct StreamSlots{
    u32 values[20]{};
};

struct BoundsPush{
    u32 meshletCount;
    u32 resourcesSlot;
    u32 reserved[2]{};
};

struct OpticalHeader{
    Float3U minimum;
    u32 transparentCount;
    Float3U maximum;
    u32 flags;
};

struct OpticalInstance{
    u32 entity;
    u32 priority;
    u32 boundary;
    u32 flags;
};

struct RuntimeInput{
    Float4U rows[3]{ { 1.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f, 0.0f } };
    u32 boundsSlot;
    u32 instanceIndex = 0u;
    u32 reserved[2]{};
};

struct FinalizePush{
    u32 sourceSlot;
    u32 inputsSlot;
    u32 outputSlot;
    u32 instanceCount;
    u32 runtimeCount;
    u32 staticComplete;
};

namespace Damage{
    enum Enum : u8{
        None,
        NonfinitePosition,
        AllNonfinite,
        EmptyMeshlet,
        ExtremeFinite,
        LargeFinite,
        Subnormal,
    };
};

struct Case{
    AStringView label;
    u32 meshletCount;
    u32 positionCount;
    Damage::Enum damage = Damage::None;
};

static_assert(sizeof(Float3U) == 12u);
static_assert(sizeof(Bounds) == NWB_RUNTIME_MESH_BOUNDS_BYTE_SIZE);
static_assert(offsetof(Bounds, flags) == NWB_RUNTIME_MESH_BOUNDS_FLAGS_OFFSET);
static_assert(offsetof(Bounds, maximum) == NWB_RUNTIME_MESH_BOUNDS_MAX_OFFSET);
static_assert(offsetof(Bounds, reserved) == NWB_RUNTIME_MESH_BOUNDS_RESERVED_OFFSET);
static_assert(sizeof(StreamSlots) == 80u);
static_assert(sizeof(BoundsPush) == NWB_SKINNED_MESH_BOUNDS_PUSH_CONSTANT_BYTE_SIZE);
static_assert(sizeof(RuntimeInput) == NWB_OPTICAL_BOUNDS_RUNTIME_INPUT_BYTES);
static_assert(offsetof(RuntimeInput, boundsSlot) == NWB_OPTICAL_BOUNDS_RUNTIME_SLOT_OFFSET);
static_assert(offsetof(RuntimeInput, instanceIndex) == NWB_OPTICAL_BOUNDS_RUNTIME_INSTANCE_OFFSET);
static_assert(sizeof(FinalizePush) == NWB_OPTICAL_BOUNDS_FINALIZE_PUSH_BYTES);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RunCase(
    GraphicsBackend::Device& device,
    ComputePipeline& partialPipeline,
    ComputePipeline& reducePipeline,
    ComputePipeline& unionPipeline,
    const Case& testCase,
    Alloc::ScratchArena& scratchArena){
    SCOPED_TRACE(testCase.label.data());
    auto& heap = device.getDescriptorHeap();
    const usize allocatedMeshlets = Max(testCase.meshletCount, 1u);
    const usize positionCapacity = allocatedMeshlets * testCase.positionCount + 2u;
    Vector<Float3U, Alloc::ScratchArena> positions(positionCapacity, scratchArena);
    Vector<Impl::MeshletDesc, Alloc::ScratchArena> meshlets(allocatedMeshlets, scratchArena);
    const usize referenceBytes = AlignUp(allocatedMeshlets * testCase.positionCount * 2u + 4u, usize(4u));
    Vector<u8, Alloc::ScratchArena> references(referenceBytes, scratchArena);
    const u32 primitiveCount = testCase.positionCount - 2u;
    const usize localVertexCount = allocatedMeshlets * testCase.positionCount;
    Vector<Impl::MeshletLocalVertexRef, Alloc::ScratchArena> localReferences(localVertexCount, scratchArena);
    const usize primitiveBytes = AlignUp(allocatedMeshlets * primitiveCount * 3u, usize(4u));
    Vector<u8, Alloc::ScratchArena> primitives(primitiveBytes, scratchArena);
    Vector<Bounds, Alloc::ScratchArena> expected(allocatedMeshlets, scratchArena);
    const Float3U largest(Limit<f32>::s_Max, Limit<f32>::s_Max, Limit<f32>::s_Max);
    const Float3U smallest(-Limit<f32>::s_Max, -Limit<f32>::s_Max, -Limit<f32>::s_Max);
    Bounds expectedUnion{ largest, NWB_RUNTIME_MESH_BOUNDS_FLAG_VALID, smallest, 0u };
    positions[0] = Float3U(-9999.0f, -9999.0f, -9999.0f);
    positions.back() = Float3U(9999.0f, 9999.0f, 9999.0f);
    for(u32 index = 0u; index < testCase.meshletCount; ++index){
        auto& meshlet = meshlets[index];
        meshlet.localVertexOffset = index * testCase.positionCount;
        meshlet.primitiveOffset = index * primitiveCount * 3u;
        meshlet.positionRefOffset = 4u + index * testCase.positionCount * 2u;
        meshlet.positionBase = 1u + index * testCase.positionCount;
        meshlet.skinBase = 0u;
        const bool empty = testCase.damage == Damage::EmptyMeshlet && index == testCase.meshletCount / 2u;
        meshlet.counts = Impl::PackMeshletCounts(
            testCase.positionCount, empty ? 0u : primitiveCount, empty ? 0u : testCase.positionCount, 0u
        );
        expected[index] = Bounds{ largest, empty ? 0u : NWB_RUNTIME_MESH_BOUNDS_FLAG_VALID, smallest, 0u };
        for(u32 vertex = 0u; vertex < testCase.positionCount; ++vertex)
            localReferences[meshlet.localVertexOffset + vertex] = { static_cast<u16>(vertex), 0u };
        for(u32 primitive = 0u; primitive < primitiveCount; ++primitive){
            primitives[meshlet.primitiveOffset + primitive * 3u] = 0u;
            primitives[meshlet.primitiveOffset + primitive * 3u + 1u] = static_cast<u8>(primitive + 1u);
            primitives[meshlet.primitiveOffset + primitive * 3u + 2u] = static_cast<u8>(primitive + 2u);
        }
        for(u32 position = 0u; position < testCase.positionCount; ++position){
            // These are resolved current-pose positions, well outside the deliberately unrelated [-1,1] bind bounds.
            Float3U value(
                32.0f + static_cast<f32>(index) * 2.0f + static_cast<f32>(position % 7u),
                -16.0f - static_cast<f32>(position % 11u),
                8.0f + static_cast<f32>(position % 13u)
            );
            if(testCase.damage == Damage::ExtremeFinite)
                value = Float3U(Limit<f32>::s_Max, -Limit<f32>::s_Max, position == 0u ? -1.0f : 1.0f);
            if(testCase.damage == Damage::LargeFinite)
                value = Float3U(1e37f, -1e37f, position == 0u ? -1e37f : 1e37f);
            if(testCase.damage == Damage::Subnormal){
                const f32 tiny = BitCast<f32>(position == 0u ? 1u : 0x007fffffu);
                value = Float3U(tiny, -tiny, 0.0f);
            }
            const bool damaged = testCase.damage == Damage::AllNonfinite
                || (testCase.damage == Damage::NonfinitePosition && index == testCase.meshletCount - 1u
                    && position == testCase.positionCount - 1u);
            if(damaged){
                value.y = Limit<f32>::s_QuietNaN;
                expected[index].flags = 0u;
            }
            positions[meshlet.positionBase + position] = value;
            references[meshlet.positionRefOffset + position] = static_cast<u8>(position);
            if(!damaged && !empty){
                expected[index].minimum.x = Min(expected[index].minimum.x, value.x);
                expected[index].minimum.y = Min(expected[index].minimum.y, value.y);
                expected[index].minimum.z = Min(expected[index].minimum.z, value.z);
                expected[index].maximum.x = Max(expected[index].maximum.x, value.x);
                expected[index].maximum.y = Max(expected[index].maximum.y, value.y);
                expected[index].maximum.z = Max(expected[index].maximum.z, value.z);
            }
        }
        expectedUnion.flags &= expected[index].flags;
        expectedUnion.minimum.x = Min(expectedUnion.minimum.x, expected[index].minimum.x);
        expectedUnion.minimum.y = Min(expectedUnion.minimum.y, expected[index].minimum.y);
        expectedUnion.minimum.z = Min(expectedUnion.minimum.z, expected[index].minimum.z);
        expectedUnion.maximum.x = Max(expectedUnion.maximum.x, expected[index].maximum.x);
        expectedUnion.maximum.y = Max(expectedUnion.maximum.y, expected[index].maximum.y);
        expectedUnion.maximum.z = Max(expectedUnion.maximum.z, expected[index].maximum.z);
    }
    if(testCase.meshletCount == 0u || testCase.damage == Damage::ExtremeFinite)
        expectedUnion.flags = 0u;
    constexpr u32 guard = 0x5a5a5a5au;
    const usize sphereWordCount = (allocatedMeshlets + 2u) * sizeof(Impl::MeshletBounds) / sizeof(u32);
    Vector<u32, Alloc::ScratchArena> sphereWords(sphereWordCount, guard, scratchArena);
    Vector<u32, Alloc::ScratchArena> partialWords((allocatedMeshlets + 2u) * sizeof(Bounds) / sizeof(u32), guard, scratchArena);
    u32 localWords[sizeof(Bounds) / sizeof(u32) + 8u];
    u32 opticalWords[(sizeof(OpticalHeader) + sizeof(OpticalInstance)) / sizeof(u32) + 8u];
    for(u32& word : localWords)
        word = guard;
    for(u32& word : opticalWords)
        word = guard;
    struct Scene{
        OpticalHeader header;
        OpticalInstance instance;
    };
    const Scene sourceScene{
        { Float3U(-1.0f, -1.0f, -1.0f), 1u, Float3U(1.0f, 1.0f, 1.0f), 0u },
        { 17u, 3u, NWB_RT_OPTICAL_BOUNDARY_CLOSED_NESTED, NWB_RT_OPTICAL_INSTANCE_FLAG_TRANSPARENT }
    };
    StreamSlots slots;
    RuntimeInput runtimeInput;
    const void* data[] = { positions.data(), meshlets.data(), references.data(), localReferences.data(), primitives.data(),
        sphereWords.data(), partialWords.data(), localWords, &slots, &sourceScene, &runtimeInput, opticalWords };
    const usize sizes[] = { positions.size() * sizeof(Float3U), meshlets.size() * sizeof(Impl::MeshletDesc), references.size(),
        localReferences.size() * sizeof(Impl::MeshletLocalVertexRef), primitives.size(), sphereWords.size() * sizeof(u32),
        partialWords.size() * sizeof(u32), sizeof(localWords), sizeof(slots), sizeof(sourceScene), sizeof(runtimeInput),
        sizeof(opticalWords) };
    BufferHandle buffers[LengthOf(data)];
    GpuDescriptorHandle descriptors[LengthOf(data)]{};
    ScopeExit releaseDescriptors([&]()noexcept{
        for(const GpuDescriptorHandle descriptor : descriptors){
            if(descriptor.valid())
                heap.free(descriptor);
        }
        heap.collectRetired();
    });
    for(usize index = 0u; index < LengthOf(buffers); ++index){
        const bool output = (index >= 5u && index <= 7u) || index == 11u;
        const bool uniform = index == 8u;
        BufferDesc desc;
        desc
            .setByteSize(sizes[index]).setCanHaveRawViews(!uniform).setIsConstantBuffer(uniform)
            .setCanHaveUAVs(output).setInitialState(ResourceStates::Common).setKeepInitialState(true)
        ;
        if(output)
            desc.setCpuAccess(CpuAccessMode::Read);
        buffers[index] = device.createBuffer(desc);
        ASSERT_TRUE(buffers[index]);
        descriptors[index] = heap.allocate(uniform ? GpuDescriptorClass::UniformBuffer : GpuDescriptorClass::StorageBuffer);
        ASSERT_TRUE(descriptors[index].valid());
        const DescriptorWriteItem rawWrite = output ? DescriptorWriteItem::RawBuffer_UAV(0u, buffers[index].get())
            : DescriptorWriteItem::RawBuffer_SRV(0u, buffers[index].get());
        const DescriptorWriteItem write = uniform ? DescriptorWriteItem::ConstantBuffer(0u, buffers[index].get()) : rawWrite;
        ASSERT_TRUE(heap.write(descriptors[index], write));
    }
    slots.values[1] = descriptors[0].slot();
    slots.values[6] = descriptors[1].slot();
    slots.values[7] = descriptors[2].slot();
    slots.values[12] = descriptors[3].slot();
    slots.values[13] = descriptors[4].slot();
    slots.values[14] = descriptors[5].slot();
    slots.values[16] = descriptors[6].slot();
    slots.values[17] = descriptors[7].slot();
    runtimeInput.boundsSlot = descriptors[7].slot();
    const CommandListHandle commandList = device.createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
    for(usize index = 0u; index < LengthOf(buffers); ++index){
        ASSERT_TRUE(commandList->tryWriteBuffer(*buffers[index], data[index], sizes[index]));
        const bool output = (index >= 5u && index <= 7u) || index == 11u;
        commandList->setBufferState(buffers[index].get(), index == 8u ? ResourceStates::ConstantBuffer
            : (output ? ResourceStates::UnorderedAccess : ResourceStates::ShaderResource));
    }
    commandList->commitBarriers();
    ComputeState state;
    state.setPipeline(&partialPipeline);
    commandList->setComputeState(state);
    heap.bindCompute(*commandList, partialPipeline);
    const BoundsPush push{ testCase.meshletCount, descriptors[8].slot() };
    commandList->setPushConstants(&push, sizeof(push));
    commandList->dispatch(testCase.meshletCount + 2u, 1u, 1u);
    commandList->setBufferState(buffers[6].get(), ResourceStates::ShaderResource);
    commandList->commitBarriers();
    state.setPipeline(&reducePipeline);
    commandList->setComputeState(state);
    heap.bindCompute(*commandList, reducePipeline);
    commandList->setPushConstants(&push, sizeof(push));
    commandList->dispatch(1u, 1u, 1u);
    commandList->setBufferState(buffers[7].get(), ResourceStates::ShaderResource);
    commandList->commitBarriers();
    state.setPipeline(&unionPipeline);
    commandList->setComputeState(state);
    heap.bindCompute(*commandList, unionPipeline);
    const FinalizePush finalize{ descriptors[9].slot(), descriptors[10].slot(), descriptors[11].slot(), 1u, 1u, 1u };
    commandList->setPushConstants(&finalize, sizeof(finalize));
    commandList->dispatch(1u, 1u, 1u);
    commandList->close();
    ASSERT_FALSE(commandList->commandRecordingFailed());
    CommandList* const submitted[] = { commandList.get() };
    const QueueSubmissionToken token = device.executeCommandLists(
        submitted, LengthOf(submitted), commandList->getDescription().physicalQueue, QueueSubmissionDesc{}
    );
    ASSERT_TRUE(token.valid());
    ASSERT_TRUE(device.waitForIdle());
    for(const usize index : { 5u, 6u, 7u, 11u }){
        const u32* const mapped = static_cast<const u32*>(device.mapBuffer(*buffers[index], CpuAccessMode::Read));
        ASSERT_NE(mapped, nullptr);
        ScopeExit unmap([&]()noexcept{ device.unmapBuffer(*buffers[index]); });
        const usize usedWords = index == 5u ? testCase.meshletCount * sizeof(Impl::MeshletBounds) / sizeof(u32)
            : (index == 6u ? testCase.meshletCount * sizeof(Bounds) / sizeof(u32)
                : (index == 7u ? sizeof(Bounds) / sizeof(u32) : sizeof(Scene) / sizeof(u32)));
        for(usize word = usedWords; word < sizes[index] / sizeof(u32); ++word)
            EXPECT_EQ(mapped[word], guard) << "guard buffer " << index << " word " << word;
        if(index == 6u || index == 7u){
            const Bounds* const actual = reinterpret_cast<const Bounds*>(mapped);
            const usize count = index == 6u ? testCase.meshletCount : 1u;
            for(usize bound = 0u; bound < count; ++bound){
                const Bounds& target = index == 6u ? expected[bound] : expectedUnion;
                EXPECT_EQ(actual[bound].flags, target.flags) << "bound " << bound;
                EXPECT_EQ(actual[bound].reserved, 0u);
                if(target.flags != 0u){
                    for(u32 axis = 0u; axis < 3u; ++axis){
                        const f64 lower = target.minimum.raw[axis];
                        const f64 upper = target.maximum.raw[axis];
                        if(index == 6u){
                            const f64 tolerance = testCase.damage == Damage::Subnormal ? 1.1754943508222875e-38 : 0.0;
                            EXPECT_NEAR(actual[bound].minimum.raw[axis], lower, tolerance);
                            EXPECT_NEAR(actual[bound].maximum.raw[axis], upper, tolerance);
                        }
                        else{
                            // Enclose actual positions, allowing only the declared outward FP32/subnormal uncertainty.
                            const f64 roundoff = 8.0 * 1.1920928955078125e-7 * Max(Abs(lower), Abs(upper));
                            const f64 padding = Max(roundoff, 2.350988701644575e-38);
                            EXPECT_LE(actual[bound].minimum.raw[axis], lower);
                            EXPECT_GE(actual[bound].maximum.raw[axis], upper);
                            EXPECT_GE(actual[bound].minimum.raw[axis], lower - padding);
                            EXPECT_LE(actual[bound].maximum.raw[axis], upper + padding);
                        }
                    }
                }
            }
        }
        if(index == 11u){
            const Scene* const actual = reinterpret_cast<const Scene*>(mapped);
            const bool valid = expectedUnion.flags != 0u;
            EXPECT_EQ(actual->header.flags, valid ? NWB_RT_OPTICAL_SCENE_FLAG_BOUNDS_VALID : 0u);
            EXPECT_EQ(actual->header.transparentCount, 1u);
            EXPECT_EQ(actual->instance.entity, sourceScene.instance.entity);
            EXPECT_EQ(actual->instance.priority, sourceScene.instance.priority);
            EXPECT_EQ(actual->instance.boundary, sourceScene.instance.boundary);
            EXPECT_EQ(actual->instance.flags, NWB_RT_OPTICAL_INSTANCE_FLAG_TRANSPARENT
                | (valid ? NWB_RT_OPTICAL_INSTANCE_FLAG_BOUNDS_VALID : 0u));
            if(valid){
                EXPECT_LE(actual->header.minimum.x, expectedUnion.minimum.x);
                EXPECT_LE(actual->header.minimum.y, expectedUnion.minimum.y);
                EXPECT_LE(actual->header.minimum.z, expectedUnion.minimum.z);
                EXPECT_GE(actual->header.maximum.x, expectedUnion.maximum.x);
                EXPECT_GE(actual->header.maximum.y, expectedUnion.maximum.y);
                EXPECT_GE(actual->header.maximum.z, expectedUnion.maximum.z);
                if(testCase.damage != Damage::Subnormal)
                    EXPECT_GT(actual->header.minimum.x, 1.0f) << "bind-pose bounds cannot replace the current pose";
            }
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(RuntimeBoundsKernelTest, CurrentPoseMeshletBoundsEnclosePositionsAndPoisonIncompleteOpticalUnions){
    using namespace __hidden_local_bounds_kernel_tests;
    Alloc::ScratchArena scratchArena(Name("tests/smoke/runtime_bounds_kernel/local"));
    ComputePipelineHandle partial;
    ComputePipelineHandle reduce;
    ComputePipelineHandle finalize;
    ASSERT_TRUE(loadKernel("skinned_mesh", "meshlet_bounds_cs", sizeof(BoundsPush), scratchArena, partial));
    ASSERT_TRUE(loadKernel("skinned_mesh", "local_bounds_cs", sizeof(BoundsPush), scratchArena, reduce));
    ASSERT_TRUE(loadKernel("raytrace", "optical_bounds_finalize_cs", sizeof(FinalizePush), scratchArena, finalize));
    const Case cases[] = {
        { "triangle outside bind pose", 1u, 3u },
        { "partial workgroup extrema", 3u, 95u },
        { "maximum meshlet vertices", 2u, 96u },
        { "strided meshlet reduction", 129u, 3u },
        { "one invalid final position poisons union", 3u, 95u, Damage::NonfinitePosition },
        { "all positions invalid", 1u, 3u, Damage::AllNonfinite },
        { "empty partial poisons union", 3u, 3u, Damage::EmptyMeshlet },
        { "empty reduction invalidates stale output", 0u, 3u },
        { "finite extrema reject unrepresentable outward padding", 1u, 3u, Damage::ExtremeFinite },
        { "large finite bounds remain conservative", 1u, 3u, Damage::LargeFinite },
        { "subnormal positions survive conservative proof", 1u, 3u, Damage::Subnormal },
    };
    for(const Case& testCase : cases)
        RunCase(device(), *partial, *reduce, *finalize, testCase, scratchArena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

