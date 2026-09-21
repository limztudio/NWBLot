// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "optical_trace_kernel_fixture.h"

#include <impl/assets/graphics/bvh/constants.h>
#include <impl/assets/graphics/mesh/runtime_constants.h>
#include <impl/assets/graphics/raytrace/optical_scene_constants.h>
#include <impl/assets/graphics/raytrace/optical_transport_constants.h>
#include <impl/assets/graphics/shadow/constants.h>

#include <global/bit.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_optical_trace_kernel_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct Attribute{
    u32 normal[2] = { 0u, 0x00003c00u };
    Float2U uv{};
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
    u32 limits[4] = { 1u, NWB_OPTICAL_DEFAULT_QUERIES, 0u, 0u };
};

struct PushConstants{
    u32 inputSlot;
    u32 outputSlot;
    u32 outputOffset;
    u32 reserved = 0u;
};

struct Observation{
    f32 radiance[3];
    f32 distance;
    u32 queryCount;
    u32 bootstrapEventCount;
    u32 tirCount;
    u32 termination;
    u32 transparentPath;
    u32 surfaceHit;
    u32 reserved[2];
};

struct Plane{
    f32 z = 1.0f;
    u32 model = 0u;
    u32 mode = NWB_RT_OPTICAL_BOUNDARY_UNSPECIFIED;
    bool transparent = true;
    bool boundsValid = true;
};

struct Case{
    AStringView name;
    RayInput ray;
    Plane planes[34];
    u32 planeCount = 1u;
    u32 headerFlags = NWB_RT_OPTICAL_SCENE_FLAG_BOUNDS_VALID;
    u32 opticalCount = 0xffffffffu;
    f32 extent = 2.0f;
    bool reverseOrder = false;
    bool mirrored = false;
    bool missingScene = false;
    bool replaceBounds = false;
    Float3U boundsMin{};
    Float3U boundsMax{};
    Float3U expectedRadiance{ 0.15625f, 0.25f, 0.40625f };
    f32 expectedDistance = 1.0f;
    u32 expectedTermination = NWB_OPTICAL_TERMINATION_MISS_OUTSIDE;
    u32 expectedQueries = 3u;
    u32 expectedBootstrap = 0u;
    u32 expectedTransparent = 1u;
    u32 expectedSurfaceHit = 1u;
};

using Cases = Vector<Case, Alloc::ScratchArena>;

static_assert(sizeof(Attribute) == NWB_RAYTRACE_VERTEX_ATTRIBUTE_STRIDE_BYTES);
static_assert(sizeof(Material) == 36u);
static_assert(sizeof(MeshInstance) == 96u);
static_assert(sizeof(RayInput) == 64u);
static_assert(sizeof(PushConstants) == 16u);
static_assert(sizeof(Observation) == 48u);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Case& AddCase(Cases& cases, const AStringView name){
    cases.push_back(Case{ .name = name, .ray = {}, .planes = {} });
    return cases.back();
}

void ExpectRejected(Case& testCase, const u32 queries, const u32 termination = NWB_OPTICAL_TERMINATION_UNSUPPORTED){
    testCase.expectedRadiance = {};
    testCase.expectedTermination = termination;
    testCase.expectedQueries = queries;
    if(queries == 0u){
        testCase.expectedDistance = 0.0f;
        testCase.expectedTransparent = 0u;
        testCase.expectedSurfaceHit = 0u;
    }
}

