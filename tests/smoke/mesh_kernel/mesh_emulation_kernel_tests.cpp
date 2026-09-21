// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "object_geometry_kernel_fixture.h"

#include <impl/assets/graphics/mesh/object_geometry_constants.h>

#include <impl/assets/graphics/mesh/binding_slots.h>
#include <impl/assets/graphics/mesh/runtime_constants.h>
#include <impl/assets_mesh/meshlet_payload_packing.h>
#include <impl/ecs_render/mesh/renderer_mesh_types.h>

#include <global/algorithm.h>
#include <global/math/convert.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_mesh_emulation_kernel_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_MeshletCapacity = s_ExpectedDualCount;
constexpr u32 s_VertexCapacity = NWB_MESH_SHADER_MAX_VERTICES;
constexpr u32 s_PrimitiveCapacity = NWB_MESH_SHADER_MAX_TRIANGLES;
constexpr u32 s_OutputVertexCapacity = s_MeshletCapacity * (s_PrimitiveCapacity * 3u + 3u) + 7u;
constexpr u32 s_CompactVertexCapacity = NWB_MESH_EMULATION_FIRST_VERTEX_INDEX + s_MeshletCapacity * s_VertexCapacity;
constexpr u32 s_IndexByteStride = sizeof(u32);
constexpr u32 s_OutputIndexBytes = s_OutputVertexCapacity * s_IndexByteStride;
constexpr u32 s_IndexedOutputBytes = s_CompactVertexCapacity * NWB_MESH_EMULATION_VERTEX_BYTE_SIZE + s_OutputIndexBytes;
constexpr u32 s_IndexedOutputByteCapacity = AlignUp(s_IndexedOutputBytes, NWB_MESH_EMULATION_VERTEX_BYTE_SIZE);
constexpr u32 s_PositionCapacity = s_ExpectedDualCount + s_MeshletCapacity * s_VertexCapacity;
constexpr u32 s_AttributeCapacity = 3u + s_MeshletCapacity * s_VertexCapacity;
constexpr u32 s_ReferenceBytes = 8u + s_MeshletCapacity * s_VertexCapacity * 4u * 4u;

