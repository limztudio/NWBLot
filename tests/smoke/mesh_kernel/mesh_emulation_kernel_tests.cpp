// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "mesh_kernel_fixture.h"

#include <impl/assets/graphics/mesh/binding_slots.h>
#include <impl/assets/graphics/mesh/runtime_constants.h>
#include <impl/assets_mesh/meshlet_payload_packing.h>
#include <impl/ecs_render/mesh/renderer_mesh_types.h>

#include <global/math/convert.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{
namespace __hidden_mesh_emulation_kernel_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_MeshletCapacity = 2u;
constexpr u32 s_VertexCapacity = NWB_MESH_SHADER_MAX_VERTICES;
constexpr u32 s_PrimitiveCapacity = NWB_MESH_SHADER_MAX_TRIANGLES;
constexpr u32 s_OutputVertexCapacity = s_MeshletCapacity * (s_PrimitiveCapacity * 3u + 3u) + 7u;
constexpr u32 s_PositionCapacity = 2u + s_MeshletCapacity * s_VertexCapacity;
constexpr u32 s_AttributeCapacity = 3u + s_MeshletCapacity * s_VertexCapacity;
constexpr u32 s_ReferenceBytes = 8u + s_MeshletCapacity * s_VertexCapacity * 4u * 4u;

namespace Pose{
    enum Enum : u8{
        Identity,
        Transformed,
        ResolvedDeformed,
    };
};

namespace Cull{
    enum Enum : u8{
        None,
        Frustum,
        Cone,
        Clip,
        Scissor,
        KeptWithFlags,
        FirstMeshletOnly,
    };
};

struct Half4{
    u16 x;
    u16 y;
    u16 z;
    u16 w;
};

// These two narrow upload/readback records describe shader ABI, not vertex or culling implementations.
struct GeneratedVertex{
    f32 position[4];
    Half4 normal;
    Half4 tangent;
    f32 uv0[2];
    Half4 color;
    f32 worldPosition[4];
};

struct ViewData{
    f32 worldToClip[16];
    f32 clipToWorld[16];
    f32 cameraPosition[4];
    f32 frustumPlanes[NWB_MESH_VIEW_FRUSTUM_PLANE_COUNT][4];
};

struct PushConstants{
    u32 dispatch[4];
    f32 viewport[4];
    f32 scissor[4];
    u32 frameHeapSlots[4];
};

struct Inputs{
    Float3U positions[s_PositionCapacity]{};
    Half4 normals[s_AttributeCapacity]{};
    Half4 tangents[s_AttributeCapacity]{};
    Float2U uv0[s_AttributeCapacity]{};
    Half4 colors[s_AttributeCapacity]{};
    Impl::MeshletDesc meshlets[s_MeshletCapacity]{};
    Impl::MeshletBounds bounds[s_MeshletCapacity]{};
    u8 positionRefs[s_ReferenceBytes]{};
    u8 attributeRefs[s_ReferenceBytes]{};
    Impl::MeshletLocalVertexRef localRefs[s_MeshletCapacity * s_VertexCapacity]{};
    u8 primitiveIndices[(s_OutputVertexCapacity + 3u) & ~3u]{};
    Impl::InstanceGpuData instances[2]{};
    ViewData view{};
    PushConstants push{};
};

struct Case{
    u32 vertexCount;
    u32 primitiveCount;
    u32 meshletCount;
    Impl::MeshletRefDeltaWidth::Enum width;
    Pose::Enum pose;
    Cull::Enum cull;
};