void BuildCases(Cases& cases){
    cases.reserve(64u);
    AddCase(cases, "single alpha quarter");
    auto& half = AddCase(cases, "single alpha half");
    half.planes[0].model = 1u;
    half.expectedRadiance = { 0.1875f, 0.25f, 0.3125f };
    auto& full = AddCase(cases, "alpha one terminates");
    full.planes[0].model = s_ExpectedDualCount;
    full.expectedRadiance = { 0.25f, 0.25f, 0.125f };
    full.expectedTermination = NWB_OPTICAL_TERMINATION_OPAQUE;
    full.expectedQueries = s_ExpectedDualCount;
    auto& zero = AddCase(cases, "alpha zero keeps environment");
    zero.planes[0].model = 3u;
    zero.expectedRadiance = { 0.125f, 0.25f, 0.5f };
    auto& overlap = AddCase(cases, "overlapping alpha silhouettes");
    overlap.planeCount = s_ExpectedDualCount;
    overlap.planes[1].z = 2.0f;
    overlap.planes[1].model = 1u;
    overlap.expectedRadiance = { 0.25f, 0.25f, 0.3125f };
    overlap.expectedDistance = 2.0f;
    overlap.expectedQueries = 5u;
    const Case twoAlpha = overlap;
    cases.push_back(twoAlpha);
    cases.back().name = "reversed TLAS insertion";
    cases.back().reverseOrder = true;
    auto& opaqueAfter = AddCase(cases, "opaque behind alpha");
    opaqueAfter.planeCount = s_ExpectedDualCount;
    opaqueAfter.planes[1].z = 2.0f;
    opaqueAfter.planes[1].transparent = false;
    opaqueAfter.expectedRadiance = { 0.34375f, 0.25f, 0.21875f };
    opaqueAfter.expectedDistance = 2.0f;
    opaqueAfter.expectedTermination = NWB_OPTICAL_TERMINATION_OPAQUE;
    auto& opaqueBefore = AddCase(cases, "near opaque is last TLAS instance");
    opaqueBefore.planeCount = s_ExpectedDualCount;
    opaqueBefore.planes[0].z = 2.0f;
    opaqueBefore.planes[1].transparent = false;
    opaqueBefore.expectedRadiance = { 0.375f, 0.25f, 0.25f };
    opaqueBefore.expectedTermination = NWB_OPTICAL_TERMINATION_OPAQUE;
    opaqueBefore.expectedQueries = 1u;
    opaqueBefore.expectedTransparent = 0u;
    auto& noTransparent = AddCase(cases, "empty transparent scene opaque hit");
    noTransparent.planes[0].transparent = false;
    noTransparent.expectedRadiance = { 0.25f, 0.25f, 0.125f };
    noTransparent.expectedTermination = NWB_OPTICAL_TERMINATION_OPAQUE;
    noTransparent.expectedQueries = 1u;
    noTransparent.expectedTransparent = 0u;
    auto& empty = AddCase(cases, "empty scene ignores stored invalid bounds");
    empty.planes[0].transparent = false;
    empty.planes[0].z = 200.0f; // Keep a valid nonzero-mask TLAS instance beyond the finite ray range.
    empty.replaceBounds = true;
    empty.boundsMin.x = BitCast<f32>(0x7fc00000u);
    empty.expectedRadiance = { 0.125f, 0.25f, 0.5f };
    empty.expectedDistance = 0.0f;
    empty.expectedQueries = 1u;
    empty.expectedTransparent = empty.expectedSurfaceHit = 0u;
    const Case emptyScene = empty;
    cases.push_back(emptyScene);
    cases.back().name = "empty scene still requires valid header";
    cases.back().headerFlags = 0u;
    ExpectRejected(cases.back(), 0u);
    auto& miss = AddCase(cases, "finite range stops before alpha");
    miss.ray.directionTMax.w = 0.5f;
    miss.expectedRadiance = { 0.125f, 0.25f, 0.5f };
    miss.expectedDistance = 0.0f;
    miss.expectedQueries = 1u;
    miss.expectedTransparent = miss.expectedSurfaceHit = 0u;
    auto& direction = AddCase(cases, "nonunit direction is normalized");
    direction.ray.directionTMax.z = 7.0f;
    AddCase(cases, "coplanar indexed seam").ray.originTMin.x = 0.25f;
    AddCase(cases, "mirrored instance").mirrored = true;
    auto& coincidence = AddCase(cases, "independent coincident alpha is ambiguous");
    coincidence.planeCount = s_ExpectedDualCount;
    ExpectRejected(coincidence, s_ExpectedDualCount, NWB_OPTICAL_TERMINATION_AMBIGUOUS);
    const u32 invalidModels[] = { 4u, 5u, 6u, 7u, 8u, 9u };
    constexpr AStringView invalidNames[] = {
        "unspecified nonunit IOR", "nonfinite IOR", "nonfinite alpha coverage", "nonfinite absorption tint", "IOR below one", "infinite IOR"
    };
    for(u32 index = 0u; index < LengthOf(invalidModels); ++index){
        auto& invalid = AddCase(cases, invalidNames[index]);
        invalid.planes[0].model = invalidModels[index];
        ExpectRejected(invalid, invalidModels[index] == 6u ? s_ExpectedDualCount : 1u);
    }
    auto& unknown = AddCase(cases, "unknown boundary policy rejected");
    unknown.planes[0].mode = 0x7fffffffu;
    ExpectRejected(unknown, s_ExpectedDualCount);
    auto& instanceBounds = AddCase(cases, "invalid transparent instance bounds");
    instanceBounds.planes[0].boundsValid = false;
    ExpectRejected(instanceBounds, 1u);
    instanceBounds.expectedTransparent = 0u;
    auto& shortMetadata = AddCase(cases, "hit outside declared instance count");
    shortMetadata.opticalCount = 0u;
    ExpectRejected(shortMetadata, 1u);
    shortMetadata.expectedTransparent = 0u;
    auto& header = AddCase(cases, "invalid scene header");
    header.headerFlags = 0u;
    ExpectRejected(header, 0u);
    auto& missing = AddCase(cases, "missing scene slot");
    missing.missingScene = true;
    ExpectRejected(missing, 0u);
    auto& inverted = AddCase(cases, "inverted union bounds");
    inverted.replaceBounds = true;
    inverted.boundsMin = { 1.0f, 0.0f, 0.0f };
    inverted.boundsMax = { -1.0f, 0.0f, 1.0f };
    ExpectRejected(inverted, 0u);
    auto& nonfinite = AddCase(cases, "nonfinite union bounds");
    nonfinite.replaceBounds = true;
    nonfinite.boundsMin.x = BitCast<f32>(0x7fc00000u);
    ExpectRejected(nonfinite, 0u);
    auto& origin = AddCase(cases, "nonfinite ray origin");
    origin.ray.originTMin.x = BitCast<f32>(0x7fc00000u);
    ExpectRejected(origin, 0u);
    auto& invalidDirection = AddCase(cases, "zero ray direction");
    invalidDirection.ray.directionTMax.z = 0.0f;
    ExpectRejected(invalidDirection, 0u);
    auto& invalidRange = AddCase(cases, "invalid finite ray range");
    invalidRange.ray.directionTMax.w = invalidRange.ray.originTMin.w;
    ExpectRejected(invalidRange, 0u);
    auto& noQueries = AddCase(cases, "zero query budget");
    noQueries.ray.limits[1] = 0u;
    ExpectRejected(noQueries, 0u, NWB_OPTICAL_TERMINATION_QUERY_LIMIT);
    auto& oneQuery = AddCase(cases, "boundary query budget exhausted");
    oneQuery.ray.limits[1] = 1u;
    ExpectRejected(oneQuery, 1u, NWB_OPTICAL_TERMINATION_QUERY_LIMIT);
    cases.push_back(twoAlpha);
    auto& threeQueries = cases.back();
    threeQueries.name = "preserve accumulated radiance at query cap";
    threeQueries.ray.limits[1] = 3u;
    threeQueries.expectedQueries = 3u;
    threeQueries.expectedTermination = NWB_OPTICAL_TERMINATION_QUERY_LIMIT;
    threeQueries.expectedRadiance = { 0.0625f, 0.0625f, 0.03125f };
    cases.push_back(twoAlpha);
    auto& inside = cases.back();
    inside.name = "inside alpha union performs bootstrap";
    inside.ray.originTMin.z = 1.5f;
    inside.expectedDistance = 0.5f;
    inside.expectedQueries = 4u;
    inside.expectedBootstrap = 1u;
    inside.expectedRadiance = { 0.25f, 0.25f, 0.375f };
    const Case insideAlpha = inside;
    cases.push_back(insideAlpha);
    auto& insideRejected = cases.back();
    insideRejected.name = "bootstrap rejects unspecified nonunit IOR";
    insideRejected.planes[0].model = 4u;
    ExpectRejected(insideRejected, 1u);
    insideRejected.expectedDistance = 0.0f;
    insideRejected.expectedTransparent = insideRejected.expectedSurfaceHit = 0u;
    cases.push_back(insideAlpha);
    auto& insideUnknown = cases.back();
    insideUnknown.name = "bootstrap rejects unknown boundary policy";
    insideUnknown.planes[0].mode = 0x7fffffffu;
    ExpectRejected(insideUnknown, 1u);
    insideUnknown.expectedDistance = 0.0f;
    insideUnknown.expectedTransparent = insideUnknown.expectedSurfaceHit = 0u;
    auto& clampedBudget = AddCase(cases, "requested queries remain clamped to shared maximum");
    clampedBudget.planeCount = 9u;
    clampedBudget.ray.limits[1] = 64u;
    for(u32 index = 0u; index < clampedBudget.planeCount; ++index){
        clampedBudget.planes[index].z = static_cast<f32>(index + 1u);
        clampedBudget.planes[index].model = 3u;
    }
    ExpectRejected(clampedBudget, NWB_OPTICAL_MAX_QUERIES, NWB_OPTICAL_TERMINATION_QUERY_LIMIT);
    clampedBudget.expectedDistance = 8.0f;
    auto& dense = AddCase(cases, "bootstrap candidate cap remains conservative");
    dense.planeCount = 33u;
    dense.extent = 128.0f;
    for(u32 index = 0u; index < 32u; ++index)
        dense.planes[index].z = -32.0f + static_cast<f32>(index);
    ExpectRejected(dense, 1u, NWB_OPTICAL_TERMINATION_EVENT_LIMIT);
    dense.expectedBootstrap = NWB_OPTICAL_MAX_CANDIDATE_EVENTS;
    dense.expectedDistance = 0.0f;
    dense.expectedTransparent = dense.expectedSurfaceHit = 0u;
    const Case denseScene = dense;
    cases.push_back(denseScene);
    auto& completeBootstrap = cases.back();
    completeBootstrap.name = "bootstrap one candidate below cap completes";
    completeBootstrap.planeCount = 32u;
    for(u32 index = 0u; index < 31u; ++index)
        completeBootstrap.planes[index].z = -31.0f + static_cast<f32>(index);
    completeBootstrap.planes[31].z = 1.0f;
    completeBootstrap.expectedRadiance = { 0.1875f, 0.25f, 0.4375f };
    completeBootstrap.expectedDistance = 1.0f;
    completeBootstrap.expectedTermination = NWB_OPTICAL_TERMINATION_MISS_OUTSIDE;
    completeBootstrap.expectedQueries = 4u;
    completeBootstrap.expectedBootstrap = 31u;
    completeBootstrap.expectedTransparent = completeBootstrap.expectedSurfaceHit = 1u;
}