namespace Pose{
    enum Enum : u8{
        Identity,
        Transformed,
        ResolvedDeformed,
        Mirrored,
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
        AuthoredClip,
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

struct ObjectVertex{
    f32 position[4];
    Half4 normal;
    Half4 tangent;
    f32 uv0[2];
    Half4 color;
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

struct ComputePushConstants{
    PushConstants mesh;
    u32 emulationOutput[4];
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
    bool reverseWinding = false;
};

static_assert(sizeof(ObjectVertex) == NWB_MESH_OBJECT_VERTEX_BYTE_SIZE);
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
static_assert(sizeof(ComputePushConstants) == NWB_MESH_COMPUTE_PUSH_CONSTANT_BYTE_SIZE);
static_assert(offsetof(ComputePushConstants, emulationOutput) == NWB_MESH_COMPUTE_INDEX_BYTE_OFFSET);
static_assert(s_IndexedOutputByteCapacity <= s_OutputVertexCapacity * sizeof(GeneratedVertex));


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
        meshlet.positionBase = s_ExpectedDualCount + meshletIndex * positionCount;
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
            inputs.normals[attribute] = PackHalf4(axis == 1u ? 1.0f : 0.0f, axis == s_ExpectedDualCount ? 1.0f : 0.0f, axis == 0u ? 1.0f : 0.0f, 0.0f);
            inputs.tangents[attribute] = PackHalf4(axis == 3u ? 0.0f : 1.0f, 0.0f, 0.0f, vertex % s_ExpectedDualCount == 0u ? 1.0f : -1.0f);
            inputs.uv0[attribute] = Float2U(static_cast<f32>(vertex) * 0.03125f, static_cast<f32>(meshletIndex) + 0.125f);
            inputs.colors[attribute] = PackHalf4(static_cast<f32>(vertex) * 0.0078125f, 0.25f, 0.5f, 0.75f);
        }
        for(u32 primitive = 0u; primitive < testCase.primitiveCount; ++primitive){
            for(u32 corner = 0u; corner < 3u; ++corner){
                const u32 sourceCorner = testCase.reverseWinding && corner != 0u ? 3u - corner : corner;
                const u32 localVertex = (primitive * 3u + sourceCorner) % testCase.vertexCount;
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
    if(testCase.pose == Pose::Transformed || testCase.pose == Pose::Mirrored){
        inputs.instances[1].rotation = Float4(0.0f, 0.38268343f, 0.0f, 0.9238795f);
        inputs.instances[1].translation = Float3UInt(0.0625f, -0.0625f, 0.125f, 0u);
        inputs.instances[1].scale = Float4(testCase.pose == Pose::Mirrored ? -0.75f : 0.75f, 1.25f, 0.5f, 0.0f);
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

void RunCase(GraphicsBackend::Device& device, ComputePipeline& reference, ComputePipeline& candidate, const Case& testCase, const ObjectGeometryKernels* objectKernels = nullptr){
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
    BufferHandle objectCache;
    GpuDescriptorHandle descriptors[bufferCount + 3u]{};
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
    const u32 compactVertexCount = NWB_MESH_EMULATION_FIRST_VERTEX_INDEX + testCase.meshletCount * testCase.vertexCount;
    const u32 indexByteOffset = compactVertexCount * NWB_MESH_EMULATION_VERTEX_BYTE_SIZE;
    const u32 indexedByteSize = AlignUp(indexByteOffset + s_OutputIndexBytes, NWB_MESH_EMULATION_VERTEX_BYTE_SIZE);
    ASSERT_LE(indexedByteSize, s_IndexedOutputByteCapacity);
    const u32 cacheIndexByteOffset = compactVertexCount * NWB_MESH_OBJECT_VERTEX_BYTE_SIZE;
    const u32 cacheByteSize = AlignUp(
        cacheIndexByteOffset + s_OutputIndexBytes + s_ExpectedDualCount * NWB_MESH_OBJECT_VERTEX_BYTE_SIZE, NWB_MESH_OBJECT_VERTEX_BYTE_SIZE
    );
    if(objectKernels != nullptr){
        BufferDesc desc;
        desc.setByteSize(cacheByteSize).setStructStride(NWB_MESH_OBJECT_VERTEX_BYTE_SIZE).setIsVertexBuffer(true).setIsIndexBuffer(true)
            .setCanHaveRawViews(true).setCanHaveUAVs(true).setCpuAccess(CpuAccessMode::Read)
            .setInitialState(ResourceStates::Common).setKeepInitialState(true);
        objectCache = device.createBuffer(desc);
        ASSERT_TRUE(objectCache);
        descriptors[bufferCount + s_ExpectedDualCount] = heap.allocate(GpuDescriptorClass::StorageBuffer);
        ASSERT_TRUE(descriptors[bufferCount + s_ExpectedDualCount].valid());
        ASSERT_TRUE(heap.write(descriptors[bufferCount + s_ExpectedDualCount], DescriptorWriteItem::RawBuffer_UAV(0u, objectCache.get())));
        NWB_MEMSET(sentinels, 0x39, sizeof(sentinels));
        ASSERT_LE(cacheByteSize, sizeof(sentinels));
        ASSERT_TRUE(commandList->tryWriteBuffer(*objectCache, sentinels, cacheByteSize));
    }
    const usize outputByteSizes[] = { sizeof(sentinels), indexedByteSize };
    ComputePipeline* const pipelines[] = { &reference, &candidate };
    for(u32 arm = 0u; arm < s_ExpectedDualCount; ++arm){
        BufferDesc outputDesc;
        outputDesc
            .setByteSize(outputByteSizes[arm])
            .setStructStride(NWB_MESH_EMULATION_VERTEX_BYTE_SIZE)
            .setIsVertexBuffer(true)
            .setIsIndexBuffer(arm == 1u)
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
        ASSERT_TRUE(commandList->tryWriteBuffer(*outputs[arm], sentinels, outputByteSizes[arm]));
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
        if(arm == 1u && objectKernels != nullptr){
            // Decode under a different pose and view, then retain those bytes for the current draw's transforms.
            const Impl::InstanceGpuData currentInstance = inputs.instances[1];
            const ViewData currentView = inputs.view;
            inputs.instances[1].translation.x += 64.0f;
            inputs.instances[1].scale = Float4(-2.0f, 3.0f, 0.25f, 0.0f);
            inputs.view.worldToClip[0] = 17.0f;
            ASSERT_TRUE(commandList->tryWriteBuffer(*buffers[instanceBufferIndex], inputs.instances, sizeof(inputs.instances)));
            ASSERT_TRUE(commandList->tryWriteBuffer(*buffers[viewBufferIndex], &inputs.view, sizeof(inputs.view)));
            commandList->setBufferState(buffers[instanceBufferIndex].get(), ResourceStates::ShaderResource);
            commandList->setBufferState(buffers[viewBufferIndex].get(), ResourceStates::ConstantBuffer);
            commandList->setBufferState(objectCache.get(), ResourceStates::UnorderedAccess, true);
            commandList->commitBarriers();
            ComputeState decodeState;
            decodeState.setPipeline(objectKernels->decode.get());
            commandList->setComputeState(decodeState);
            heap.bindCompute(*commandList, *objectKernels->decode);
            ComputePushConstants decodePush{ inputs.push, { cacheIndexByteOffset, 0u, 0u, 0u } };
            decodePush.mesh.frameHeapSlots[NWB_MESH_FRAME_HEAP_SLOT_GENERATED_VERTEX] = descriptors[bufferCount + s_ExpectedDualCount].slot();
            commandList->setPushConstants(&decodePush, sizeof(decodePush));
            commandList->dispatch(testCase.meshletCount + s_ExpectedDualCount, 1u, 1u);
            commandList->setBufferState(objectCache.get(), ResourceStates::ShaderResource);
            inputs.instances[1] = currentInstance;
            inputs.view = currentView;
            ASSERT_TRUE(commandList->tryWriteBuffer(*buffers[instanceBufferIndex], inputs.instances, sizeof(inputs.instances)));
            ASSERT_TRUE(commandList->tryWriteBuffer(*buffers[viewBufferIndex], &inputs.view, sizeof(inputs.view)));
            commandList->setBufferState(buffers[instanceBufferIndex].get(), ResourceStates::ShaderResource);
            commandList->setBufferState(buffers[viewBufferIndex].get(), ResourceStates::ConstantBuffer);
            commandList->commitBarriers();
            commandList->setComputeState(state);
            heap.bindCompute(*commandList, *pipelines[arm]);
        }
        if(arm == 0u)
            commandList->setPushConstants(&inputs.push, sizeof(inputs.push));
        else{
            const ComputePushConstants computePush{ inputs.push, { indexByteOffset, 0u, 0u, 0u } };
            commandList->setPushConstants(&computePush, sizeof(computePush));
        }
        // Two excess workgroups exercise the uniform count guard; partial vertex/primitive lanes remain active cases.
        commandList->dispatch(testCase.meshletCount + s_ExpectedDualCount, 1u, 1u);
        if(arm == 1u && objectKernels != nullptr){
            commandList->setBufferState(outputs[arm].get(), ResourceStates::UnorderedAccess, true);
            commandList->commitBarriers();
            ComputeState transformState;
            transformState.setPipeline(objectKernels->transform.get());
            commandList->setComputeState(transformState);
            heap.bindCompute(*commandList, *objectKernels->transform);
            ComputePushConstants transformPush{ inputs.push, { indexByteOffset, 0u, 0u, 0u } };
            transformPush.mesh.dispatch[2] = compactVertexCount;
            transformPush.mesh.frameHeapSlots[NWB_MESH_FRAME_HEAP_SLOT_MATERIAL_TYPED] = descriptors[bufferCount + s_ExpectedDualCount].slot();
            commandList->setPushConstants(&transformPush, sizeof(transformPush));
            commandList->dispatch((compactVertexCount + 63u) / 64u + 1u, 1u, 1u);
        }
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
    const ObjectVertex* mappedCache = nullptr;
    ScopeExit unmap([&]()noexcept{
        if(mappedCache != nullptr)
            device.unmapBuffer(*objectCache);
        for(u32 arm = 0u; arm < s_ExpectedDualCount; ++arm){
            if(mapped[arm] != nullptr)
                device.unmapBuffer(*outputs[arm]);
        }
    });
    for(u32 arm = 0u; arm < s_ExpectedDualCount; ++arm){
        mapped[arm] = static_cast<const GeneratedVertex*>(device.mapBuffer(*outputs[arm], CpuAccessMode::Read));
        ASSERT_NE(mapped[arm], nullptr);
    }
    if(objectKernels != nullptr){
        mappedCache = static_cast<const ObjectVertex*>(device.mapBuffer(*objectCache, CpuAccessMode::Read));
        ASSERT_NE(mappedCache, nullptr);
        const ObjectVertex sentinel{};
        EXPECT_EQ(NWB_MEMCMP(&mappedCache[0], &sentinel, sizeof(sentinel)), 0);
        for(u32 meshletIndex = 0u; meshletIndex < testCase.meshletCount; ++meshletIndex){
            const auto& meshlet = inputs.meshlets[meshletIndex];
            for(u32 localIndex = 0u; localIndex < testCase.vertexCount; ++localIndex){
                const u32 outputIndex = NWB_MESH_OBJECT_FIRST_VERTEX_INDEX + meshlet.localVertexOffset + localIndex;
                const auto& local = inputs.localRefs[meshlet.localVertexOffset + localIndex];
                const auto& position = inputs.positions[meshlet.positionBase + local.localDeformedPosition];
                const u32 attribute = meshlet.normalBase + local.localAttribute;
                ObjectVertex expected{
                    { position.x, position.y, position.z, 1.0f }, inputs.normals[attribute], inputs.tangents[attribute],
                    { inputs.uv0[attribute].x, inputs.uv0[attribute].y }, inputs.colors[attribute]
                };
                EXPECT_EQ(NWB_MEMCMP(&mappedCache[outputIndex], &expected, sizeof(expected)), 0) << outputIndex;
            }
        }
        const u8* const cacheBytes = reinterpret_cast<const u8*>(mappedCache);
        const u32* const persistentIndices = reinterpret_cast<const u32*>(cacheBytes + cacheIndexByteOffset);
        bool indexWritten[s_OutputVertexCapacity]{};
        for(u32 meshletIndex = 0u; meshletIndex < testCase.meshletCount; ++meshletIndex){
            const auto& meshlet = inputs.meshlets[meshletIndex];
            for(u32 corner = 0u; corner < testCase.primitiveCount * 3u; ++corner){
                const u32 primitiveIndex = meshlet.primitiveOffset + corner;
                const u32 expectedIndex = NWB_MESH_OBJECT_FIRST_VERTEX_INDEX + meshlet.localVertexOffset
                    + inputs.primitiveIndices[primitiveIndex];
                // The persistent stream retains every triangle, even when the current dynamic view culls its meshlet.
                EXPECT_EQ(persistentIndices[primitiveIndex], expectedIndex) << primitiveIndex;
                EXPECT_LT(persistentIndices[primitiveIndex], compactVertexCount);
                indexWritten[primitiveIndex] = true;
            }
        }
        for(u32 byte = cacheIndexByteOffset; byte < cacheByteSize; ++byte){
            const u32 index = (byte - cacheIndexByteOffset) / s_IndexByteStride;
            if(index >= s_OutputVertexCapacity || !indexWritten[index])
                EXPECT_EQ(cacheBytes[byte], 0x39u) << byte;
        }
    }
    bool written[s_OutputVertexCapacity]{};
    bool indexedWritten[s_IndexedOutputByteCapacity]{};
    const auto markIndexedBytes = [&](const u32 offset, const u32 byteCount){
        for(u32 byte = 0u; byte < byteCount; ++byte)
            indexedWritten[offset + byte] = true;
    };
    const u8* const indexedBytes = reinterpret_cast<const u8*>(mapped[1]);
    const u32* const indices = reinterpret_cast<const u32*>(indexedBytes + indexByteOffset);
    const GeneratedVertex culled = CulledVertex();
    if(testCase.meshletCount != 0u || objectKernels != nullptr){
        EXPECT_EQ(NWB_MEMCMP(&mapped[1][NWB_MESH_EMULATION_SENTINEL_VERTEX_INDEX], &culled, sizeof(culled)), 0);
        markIndexedBytes(NWB_MESH_EMULATION_SENTINEL_VERTEX_INDEX * NWB_MESH_EMULATION_VERTEX_BYTE_SIZE, NWB_MESH_EMULATION_VERTEX_BYTE_SIZE);
    }
    for(u32 meshletIndex = 0u; meshletIndex < testCase.meshletCount; ++meshletIndex){
        const auto& meshlet = inputs.meshlets[meshletIndex];
        const bool expectCulled = (testCase.cull >= Cull::Frustum && testCase.cull <= Cull::Scissor)
            || testCase.cull == Cull::AuthoredClip || (testCase.cull == Cull::FirstMeshletOnly && meshletIndex == 0u);
        const bool meshletCulled = testCase.cull == Cull::Frustum || testCase.cull == Cull::Cone
            || (testCase.cull == Cull::FirstMeshletOnly && meshletIndex == 0u);
        if(!meshletCulled || objectKernels != nullptr){
            markIndexedBytes(
                (NWB_MESH_EMULATION_FIRST_VERTEX_INDEX + meshlet.localVertexOffset) * NWB_MESH_EMULATION_VERTEX_BYTE_SIZE,
                testCase.vertexCount * NWB_MESH_EMULATION_VERTEX_BYTE_SIZE
            );
        }
        for(u32 cornerIndex = 0u; cornerIndex < testCase.primitiveCount * 3u; ++cornerIndex){
            const u32 outputIndex = meshlet.primitiveOffset + cornerIndex;
            written[outputIndex] = true;
            const u32 vertexIndex = indices[outputIndex];
            ASSERT_LT(vertexIndex, compactVertexCount) << outputIndex;
            const u32 expectedIndex = expectCulled ? NWB_MESH_EMULATION_SENTINEL_VERTEX_INDEX
                : NWB_MESH_EMULATION_FIRST_VERTEX_INDEX + meshlet.localVertexOffset + inputs.primitiveIndices[outputIndex];
            EXPECT_EQ(vertexIndex, expectedIndex) << outputIndex;
            markIndexedBytes(indexByteOffset + outputIndex * s_IndexByteStride, s_IndexByteStride);
            const GeneratedVertex* const triangleVertices[] = { &mapped[0][outputIndex], &mapped[1][vertexIndex] };
            // Expand the actual GPU indices, then compare every legacy vertex byte, including packed halves and raster flags.
            EXPECT_EQ(NWB_MEMCMP(triangleVertices[0], triangleVertices[1], sizeof(GeneratedVertex)), 0) << outputIndex;
            for(u32 arm = 0u; arm < s_ExpectedDualCount; ++arm){
                const auto& vertex = *triangleVertices[arm];
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
                if(testCase.pose == Pose::Identity || testCase.pose == Pose::ResolvedDeformed){
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
    // Different initial patterns expose missing writes; gaps, culled meshlet vertices, and both tails remain untouched.
    GeneratedVertex sentinel;
    NWB_MEMSET(&sentinel, 0x5a, sizeof(sentinel));
    for(u32 index = 0u; index < s_OutputVertexCapacity; ++index){
        if(!written[index])
            EXPECT_EQ(NWB_MEMCMP(&mapped[0][index], &sentinel, sizeof(sentinel)), 0) << index;
    }
    for(u32 byte = 0u; byte < indexedByteSize; ++byte){
        if(!indexedWritten[byte])
            EXPECT_EQ(indexedBytes[byte], 0xa5u) << byte;
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
    constexpr u32 shapes[][2] = { { 3u, 1u }, { 6u, s_ExpectedDualCount }, { 96u, 1u }, { 96u, 126u } };
    for(const auto& shape : shapes){
        for(u32 width = Impl::MeshletRefDeltaWidth::U8; width <= Impl::MeshletRefDeltaWidth::U32; ++width){
            for(u32 pose = Pose::Identity; pose <= Pose::ResolvedDeformed; ++pose){
                for(u32 meshlets = 1u; meshlets <= s_ExpectedDualCount; ++meshlets){
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
        for(u32 meshlets = 1u; meshlets <= s_ExpectedDualCount; ++meshlets){
            const Case testCase{
                96u, 126u, meshlets, Impl::MeshletRefDeltaWidth::U8, Pose::Identity, static_cast<Cull::Enum>(cull)
            };
            ASSERT_NO_FATAL_FAILURE(RunCase(device(), *reference, *candidate, testCase));
        }
    }
    const Case mixed{ 96u, 126u, s_ExpectedDualCount, Impl::MeshletRefDeltaWidth::U16, Pose::Identity, Cull::FirstMeshletOnly };
    ASSERT_NO_FATAL_FAILURE(RunCase(device(), *reference, *candidate, mixed));
    const Case empty{ 3u, 1u, 0u, Impl::MeshletRefDeltaWidth::U8, Pose::Identity, Cull::None };
    ASSERT_NO_FATAL_FAILURE(RunCase(device(), *reference, *candidate, empty));
}


TEST_F(MeshKernelTest, ObjectCachePreservesSeamsCullsAndCurrentTransforms){
    using namespace __hidden_mesh_emulation_kernel_tests;
    Alloc::ScratchArena scratchArena(Name("tests/smoke/mesh_kernel/object_cache"));
    ComputePipelineHandle reference;
    ObjectGeometryKernels kernels;
    ComputePipelineHandle indexedReference;
    const Common::LoggerRegistrationGuard diagnosticGuard(*s_logger, Common::LoggerBreakPolicy::BreakOnFatal);
    const auto run = [&](){
        ASSERT_TRUE(loadMeshKernel(false, scratchArena, reference));
        ASSERT_TRUE(loadMeshKernel(true, scratchArena, indexedReference));
        ASSERT_TRUE(LoadObjectGeometryKernels(device(), arena(), scratchArena, kernels));
        for(u32 width = Impl::MeshletRefDeltaWidth::U8; width <= Impl::MeshletRefDeltaWidth::U32; ++width){
            for(u32 pose = Pose::Identity; pose <= Pose::Mirrored; ++pose){
                const Case testCase{
                    96u, 126u, s_ExpectedDualCount, static_cast<Impl::MeshletRefDeltaWidth::Enum>(width), static_cast<Pose::Enum>(pose), Cull::None
                };
                ASSERT_NO_FATAL_FAILURE(RunCase(device(), *reference, *indexedReference, testCase, &kernels));
            }
        }
        for(u32 cull = Cull::Frustum; cull <= Cull::FirstMeshletOnly; ++cull){
            const Case testCase{ 96u, 126u, s_ExpectedDualCount, Impl::MeshletRefDeltaWidth::U16, Pose::Identity, static_cast<Cull::Enum>(cull) };
            ASSERT_NO_FATAL_FAILURE(RunCase(device(), *reference, *indexedReference, testCase, &kernels));
        }
        const Case partial{ 6u, s_ExpectedDualCount, 1u, Impl::MeshletRefDeltaWidth::U8, Pose::Transformed, Cull::None };
        ASSERT_NO_FATAL_FAILURE(RunCase(device(), *reference, *indexedReference, partial, &kernels));
        const Case reversed{ 96u, 126u, s_ExpectedDualCount, Impl::MeshletRefDeltaWidth::U32, Pose::Mirrored, Cull::None, true };
        ASSERT_NO_FATAL_FAILURE(RunCase(device(), *reference, *indexedReference, reversed, &kernels));
        const Case empty{ 3u, 1u, 0u, Impl::MeshletRefDeltaWidth::U8, Pose::Identity, Cull::None };
        ASSERT_NO_FATAL_FAILURE(RunCase(device(), *reference, *indexedReference, empty, &kernels));
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

TEST_F(MeshKernelTest, AuthoredClipChangesRetainCompletedVertexCulling){
    using namespace __hidden_mesh_emulation_kernel_tests;
    Alloc::ScratchArena scratchArena(Name("tests/smoke/mesh_kernel/authored_clip"));
    ComputePipelineHandle reference;
    ComputePipelineHandle candidate;
    ASSERT_TRUE(loadMeshKernel(false, scratchArena, reference, true));
    ASSERT_TRUE(loadMeshKernel(true, scratchArena, candidate, true));
    for(u32 width = Impl::MeshletRefDeltaWidth::U8; width <= Impl::MeshletRefDeltaWidth::U32; ++width){
        for(u32 meshlets = 1u; meshlets <= s_ExpectedDualCount; ++meshlets){
            // Source positions are visible; only the authored vertex builder moves the triangles outside the clip volume.
            const Case testCase{
                96u, 126u, meshlets, static_cast<Impl::MeshletRefDeltaWidth::Enum>(width), Pose::Identity, Cull::AuthoredClip
            };
            ASSERT_NO_FATAL_FAILURE(RunCase(device(), *reference, *candidate, testCase));
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