static_assert(sizeof(Half4) == 8u);
static_assert(sizeof(GeneratedVertex) == NWB_MESH_EMULATION_VERTEX_BYTE_SIZE);
static_assert(offsetof(GeneratedVertex, normal) == NWB_MESH_EMULATION_VERTEX_NORMAL_BYTE_OFFSET);
static_assert(offsetof(GeneratedVertex, tangent) == NWB_MESH_EMULATION_VERTEX_TANGENT_BYTE_OFFSET);
static_assert(offsetof(GeneratedVertex, uv0) == NWB_MESH_EMULATION_VERTEX_UV0_BYTE_OFFSET);
static_assert(offsetof(GeneratedVertex, color) == NWB_MESH_EMULATION_VERTEX_COLOR_BYTE_OFFSET);
static_assert(offsetof(GeneratedVertex, worldPosition) == NWB_MESH_EMULATION_VERTEX_WORLD_POSITION_BYTE_OFFSET);
static_assert(sizeof(Float3U) == 12u);
static_assert(sizeof(Float2U) == 8u);
static_assert(sizeof(ViewData) == NWB_MESH_VIEW_FLOAT_COUNT * sizeof(f32));
static_assert(sizeof(PushConstants) == NWB_MESH_PUSH_CONSTANT_BYTE_SIZE);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Half4 PackHalf4(const f32 x, const f32 y, const f32 z, const f32 w){
    return { ConvertFloatToHalf(x), ConvertFloatToHalf(y), ConvertFloatToHalf(z), ConvertFloatToHalf(w) };
}

void StoreDelta(u8 (&bytes)[s_ReferenceBytes], u32& offset, const u32 value, const u32 byteWidth){
    for(u32 byte = 0u; byte < byteWidth; ++byte){
        bytes[offset] = static_cast<u8>((value >> (byte * 8u)) & 0xffu);
        ++offset;
    }
}