void RunCase(
    GraphicsBackend::Device& device,
    Alloc::GlobalArena& memoryArena,
    ComputePipeline& general,
    ComputePipeline& specialized,
    const Case& testCase,
    Alloc::ScratchArena& scratchArena){
    SCOPED_TRACE(testCase.name.data());
    auto& heap = device.getDescriptorHeap();
    GpuDescriptorHandle descriptors[9]{};
    ScopeExit releaseDescriptors([&]()noexcept{
        for(const GpuDescriptorHandle descriptor : descriptors){
            if(descriptor.valid())
                heap.free(descriptor);
        }
        heap.collectRetired();
    });
    const f32 extent = testCase.extent;
    const Float3U positions[] = { { -extent, -extent, 0.0f }, { extent, -extent, 0.0f }, { extent, extent, 0.0f }, { -extent, extent, 0.0f } };
    const u32 indices[] = { 0u, s_ExpectedDualCount, 1u, 0u, 3u, s_ExpectedDualCount };
    const Attribute attributes[LengthOf(indices)]{};
    Vector<MeshInstance, Alloc::ScratchArena> instances(testCase.planeCount, scratchArena);
    Vector<Material, Alloc::ScratchArena> materials(testCase.planeCount, scratchArena);
    Vector<RayTracingInstanceDesc, Alloc::ScratchArena> hardwareInstances(testCase.planeCount, scratchArena);
    Vector<u32, Alloc::ScratchArena> optical(8u + 4u * testCase.planeCount, scratchArena);
    Float3U boundsMin{ -extent, -extent, 1e30f };
    Float3U boundsMax{ extent, extent, -1e30f };
    for(u32 index = 0u; index < testCase.planeCount; ++index){
        if(testCase.planes[index].transparent){
            ++optical[3];
            boundsMin.z = Min(boundsMin.z, testCase.planes[index].z);
            boundsMax.z = Max(boundsMax.z, testCase.planes[index].z);
        }
    }
    if(testCase.replaceBounds){
        boundsMin = testCase.boundsMin;
        boundsMax = testCase.boundsMax;
    }
    for(u32 axis = 0u; axis < 3u; ++axis){
        optical[axis] = BitCast<u32>(boundsMin.raw[axis]);
        optical[4u + axis] = BitCast<u32>(boundsMax.raw[axis]);
    }
    optical[7] = testCase.headerFlags;
    RayInput ray = testCase.ray;
    ray.limits[0] = testCase.opticalCount == 0xffffffffu ? testCase.planeCount : testCase.opticalCount;
    const u32 typedWords = 0u;
    const void* const data[] = { positions, indices, attributes, instances.data(), materials.data(), optical.data(), &typedWords, &ray };
    const usize sizes[] = {
        sizeof(positions), sizeof(indices), sizeof(attributes), instances.size() * sizeof(MeshInstance),
        materials.size() * sizeof(Material), optical.size() * sizeof(u32), sizeof(typedWords), sizeof(ray)
    };
    BufferHandle buffers[8];
    for(u32 index = 0u; index < LengthOf(buffers); ++index){
        BufferDesc desc;
        desc.setByteSize(sizes[index]).setInitialState(ResourceStates::Common).setKeepInitialState(true);
        if(index == 7u)
            desc.setIsConstantBuffer(true);
        else
            desc.setCanHaveRawViews(true);
        if(index < s_ExpectedDualCount)
            desc.setIsAccelStructBuildInput(true);
        buffers[index] = device.createBuffer(desc);
        ASSERT_TRUE(buffers[index]);
        descriptors[index] = heap.allocate(index == 7u ? GpuDescriptorClass::UniformBuffer : GpuDescriptorClass::StorageBuffer);
        ASSERT_TRUE(descriptors[index].valid());
        const DescriptorWriteItem view = index == 7u
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
        const Plane& plane = testCase.planes[index];
        instances[index].translation.z = plane.z;
        instances[index].scale.x = testCase.mirrored ? -1.0f : 1.0f;
        Material& material = materials[index];
        material.modelId = plane.model;
        material.flags = plane.transparent ? NWB_RT_INSTANCE_MATERIAL_FLAG_TRANSPARENT : 0u;
        material.meshInstanceIndex = index;
        material.positionSlot = descriptors[0].slot();
        material.indexSlot = descriptors[1].slot();
        material.attributeSlot = descriptors[2].slot();
        optical[8u + 4u * index] = 100u + index;
        optical[10u + 4u * index] = plane.mode;
        optical[11u + 4u * index] = (plane.transparent ? NWB_RT_OPTICAL_INSTANCE_FLAG_TRANSPARENT : 0u)
            | (plane.boundsValid ? NWB_RT_OPTICAL_INSTANCE_FLAG_BOUNDS_VALID : 0u);
        AffineTransform transform = s_identityTransform;
        transform._11 = instances[index].scale.x;
        transform._34 = plane.z;
        const u32 mask = NWB_RT_OPTICAL_BASE_INSTANCE_MASK | (plane.transparent ? NWB_RT_OPTICAL_TRANSPARENT_INSTANCE_MASK : 0u);
        hardwareInstances[index].setBLAS(blas.get()).setInstanceID(index).setTransform(transform).setInstanceMask(mask);
    }
    if(testCase.reverseOrder){
        for(usize index = 0u; index < hardwareInstances.size() / s_ExpectedDualCount; ++index)
            Swap(hardwareInstances[index], hardwareInstances[hardwareInstances.size() - index - 1u]);
    }
    ray.slots[0] = descriptors[4].slot();
    ray.slots[1] = testCase.missingScene ? 0xffffffffu : descriptors[5].slot();
    ray.slots[2] = descriptors[3].slot();
    ray.slots[3] = descriptors[6].slot();
    RayTracingAccelStructDesc tlasDesc(memoryArena);
    tlasDesc.setTopLevelMaxInstances(testCase.planeCount);
    const RayTracingAccelStructHandle tlas = device.createAccelStruct(tlasDesc);
    ASSERT_TRUE(tlas);
    descriptors[8] = heap.allocate(GpuDescriptorClass::AccelStruct);
    ASSERT_TRUE(descriptors[8].valid());
    ASSERT_TRUE(heap.write(descriptors[8], DescriptorWriteItem::RayTracingAccelStruct(0u, tlas.get())));
    Observation initial[4];
    NWB_MEMSET(initial, 0xa5, sizeof(initial));
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
    const GpuDescriptorHandle outputDescriptor = heap.allocate(GpuDescriptorClass::StorageBuffer);
    ASSERT_TRUE(outputDescriptor.valid());
    ScopeExit releaseOutput([&]()noexcept{ heap.free(outputDescriptor); heap.collectRetired(); });
    ASSERT_TRUE(heap.write(outputDescriptor, DescriptorWriteItem::RawBuffer_UAV(0u, output.get())));
    const CommandListHandle commands = device.createCommandList();
    ASSERT_TRUE(commands);
    commands->open();
    for(u32 index = 0u; index < LengthOf(buffers); ++index)
        ASSERT_TRUE(commands->tryWriteBuffer(*buffers[index], data[index], sizes[index]));
    commands->buildBottomLevelAccelStruct(blas.get(), &geometry, 1u, RayTracingAccelStructBuildFlags::None);
    commands->buildTopLevelAccelStruct(tlas.get(), hardwareInstances.data(), hardwareInstances.size(), RayTracingAccelStructBuildFlags::None);
    commands->setAccelStructState(tlas.get(), ResourceStates::AccelStructRead);
    for(u32 index = 0u; index < LengthOf(buffers); ++index)
        commands->setBufferState(buffers[index].get(), index == 7u ? ResourceStates::ConstantBuffer : ResourceStates::ShaderResource);
    ASSERT_TRUE(commands->tryWriteBuffer(*output, initial, sizeof(initial)));
    commands->setBufferState(output.get(), ResourceStates::UnorderedAccess, true);
    commands->commitBarriers();
    ComputePipeline* const pipelines[] = { &general, &specialized };
    for(u32 index = 0u; index < LengthOf(pipelines); ++index){
        const PushConstants push{ descriptors[7].slot(), outputDescriptor.slot(), (index + 1u) * static_cast<u32>(sizeof(Observation)) };
        if(index != 0u){
            commands->setBufferState(output.get(), ResourceStates::UnorderedAccess, true);
            commands->commitBarriers();
        }
        commands->setComputeState(ComputeState().setPipeline(pipelines[index]));
        heap.bindCompute(*commands, *pipelines[index], descriptors[8]);
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
    const Observation* const observations = static_cast<const Observation*>(device.mapBuffer(*output, CpuAccessMode::Read));
    ASSERT_NE(observations, nullptr);
    ScopeExit unmap([&]()noexcept{ device.unmapBuffer(*output); });
    EXPECT_EQ(NWB_MEMCMP(&observations[0], &initial[0], sizeof(Observation)), 0);
    EXPECT_EQ(NWB_MEMCMP(&observations[3], &initial[3], sizeof(Observation)), 0);
    // Compare every bit, including result flags and consumed-query/bootstrap counters, before checking the independent oracle.
    u32 generalWords[sizeof(Observation) / sizeof(u32)];
    u32 specializedWords[LengthOf(generalWords)];
    NWB_MEMCPY(generalWords, sizeof(generalWords), &observations[1], sizeof(Observation));
    NWB_MEMCPY(specializedWords, sizeof(specializedWords), &observations[2], sizeof(Observation));
    for(u32 word = 0u; word < sizeof(Observation) / sizeof(u32); ++word)
        EXPECT_EQ(generalWords[word], specializedWords[word]) << "result word " << word;
    for(u32 variant = 1u; variant <= s_ExpectedDualCount; ++variant){
        SCOPED_TRACE(variant);
        const Observation& actual = observations[variant];
        for(u32 channel = 0u; channel < 3u; ++channel){
            EXPECT_TRUE(IsFinite(actual.radiance[channel]));
            EXPECT_FLOAT_EQ(actual.radiance[channel], testCase.expectedRadiance.raw[channel]);
        }
        EXPECT_NEAR(actual.distance, testCase.expectedDistance, 0.00002f);
        EXPECT_EQ(actual.queryCount, testCase.expectedQueries);
        EXPECT_EQ(actual.bootstrapEventCount, testCase.expectedBootstrap);
        EXPECT_EQ(actual.tirCount, 0u);
        EXPECT_EQ(actual.termination, testCase.expectedTermination);
        EXPECT_EQ(actual.transparentPath, testCase.expectedTransparent);
        EXPECT_EQ(actual.surfaceHit, testCase.expectedSurfaceHit);
        EXPECT_EQ(actual.reserved[0], 0u);
        EXPECT_EQ(actual.reserved[1], 0u);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(OpticalTraceKernelTest, UnspecifiedSpecializationPreservesGeneralTraversal){
    using namespace __hidden_optical_trace_kernel_tests;
    const Common::LoggerRegistrationGuard diagnosticLogger(*s_logger, Common::LoggerBreakPolicy::BreakOnFatal);
    const u32 initialErrors = s_logger->errorCount();
    // Keep ordinary ASSERT failures inside this scope so captured backend diagnostics are emitted after cleanup.
    const auto run = [&]{
        Alloc::ScratchArena scratchArena(Name("tests/smoke/reflection_kernel/optical_trace"));
        ComputePipelineHandle general;
        ComputePipelineHandle specialized;
        ASSERT_TRUE(loadTraceKernel(true, scratchArena, general));
        ASSERT_TRUE(loadTraceKernel(false, scratchArena, specialized));
        Cases cases(scratchArena);
        BuildCases(cases);
        for(const Case& testCase : cases)
            ASSERT_NO_FATAL_FAILURE(RunCase(device(), arena(), *general, *specialized, testCase, scratchArena));
    };
    run();
    const TStringView validationPrefix = NWB_TEXT("Vulkan debug: [severity=error");
    const bool validationFailed = s_logger->sawMessageContaining(validationPrefix);
    const bool backendFailed = s_logger->errorCount() != initialErrors;
    if(HasFailure() || validationFailed || backendFailed){
        s_logger->emitErrorsToStderr();
        s_logger->emitMessagesContainingToStderr(validationPrefix);
    }
    EXPECT_FALSE(backendFailed) << "optical traversal fixture emitted a backend error";
    EXPECT_FALSE(validationFailed) << "optical traversal fixture emitted a Vulkan severity=error message";
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