void BuildInputs(const Case& testCase, Inputs& inputs){
    const u32 positionCount = testCase.vertexCount - 1u;
    const u32 byteWidth = 1u << static_cast<u32>(testCase.width);
    u32 positionOffset = 4u;
    u32 attributeOffset = 4u;
    for(u32 meshletIndex = 0u; meshletIndex < testCase.meshletCount; ++meshletIndex){
        auto& meshlet = inputs.meshlets[meshletIndex];
        meshlet.localVertexOffset = meshletIndex * testCase.vertexCount;
        meshlet.primitiveOffset = 3u + meshletIndex * (testCase.primitiveCount * 3u + 3u);
        meshlet.positionRefOffset = positionOffset;
        meshlet.attributeRefOffset = attributeOffset;
        meshlet.counts = Impl::PackMeshletCounts(
            testCase.vertexCount, testCase.primitiveCount, positionCount, testCase.vertexCount
        );
        meshlet.positionBase = 2u + meshletIndex * positionCount;
        meshlet.normalBase = 3u + meshletIndex * testCase.vertexCount;
        meshlet.tangentBase = meshlet.normalBase;
        meshlet.uv0Base = meshlet.normalBase;
        meshlet.colorBase = meshlet.normalBase;
        meshlet.encoding = Impl::PackMeshletRefEncoding(
            testCase.width, testCase.width, testCase.width, testCase.width, testCase.width, testCase.width
        );
        for(u32 position = 0u; position < positionCount; ++position){
            f32 x = (static_cast<f32>(position % 8u) - 3.0f) * 0.0625f;
            const f32 y = (static_cast<f32>((position / 8u) % 12u) - 5.0f) * 0.03125f;
            const f32 z = testCase.pose == Pose::ResolvedDeformed ? 0.375f + x * 0.25f : 0.5f;
            if(testCase.cull == Cull::Clip || (testCase.cull == Cull::FirstMeshletOnly && meshletIndex == 0u))
                x += 3.0f;
            inputs.positions[meshlet.positionBase + position] = Float3U(x, y, z);
            StoreDelta(inputs.positionRefs, positionOffset, position, byteWidth);
        }
        // The rendering entrypoint reads resolved position streams and deliberately does not read skin references.
        for(u32 position = 0u; position < positionCount; ++position)
            StoreDelta(inputs.positionRefs, positionOffset, 0u, byteWidth);
        for(u32 channel = 0u; channel < 4u; ++channel){
            for(u32 attribute = 0u; attribute < testCase.vertexCount; ++attribute)
                StoreDelta(inputs.attributeRefs, attributeOffset, attribute, byteWidth);
        }
        for(u32 vertex = 0u; vertex < testCase.vertexCount; ++vertex){
            // The last local vertex shares position zero but retains its own normal/tangent/UV/color seam identity.
            inputs.localRefs[meshlet.localVertexOffset + vertex] = {
                static_cast<u16>(vertex % positionCount), static_cast<u16>(vertex)
            };
            const u32 attribute = meshlet.normalBase + vertex;
            const u32 axis = vertex % 4u;
            inputs.normals[attribute] = PackHalf4(axis == 1u ? 1.0f : 0.0f, axis == 2u ? 1.0f : 0.0f, axis == 0u ? 1.0f : 0.0f, 0.0f);
            inputs.tangents[attribute] = PackHalf4(axis == 3u ? 0.0f : 1.0f, 0.0f, 0.0f, vertex % 2u == 0u ? 1.0f : -1.0f);
            inputs.uv0[attribute] = Float2U(static_cast<f32>(vertex) * 0.03125f, static_cast<f32>(meshletIndex) + 0.125f);
            inputs.colors[attribute] = PackHalf4(static_cast<f32>(vertex) * 0.0078125f, 0.25f, 0.5f, 0.75f);
        }
        for(u32 primitive = 0u; primitive < testCase.primitiveCount; ++primitive){
            for(u32 corner = 0u; corner < 3u; ++corner){
                const u32 localVertex = (primitive * 3u + corner) % testCase.vertexCount;
                inputs.primitiveIndices[meshlet.primitiveOffset + primitive * 3u + corner] = static_cast<u8>(localVertex);
            }
        }
        auto& bounds = inputs.bounds[meshletIndex];
        bounds.sphere = Float4U(0.0f, 0.0f, 0.5f, 0.625f);
        if(testCase.cull == Cull::FirstMeshletOnly && meshletIndex == 0u)
            bounds.sphere.x = 3.0f;
        // A narrow, enabled +Z cone faces away from the camera only in the explicit cone-rejection case.
        bounds.conePacked = NWB_MESHLET_CONE_AXIS_FALLBACK | (255u << 16u) | (NWB_MESHLET_CONE_FLAG_ENABLED << 24u);
    }
    for(u32 diagonal = 0u; diagonal < 4u; ++diagonal){
        inputs.view.worldToClip[diagonal * 5u] = 1.0f;
        inputs.view.clipToWorld[diagonal * 5u] = 1.0f;
    }
    inputs.view.cameraPosition[2] = testCase.cull == Cull::Cone ? -10.0f : 10.0f;
    for(auto& plane : inputs.view.frustumPlanes)
        plane[3] = 2.0f;
    inputs.view.frustumPlanes[0][0] = -1.0f;
    inputs.view.frustumPlanes[0][3] = testCase.cull == Cull::Frustum ? -2.0f : 1.0f;
    inputs.instances[0].translation.x = 10.0f;
    if(testCase.pose == Pose::Transformed){
        inputs.instances[1].rotation = Float4(0.0f, 0.38268343f, 0.0f, 0.9238795f);
        inputs.instances[1].translation = Float3UInt(0.0625f, -0.0625f, 0.125f, 0u);
        inputs.instances[1].scale = Float4(0.75f, 1.25f, 0.5f, 0.0f);
    }
    inputs.push.dispatch[0] = testCase.meshletCount;
    inputs.push.dispatch[1] = 1u;
    inputs.push.viewport[2] = 100.0f;
    inputs.push.viewport[3] = 100.0f;
    inputs.push.scissor[2] = testCase.cull == Cull::Scissor ? 1.0f : 100.0f;
    inputs.push.scissor[3] = testCase.cull == Cull::Scissor ? 1.0f : 100.0f;
    if(testCase.cull == Cull::Frustum || testCase.cull == Cull::FirstMeshletOnly)
        inputs.push.dispatch[3] = NWB_MESH_DISPATCH_FLAG_MESHLET_FRUSTUM_CULL;
    else if(testCase.cull == Cull::Cone)
        inputs.push.dispatch[3] = NWB_MESH_DISPATCH_FLAG_MESHLET_CONE_CULL;
    else if(testCase.cull == Cull::Scissor)
        inputs.push.dispatch[3] = NWB_MESH_DISPATCH_FLAG_SCISSOR_CULL;
    else if(testCase.cull == Cull::KeptWithFlags){
        inputs.push.dispatch[3] = NWB_MESH_DISPATCH_FLAG_SCISSOR_CULL
            | NWB_MESH_DISPATCH_FLAG_MESHLET_FRUSTUM_CULL | NWB_MESH_DISPATCH_FLAG_MESHLET_CONE_CULL;
    }
}

[[nodiscard]] GeneratedVertex CulledVertex(){
    GeneratedVertex result{};
    result.position[0] = 2.0f;
    result.position[1] = 2.0f;
    result.position[3] = 1.0f;
    result.normal = PackHalf4(0.0f, 0.0f, 1.0f, 0.0f);
    result.tangent = PackHalf4(1.0f, 0.0f, 0.0f, 1.0f);
    return result;
}

void RunCase(GraphicsBackend::Device& device, ComputePipeline& reference, ComputePipeline& candidate, const Case& testCase){
    SCOPED_TRACE(testCase.vertexCount);
    SCOPED_TRACE(testCase.primitiveCount);
    SCOPED_TRACE(testCase.meshletCount);
    SCOPED_TRACE(static_cast<u32>(testCase.width));
    SCOPED_TRACE(static_cast<u32>(testCase.pose));
    SCOPED_TRACE(static_cast<u32>(testCase.cull));
    Inputs inputs;
    BuildInputs(testCase, inputs);
    auto& heap = device.getDescriptorHeap();
    constexpr u32 instanceBufferIndex = 12u;
    constexpr u32 viewBufferIndex = 13u;
    constexpr u32 bufferCount = 14u;
    const void* const sources[bufferCount] = {
        inputs.positions, inputs.normals, inputs.tangents, inputs.uv0, inputs.colors, inputs.meshlets,
        inputs.positions, inputs.bounds, inputs.positionRefs, inputs.attributeRefs, inputs.localRefs,
        inputs.primitiveIndices, inputs.instances, &inputs.view
    };
    const usize sizes[bufferCount] = {
        sizeof(inputs.positions), sizeof(inputs.normals), sizeof(inputs.tangents), sizeof(inputs.uv0),
        sizeof(inputs.colors), sizeof(inputs.meshlets), sizeof(inputs.positions), sizeof(inputs.bounds),
        sizeof(inputs.positionRefs), sizeof(inputs.attributeRefs), sizeof(inputs.localRefs),
        sizeof(inputs.primitiveIndices), sizeof(inputs.instances), sizeof(inputs.view)
    };
    BufferHandle buffers[bufferCount];
    BufferHandle outputs[2];
    GpuDescriptorHandle descriptors[bufferCount + 2u]{};
    ScopeExit releaseDescriptors([&]()noexcept{
        for(const GpuDescriptorHandle descriptor : descriptors){
            if(descriptor.valid())
                heap.free(descriptor);
        }
        heap.collectRetired();
    });
    for(u32 index = 0u; index < bufferCount; ++index){
        BufferDesc desc;
        desc.setByteSize(sizes[index]).setInitialState(ResourceStates::Common).setKeepInitialState(true);
        if(index == viewBufferIndex)
            desc.setIsConstantBuffer(true);
        else
            desc.setCanHaveRawViews(true);
        buffers[index] = device.createBuffer(desc);
        ASSERT_TRUE(buffers[index]);
        descriptors[index] = heap.allocate(index == viewBufferIndex ? GpuDescriptorClass::UniformBuffer : GpuDescriptorClass::StorageBuffer);
        ASSERT_TRUE(descriptors[index].valid());
        const DescriptorWriteItem item = index == viewBufferIndex
            ? DescriptorWriteItem::ConstantBuffer(0u, buffers[index].get())
            : DescriptorWriteItem::RawBuffer_SRV(0u, buffers[index].get());
        ASSERT_TRUE(heap.write(descriptors[index], item));
    }
    for(auto& instance : inputs.instances){
        for(u32 slot = 0u; slot < NWB_MESH_INSTANCE_GEOMETRY_SLOT_COUNT; ++slot)
            instance.geometryHeapSlots[slot] = descriptors[slot].slot();
    }
    const CommandListHandle commandList = device.createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
    for(u32 index = 0u; index < bufferCount; ++index){
        ASSERT_TRUE(commandList->tryWriteBuffer(*buffers[index], sources[index], sizes[index]));
        commandList->setBufferState(
            buffers[index].get(), index == viewBufferIndex ? ResourceStates::ConstantBuffer : ResourceStates::ShaderResource
        );
    }
    GeneratedVertex sentinels[s_OutputVertexCapacity];
    ComputePipeline* const pipelines[] = { &reference, &candidate };
    for(u32 arm = 0u; arm < 2u; ++arm){
        BufferDesc outputDesc;
        outputDesc
            .setByteSize(sizeof(sentinels))
            .setCanHaveRawViews(true)
            .setCanHaveUAVs(true)
            .setCpuAccess(CpuAccessMode::Read)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
        ;
        outputs[arm] = device.createBuffer(outputDesc);
        ASSERT_TRUE(outputs[arm]);
        descriptors[bufferCount + arm] = heap.allocate(GpuDescriptorClass::StorageBuffer);
        ASSERT_TRUE(descriptors[bufferCount + arm].valid());
        ASSERT_TRUE(heap.write(descriptors[bufferCount + arm], DescriptorWriteItem::RawBuffer_UAV(0u, outputs[arm].get())));
        NWB_MEMSET(sentinels, arm == 0u ? 0x5a : 0xa5, sizeof(sentinels));
        ASSERT_TRUE(commandList->tryWriteBuffer(*outputs[arm], sentinels, sizeof(sentinels)));
        commandList->setBufferState(outputs[arm].get(), ResourceStates::UnorderedAccess, true);
        commandList->commitBarriers();
        ComputeState state;
        state.setPipeline(pipelines[arm]);
        commandList->setComputeState(state);
        heap.bindCompute(*commandList, *pipelines[arm]);
        inputs.push.frameHeapSlots[NWB_MESH_FRAME_HEAP_SLOT_INSTANCE] = descriptors[instanceBufferIndex].slot();
        inputs.push.frameHeapSlots[NWB_MESH_FRAME_HEAP_SLOT_MATERIAL_TYPED] = descriptors[6].slot();
        inputs.push.frameHeapSlots[NWB_MESH_FRAME_HEAP_SLOT_VIEW] = descriptors[viewBufferIndex].slot();
        inputs.push.frameHeapSlots[NWB_MESH_FRAME_HEAP_SLOT_GENERATED_VERTEX] = descriptors[bufferCount + arm].slot();
        commandList->setPushConstants(&inputs.push, sizeof(inputs.push));
        // Two excess workgroups exercise the uniform count guard; partial vertex/primitive lanes remain active cases.
        commandList->dispatch(testCase.meshletCount + 2u, 1u, 1u);
    }
    ASSERT_FALSE(commandList->commandRecordingFailed());
    commandList->close();
    ASSERT_FALSE(commandList->commandRecordingFailed());
    CommandList* const commandLists[] = { commandList.get() };
    const QueueSubmissionToken token = device.executeCommandLists(
        commandLists, LengthOf(commandLists), commandList->getDescription().physicalQueue, QueueSubmissionDesc{}
    );
    ASSERT_TRUE(token.valid());
    ASSERT_TRUE(device.waitForIdle());
    const GeneratedVertex* mapped[2]{};
    ScopeExit unmap([&]()noexcept{
        for(u32 arm = 0u; arm < 2u; ++arm){
            if(mapped[arm] != nullptr)
                device.unmapBuffer(*outputs[arm]);
        }
    });
    for(u32 arm = 0u; arm < 2u; ++arm){
        mapped[arm] = static_cast<const GeneratedVertex*>(device.mapBuffer(*outputs[arm], CpuAccessMode::Read));
        ASSERT_NE(mapped[arm], nullptr);
    }
    bool written[s_OutputVertexCapacity]{};
    const GeneratedVertex culled = CulledVertex();
    for(u32 meshletIndex = 0u; meshletIndex < testCase.meshletCount; ++meshletIndex){
        const auto& meshlet = inputs.meshlets[meshletIndex];
        const bool expectCulled = (testCase.cull >= Cull::Frustum && testCase.cull <= Cull::Scissor)
            || (testCase.cull == Cull::FirstMeshletOnly && meshletIndex == 0u);
        for(u32 cornerIndex = 0u; cornerIndex < testCase.primitiveCount * 3u; ++cornerIndex){
            const u32 outputIndex = meshlet.primitiveOffset + cornerIndex;
            written[outputIndex] = true;
            // Every field, including packed halves and raster flags, must match exactly; no radiometric tolerance.
            EXPECT_EQ(NWB_MEMCMP(&mapped[0][outputIndex], &mapped[1][outputIndex], sizeof(GeneratedVertex)), 0) << outputIndex;
            for(u32 arm = 0u; arm < 2u; ++arm){
                const auto& vertex = mapped[arm][outputIndex];
                if(expectCulled){
                    EXPECT_EQ(NWB_MEMCMP(&vertex, &culled, sizeof(vertex)), 0) << outputIndex;
                    continue;
                }
                const u32 localVertex = inputs.primitiveIndices[outputIndex];
                const u32 attribute = meshlet.normalBase + localVertex;
                EXPECT_EQ(vertex.uv0[0], inputs.uv0[attribute].x);
                EXPECT_EQ(vertex.uv0[1], inputs.uv0[attribute].y);
                EXPECT_EQ(NWB_MEMCMP(&vertex.color, &inputs.colors[attribute], sizeof(Half4)), 0);
                EXPECT_EQ(vertex.position[3], 1.0f);
                EXPECT_EQ(vertex.worldPosition[3], 0.0f);
                for(u32 component = 0u; component < 3u; ++component){
                    EXPECT_TRUE(IsFinite(vertex.position[component]));
                    EXPECT_TRUE(IsFinite(vertex.worldPosition[component]));
                }
                if(testCase.pose != Pose::Transformed){
                    const u32 localPosition = inputs.localRefs[meshlet.localVertexOffset + localVertex].localDeformedPosition;
                    const auto& sourcePosition = inputs.positions[meshlet.positionBase + localPosition];
                    EXPECT_EQ(vertex.position[0], sourcePosition.x);
                    EXPECT_EQ(vertex.position[1], sourcePosition.y);
                    EXPECT_EQ(vertex.position[2], sourcePosition.z);
                    EXPECT_EQ(vertex.worldPosition[0], sourcePosition.x);
                    EXPECT_EQ(vertex.worldPosition[1], sourcePosition.y);
                    EXPECT_EQ(vertex.worldPosition[2], sourcePosition.z);
                    const Half4 expectedNormal = localVertex % 4u == 3u ? PackHalf4(0.0f, 0.0f, 1.0f, 0.0f) : inputs.normals[attribute];
                    Half4 expectedTangent = inputs.tangents[attribute];
                    if(localVertex % 4u == 3u)
                        expectedTangent.z = ConvertFloatToHalf(1.0f);
                    EXPECT_EQ(NWB_MEMCMP(&vertex.normal, &expectedNormal, sizeof(Half4)), 0);
                    EXPECT_EQ(NWB_MEMCMP(&vertex.tangent, &expectedTangent, sizeof(Half4)), 0);
                }
            }
        }
    }
    // Different initial patterns make missing writes fail the paired oracle and distinguish guard preservation.
    for(u32 arm = 0u; arm < 2u; ++arm){
        GeneratedVertex sentinel;
        NWB_MEMSET(&sentinel, arm == 0u ? 0x5a : 0xa5, sizeof(sentinel));
        for(u32 index = 0u; index < s_OutputVertexCapacity; ++index){
            if(!written[index])
                EXPECT_EQ(NWB_MEMCMP(&mapped[arm][index], &sentinel, sizeof(sentinel)), 0) << index;
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(MeshKernelTest, ProductionEntrypointsPreserveEveryVertexByteAcrossSeamsAndResolvedStreams){
    using namespace __hidden_mesh_emulation_kernel_tests;
    Alloc::ScratchArena scratchArena(Name("tests/smoke/mesh_kernel/vertex_parity"));
    ComputePipelineHandle reference;
    ComputePipelineHandle candidate;
    ASSERT_TRUE(loadMeshKernel(false, scratchArena, reference));
    ASSERT_TRUE(loadMeshKernel(true, scratchArena, candidate));
    constexpr u32 shapes[][2] = { { 3u, 1u }, { 6u, 2u }, { 96u, 1u }, { 96u, 126u } };
    for(const auto& shape : shapes){
        for(u32 width = Impl::MeshletRefDeltaWidth::U8; width <= Impl::MeshletRefDeltaWidth::U32; ++width){
            for(u32 pose = Pose::Identity; pose <= Pose::ResolvedDeformed; ++pose){
                for(u32 meshlets = 1u; meshlets <= 2u; ++meshlets){
                    const Case testCase{
                        shape[0], shape[1], meshlets, static_cast<Impl::MeshletRefDeltaWidth::Enum>(width),
                        static_cast<Pose::Enum>(pose), Cull::None
                    };
                    ASSERT_NO_FATAL_FAILURE(RunCase(device(), *reference, *candidate, testCase));
                }
            }
        }
    }
}

TEST_F(MeshKernelTest, ProductionEntrypointsPreserveCullWritesAndUniformGroupGuards){
    using namespace __hidden_mesh_emulation_kernel_tests;
    Alloc::ScratchArena scratchArena(Name("tests/smoke/mesh_kernel/culling"));
    ComputePipelineHandle reference;
    ComputePipelineHandle candidate;
    ASSERT_TRUE(loadMeshKernel(false, scratchArena, reference));
    ASSERT_TRUE(loadMeshKernel(true, scratchArena, candidate));
    for(u32 cull = Cull::Frustum; cull <= Cull::KeptWithFlags; ++cull){
        for(u32 meshlets = 1u; meshlets <= 2u; ++meshlets){
            const Case testCase{
                96u, 126u, meshlets, Impl::MeshletRefDeltaWidth::U8, Pose::Identity, static_cast<Cull::Enum>(cull)
            };
            ASSERT_NO_FATAL_FAILURE(RunCase(device(), *reference, *candidate, testCase));
        }
    }
    const Case mixed{ 96u, 126u, 2u, Impl::MeshletRefDeltaWidth::U16, Pose::Identity, Cull::FirstMeshletOnly };
    ASSERT_NO_FATAL_FAILURE(RunCase(device(), *reference, *candidate, mixed));
    const Case empty{ 3u, 1u, 0u, Impl::MeshletRefDeltaWidth::U8, Pose::Identity, Cull::None };
    ASSERT_NO_FATAL_FAILURE(RunCase(device(), *reference, *candidate, empty));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

