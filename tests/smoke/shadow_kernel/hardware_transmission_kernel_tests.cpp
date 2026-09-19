// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shadow_kernel_fixture.h"

#include <impl/assets/graphics/bvh/constants.h>
#include <impl/assets/graphics/mesh/runtime_constants.h>
#include <impl/assets/graphics/raytrace/optical_scene_constants.h>
#include <impl/assets/graphics/shadow/constants.h>
#include <impl/assets/graphics/shadow/hardware_transparent_binding_slots.h>
#include <impl/assets_csg/cook.h>
#include <impl/assets_shader/cook.h>

#include <global/filesystem.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_hardware_transmission_kernel_tests{


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

struct ContextSlots{
    u32 scene[4] = { 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu };
    u32 material[4] = { 0xffffffffu, 0xffffffffu, 0u, 0u };
};

struct TestRay{
    Float4U originTMin{};
    Float4U directionTMax{ 0.0f, 0.0f, 1.0f, 100.0f };
};

struct PushConstants{
    u32 width = 1u;
    u32 height = 1u;
    u32 frameIndex = 0u;
    u32 lightSlot = 0u;
    u32 sampleIndex = 0u;
    u32 sampleCount = 1u;
    u32 deferredResourcesHeapSlot = 0u;
    u32 materialContextSlotsHeapSlot = 0u;
    u32 crossingsHeapSlot = 0u;
    u32 overflowListHeapSlot = 0u;
    u32 overflowArgsHeapSlot = 0u;
    u32 outputStorageSlot = 0u;
};

struct Observation{
    f32 selected[3];
    u32 crossingCount;
    f32 continuation[3];
    u32 overflowCount;
    u32 overflowGroups;
    u32 overflowPixel;
    u32 usedContinuation;
    u32 reserved;
};

struct Case{
    AStringView name;
    u32 instanceCount = 1u;
    u32 shellCount = 1u;
    f32 instanceStep = 0.0f;
    Float3U scale{ 1.0f, 1.0f, 1.0f };
    Float3U origin{ 0.125f, 0.25f, 0.0f };
    f32 rayLength = 100.0f;
    bool closed = true;
    bool opaque = false;
    bool reverseOrder = false;
    bool duplicateBoundary = false;
    bool wedge = false;
};

struct MeshData{
    Vector<Float3U, Alloc::ScratchArena> positions;
    Vector<u32, Alloc::ScratchArena> indices;
    Vector<Attribute, Alloc::ScratchArena> attributes;


    explicit MeshData(Alloc::ScratchArena& arena)
        : positions(arena)
        , indices(arena)
        , attributes(arena)
    {}
};

struct GpuMesh{
    BufferHandle buffers[3];
    GpuDescriptorHandle descriptors[3];
    RayTracingGeometryDesc geometry;
    RayTracingAccelStructHandle blas;
};

static_assert(sizeof(Attribute) == NWB_RAYTRACE_VERTEX_ATTRIBUTE_STRIDE_BYTES);
static_assert(sizeof(Material) == 36u);
static_assert(sizeof(MeshInstance) == 96u);
static_assert(sizeof(ContextSlots) == 32u);
static_assert(sizeof(TestRay) == 32u);
static_assert(sizeof(PushConstants) == NWB_HW_TRANSPARENT_PUSH_CONSTANT_BYTES);
static_assert(sizeof(Observation) == 48u);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void AppendBox(MeshData& mesh, const f32 nearZ){
    const u32 base = static_cast<u32>(mesh.positions.size());
    const Float3U vertices[] = {
        { -1.0f, -1.0f, nearZ }, { 1.0f, -1.0f, nearZ }, { 1.0f, 1.0f, nearZ }, { -1.0f, 1.0f, nearZ },
        { -1.0f, -1.0f, nearZ + 1.0f }, { 1.0f, -1.0f, nearZ + 1.0f },
        { 1.0f, 1.0f, nearZ + 1.0f }, { -1.0f, 1.0f, nearZ + 1.0f }
    };
    const u32 indices[] = {
        0u, 2u, 1u, 0u, 3u, 2u, 4u, 5u, 6u, 4u, 6u, 7u,
        0u, 1u, 5u, 0u, 5u, 4u, 1u, 2u, 6u, 1u, 6u, 5u,
        2u, 3u, 7u, 2u, 7u, 6u, 3u, 0u, 4u, 3u, 4u, 7u
    };
    for(const Float3U& vertex : vertices)
        mesh.positions.push_back(vertex);
    for(const u32 index : indices)
        mesh.indices.push_back(base + index);
}

void BuildMesh(MeshData& mesh, const Case& testCase, const bool wedge){
    mesh.positions.reserve(testCase.shellCount * 8u);
    mesh.indices.reserve(testCase.shellCount * 36u + 3u);
    mesh.attributes.reserve(testCase.shellCount * 36u + 3u);
    if(wedge){
        const Float3U vertices[] = {
            { 0.0f, -1.0f, 1.0f }, { 1.0f, -1.0f, 1.0f }, { 1.0f, -1.0f, 2.0f },
            { 0.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 2.0f }
        };
        const u32 indices[] = {
            0u, 1u, 2u, 3u, 5u, 4u, 0u, 3u, 4u, 0u, 4u, 1u,
            1u, 4u, 5u, 1u, 5u, 2u, 2u, 5u, 3u, 2u, 3u, 0u
        };
        mesh.positions.assign(vertices, vertices + LengthOf(vertices));
        mesh.indices.assign(indices, indices + LengthOf(indices));
    }
    else{
        for(u32 shell = 0u; shell < testCase.shellCount; ++shell)
            AppendBox(mesh, 1.0f + 2.0f * static_cast<f32>(shell));
    }
    if(testCase.duplicateBoundary){
        // The fixture ray hits the second front triangle; duplicating it must not add a physical interface.
        for(u32 corner = 3u; corner < 6u; ++corner)
            mesh.indices.push_back(mesh.indices[corner]);
    }
    if(testCase.reverseOrder){
        const usize triangleCount = mesh.indices.size() / 3u;
        for(usize triangle = 0u; triangle < triangleCount / 2u; ++triangle){
            for(usize corner = 0u; corner < 3u; ++corner)
                Swap(mesh.indices[triangle * 3u + corner], mesh.indices[(triangleCount - triangle - 1u) * 3u + corner]);
        }
    }
    mesh.attributes.resize(mesh.indices.size());
}

[[nodiscard]] Float3U Tint(const u32 model){
    return model == 0u ? Float3U{ 0.5f, 0.75f, 1.0f } : Float3U{ 0.75f, 0.5f, 1.0f };
}

[[nodiscard]] Float3U AnalyticTransmission(const Case& testCase){
    if(testCase.opaque || testCase.origin.x > 2.0f)
        return { 1.0f, 1.0f, 1.0f };
    Float3U result{ 1.0f, 1.0f, 1.0f };
    for(u32 instance = 0u; instance < testCase.instanceCount; ++instance){
        const Float3U tint = Tint(instance & 1u);
        f32 chord = static_cast<f32>(testCase.shellCount) * testCase.scale.z;
        f32 interfaces = 2.0f * static_cast<f32>(testCase.shellCount);
        f32 interfaceFactor = Pow(0.96f, interfaces);
        if(testCase.wedge && instance == 0u){
            chord = Max(testCase.origin.x, 0.0f);
            const f32 weight = Clamp(chord / 0.002f, 0.0f, 1.0f);
            const f32 fade = weight * weight * (3.0f - 2.0f * weight);
            const f32 slopedTransmission = 1.0f - (0.04f + 0.96f * Pow(1.0f - Sqrt(0.5f), 5.0f));
            interfaceFactor = 1.0f + (0.96f * slopedTransmission - 1.0f) * fade;
        }
        else if(testCase.origin.z > 1.0f && instance == 0u){
            if(!testCase.closed)
                return { 1.0f, 1.0f, 1.0f };
            chord -= 0.5f;
            interfaces -= 1.0f;
            interfaceFactor = Pow(0.96f, interfaces);
        }
        if(testCase.rayLength < 2.0f && testCase.closed){
            chord = Max(testCase.rayLength - 1.0f, 0.0f);
            interfaceFactor = chord > 0.0f ? 0.96f : 1.0f;
        }
        result.x *= Pow(tint.x, chord) * interfaceFactor;
        result.y *= Pow(tint.y, chord) * interfaceFactor;
        result.z *= Pow(tint.z, chord) * interfaceFactor;
    }
    return result;
}

void RunCase(
    GraphicsBackend::Device& device,
    Alloc::GlobalArena& memoryArena,
    ComputePipeline& collectPipeline,
    ComputePipeline& evaluatePipeline,
    const Case& testCase,
    Alloc::ScratchArena& scratchArena){
    SCOPED_TRACE(testCase.name.data());
    auto& heap = device.getDescriptorHeap();
    MeshData meshData[] = { MeshData(scratchArena), MeshData(scratchArena) };
    const u32 meshCount = testCase.wedge ? 2u : 1u;
    GpuMesh meshes[2];
    GpuDescriptorHandle inputDescriptors[6]{};
    GpuDescriptorHandle outputDescriptors[4]{};
    GpuDescriptorHandle tlasDescriptor;
    ScopeExit releaseDescriptors([&]()noexcept{
        for(GpuMesh& mesh : meshes){
            for(const GpuDescriptorHandle descriptor : mesh.descriptors){
                if(descriptor.valid())
                    heap.free(descriptor);
            }
        }
        for(const GpuDescriptorHandle descriptor : inputDescriptors){
            if(descriptor.valid())
                heap.free(descriptor);
        }
        for(const GpuDescriptorHandle descriptor : outputDescriptors){
            if(descriptor.valid())
                heap.free(descriptor);
        }
        if(tlasDescriptor.valid())
            heap.free(tlasDescriptor);
        heap.collectRetired();
    });
    for(u32 index = 0u; index < meshCount; ++index){
        BuildMesh(meshData[index], testCase, testCase.wedge && index == 0u);
        const usize sizes[] = {
            meshData[index].positions.size() * sizeof(Float3U), meshData[index].indices.size() * sizeof(u32),
            meshData[index].attributes.size() * sizeof(Attribute)
        };
        GpuMesh& mesh = meshes[index];
        for(u32 stream = 0u; stream < 3u; ++stream){
            BufferDesc desc;
            desc.setByteSize(sizes[stream]).setCanHaveRawViews(true).setInitialState(ResourceStates::Common).setKeepInitialState(true);
            if(stream < 2u)
                desc.setIsAccelStructBuildInput(true);
            mesh.buffers[stream] = device.createBuffer(desc);
            ASSERT_TRUE(mesh.buffers[stream]);
            mesh.descriptors[stream] = heap.allocate(GpuDescriptorClass::StorageBuffer);
            ASSERT_TRUE(mesh.descriptors[stream].valid());
            ASSERT_TRUE(heap.write(mesh.descriptors[stream], DescriptorWriteItem::RawBuffer_SRV(0u, mesh.buffers[stream].get())));
        }
        RayTracingGeometryTriangles triangles;
        triangles
            .setVertexBuffer(mesh.buffers[0].get())
            .setVertexFormat(Format::RGB32_FLOAT)
            .setVertexStride(sizeof(Float3U))
            .setVertexCount(static_cast<u32>(meshData[index].positions.size()))
            .setIndexBuffer(mesh.buffers[1].get())
            .setIndexFormat(Format::R32_UINT)
            .setIndexCount(static_cast<u32>(meshData[index].indices.size()))
        ;
        mesh.geometry.setTriangles(triangles);
        RayTracingAccelStructDesc desc(memoryArena);
        desc.addBottomLevelGeometry(mesh.geometry);
        mesh.blas = device.createAccelStruct(desc);
        ASSERT_TRUE(mesh.blas);
    }
    Vector<MeshInstance, Alloc::ScratchArena> instances(testCase.instanceCount, scratchArena);
    Vector<Material, Alloc::ScratchArena> materials(testCase.instanceCount, scratchArena);
    Vector<RayTracingInstanceDesc, Alloc::ScratchArena> hardwareInstances(testCase.instanceCount, scratchArena);
    Vector<u32, Alloc::ScratchArena> optical(8u + 4u * testCase.instanceCount, scratchArena);
    optical[3] = testCase.instanceCount;
    optical[7] = NWB_RT_OPTICAL_SCENE_FLAG_BOUNDS_VALID;
    for(u32 index = 0u; index < testCase.instanceCount; ++index){
        const u32 meshIndex = testCase.wedge && index > 0u ? 1u : 0u;
        instances[index].translation.z = static_cast<f32>(index) * testCase.instanceStep;
        instances[index].scale = { testCase.scale.x, testCase.scale.y, testCase.scale.z, 0.0f };
        Material& material = materials[index];
        material.modelId = index & 1u;
        material.meshInstanceIndex = index;
        material.flags = testCase.opaque ? 0u : NWB_RT_INSTANCE_MATERIAL_FLAG_TRANSPARENT;
        material.positionSlot = meshes[meshIndex].descriptors[0].slot();
        material.indexSlot = meshes[meshIndex].descriptors[1].slot();
        material.attributeSlot = meshes[meshIndex].descriptors[2].slot();
        optical[8u + index * 4u] = index + 1u;
        optical[10u + index * 4u] = testCase.closed ? NWB_RT_OPTICAL_BOUNDARY_CLOSED_NESTED : NWB_RT_OPTICAL_BOUNDARY_UNSPECIFIED;
        optical[11u + index * 4u] = NWB_RT_OPTICAL_INSTANCE_FLAG_TRANSPARENT | NWB_RT_OPTICAL_INSTANCE_FLAG_BOUNDS_VALID;
        AffineTransform transform = s_identityTransform;
        transform._11 = testCase.scale.x;
        transform._22 = testCase.scale.y;
        transform._33 = testCase.scale.z;
        transform._34 = instances[index].translation.z;
        hardwareInstances[index]
            .setBLAS(meshes[meshIndex].blas.get())
            .setInstanceID(index)
            .setTransform(transform)
            .setInstanceMask(NWB_RT_OPTICAL_BASE_INSTANCE_MASK | (testCase.opaque ? 0u : NWB_RT_OPTICAL_TRANSPARENT_INSTANCE_MASK))
        ;
    }
    if(testCase.reverseOrder){
        for(usize index = 0u; index < hardwareInstances.size() / 2u; ++index)
            Swap(hardwareInstances[index], hardwareInstances[hardwareInstances.size() - index - 1u]);
    }
    ContextSlots context;
    const u32 typedWords = 0u;
    const TestRay ray{
        { testCase.origin.x, testCase.origin.y, testCase.origin.z, 0.0f }, { 0.0f, 0.0f, 1.0f, testCase.rayLength }
    };
    const void* const inputData[] = { instances.data(), materials.data(), optical.data(), &typedWords, &context, &ray };
    const usize inputSizes[] = {
        instances.size() * sizeof(MeshInstance), materials.size() * sizeof(Material), optical.size() * sizeof(u32),
        sizeof(typedWords), sizeof(context), sizeof(ray)
    };
    BufferHandle inputs[6];
    for(u32 index = 0u; index < LengthOf(inputs); ++index){
        BufferDesc desc;
        desc.setByteSize(inputSizes[index]).setInitialState(ResourceStates::Common).setKeepInitialState(true);
        if(index >= 4u)
            desc.setIsConstantBuffer(true);
        else
            desc.setCanHaveRawViews(true);
        inputs[index] = device.createBuffer(desc);
        ASSERT_TRUE(inputs[index]);
        inputDescriptors[index] = heap.allocate(index >= 4u ? GpuDescriptorClass::UniformBuffer : GpuDescriptorClass::StorageBuffer);
        ASSERT_TRUE(inputDescriptors[index].valid());
        const DescriptorWriteItem item = index >= 4u
            ? DescriptorWriteItem::ConstantBuffer(0u, inputs[index].get())
            : DescriptorWriteItem::RawBuffer_SRV(0u, inputs[index].get())
        ;
        ASSERT_TRUE(heap.write(inputDescriptors[index], item));
    }
    context.scene[2] = inputDescriptors[1].slot();
    context.scene[3] = inputDescriptors[3].slot();
    context.material[0] = inputDescriptors[0].slot();
    context.material[1] = inputDescriptors[2].slot();
    context.material[2] = testCase.instanceCount;
    // No software BVH node or software instance buffers exist in this native fixture.
    const usize outputSizes[] = {
        NWB_HW_TRANSPARENT_WORDS_PER_RAY * sizeof(u32), sizeof(u32),
        NWB_HW_TRANSPARENT_OVERFLOW_ARGS_WORDS * sizeof(u32), sizeof(Observation)
    };
    BufferHandle outputs[4];
    for(u32 index = 0u; index < LengthOf(outputs); ++index){
        BufferDesc desc;
        desc
            .setByteSize(outputSizes[index])
            .setCanHaveRawViews(true)
            .setCanHaveUAVs(true)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
        ;
        if(index == 3u)
            desc.setCpuAccess(CpuAccessMode::Read);
        outputs[index] = device.createBuffer(desc);
        ASSERT_TRUE(outputs[index]);
        outputDescriptors[index] = heap.allocate(GpuDescriptorClass::StorageBuffer);
        ASSERT_TRUE(outputDescriptors[index].valid());
        ASSERT_TRUE(heap.write(outputDescriptors[index], DescriptorWriteItem::RawBuffer_UAV(0u, outputs[index].get())));
    }
    RayTracingAccelStructDesc tlasDesc(memoryArena);
    tlasDesc.setTopLevelMaxInstances(testCase.instanceCount);
    const RayTracingAccelStructHandle tlas = device.createAccelStruct(tlasDesc);
    ASSERT_TRUE(tlas);
    tlasDescriptor = heap.allocate(GpuDescriptorClass::AccelStruct);
    ASSERT_TRUE(tlasDescriptor.valid());
    ASSERT_TRUE(heap.write(tlasDescriptor, DescriptorWriteItem::RayTracingAccelStruct(0u, tlas.get())));
    const CommandListHandle commands = device.createCommandList();
    ASSERT_TRUE(commands);
    commands->open();
    for(u32 index = 0u; index < meshCount; ++index){
        const void* const data[] = { meshData[index].positions.data(), meshData[index].indices.data(), meshData[index].attributes.data() };
        for(u32 stream = 0u; stream < 3u; ++stream){
            const BufferHandle& buffer = meshes[index].buffers[stream];
            ASSERT_TRUE(commands->tryWriteBuffer(*buffer, data[stream], buffer->getDescription().byteSize));
        }
        commands->buildBottomLevelAccelStruct(
            meshes[index].blas.get(), &meshes[index].geometry, 1u, RayTracingAccelStructBuildFlags::None
        );
        for(const BufferHandle& buffer : meshes[index].buffers)
            commands->setBufferState(buffer.get(), ResourceStates::ShaderResource);
    }
    commands->buildTopLevelAccelStruct(
        tlas.get(), hardwareInstances.data(), hardwareInstances.size(), RayTracingAccelStructBuildFlags::None
    );
    commands->setAccelStructState(tlas.get(), ResourceStates::AccelStructRead);
    for(u32 index = 0u; index < LengthOf(inputs); ++index){
        ASSERT_TRUE(commands->tryWriteBuffer(*inputs[index], inputData[index], inputSizes[index]));
        commands->setBufferState(inputs[index].get(), index >= 4u ? ResourceStates::ConstantBuffer : ResourceStates::ShaderResource);
    }
    u32 scratch[NWB_HW_TRANSPARENT_WORDS_PER_RAY]{};
    const u32 overflowList = 0xffffffffu;
    const u32 overflowArgs[NWB_HW_TRANSPARENT_OVERFLOW_ARGS_WORDS] = { 0u, 1u, 1u, 0u };
    Observation sentinel;
    NWB_MEMSET(&sentinel, 0xa5, sizeof(sentinel));
    const void* const initial[] = { scratch, &overflowList, overflowArgs, &sentinel };
    for(u32 index = 0u; index < LengthOf(outputs); ++index){
        ASSERT_TRUE(commands->tryWriteBuffer(*outputs[index], initial[index], outputSizes[index]));
        commands->setBufferState(outputs[index].get(), ResourceStates::UnorderedAccess, true);
    }
    PushConstants push;
    push.deferredResourcesHeapSlot = inputDescriptors[5].slot();
    push.materialContextSlotsHeapSlot = inputDescriptors[4].slot();
    push.crossingsHeapSlot = outputDescriptors[0].slot();
    push.overflowListHeapSlot = outputDescriptors[1].slot();
    push.overflowArgsHeapSlot = outputDescriptors[2].slot();
    push.outputStorageSlot = outputDescriptors[3].slot();
    commands->commitBarriers();
    commands->setComputeState(ComputeState().setPipeline(&collectPipeline));
    heap.bindCompute(*commands, collectPipeline, tlasDescriptor);
    commands->setPushConstants(&push, sizeof(push));
    commands->dispatch(1u, 1u, 1u);
    for(u32 index = 0u; index < 3u; ++index)
        commands->setBufferState(outputs[index].get(), ResourceStates::UnorderedAccess, true);
    commands->commitBarriers();
    commands->setComputeState(ComputeState().setPipeline(&evaluatePipeline));
    heap.bindCompute(*commands, evaluatePipeline, tlasDescriptor);
    commands->setPushConstants(&push, sizeof(push));
    commands->dispatch(1u, 1u, 1u);
    ASSERT_FALSE(commands->commandRecordingFailed());
    commands->close();
    ASSERT_FALSE(commands->commandRecordingFailed());
    CommandList* const commandLists[] = { commands.get() };
    const QueueSubmissionToken token = device.executeCommandLists(
        commandLists, LengthOf(commandLists), commands->getDescription().physicalQueue, QueueSubmissionDesc{}
    );
    ASSERT_TRUE(token.valid());
    ASSERT_TRUE(device.waitForIdle());
    const Observation* const actual = static_cast<const Observation*>(device.mapBuffer(*outputs[3], CpuAccessMode::Read));
    ASSERT_NE(actual, nullptr);
    ScopeExit unmap([&]()noexcept{ device.unmapBuffer(*outputs[3]); });
    const Float3U expected = AnalyticTransmission(testCase);
    for(u32 channel = 0u; channel < 3u; ++channel){
        EXPECT_TRUE(IsFinite(actual->selected[channel]));
        EXPECT_TRUE(IsFinite(actual->continuation[channel]));
        EXPECT_NEAR(actual->selected[channel], expected.raw[channel], 0.002f);
        EXPECT_NEAR(actual->continuation[channel], expected.raw[channel], 0.002f);
    }
    const bool overflow = testCase.instanceCount * testCase.shellCount * 2u > NWB_HW_TRANSPARENT_CROSSING_CAPACITY;
    EXPECT_EQ(actual->usedContinuation, overflow ? 1u : 0u);
    EXPECT_EQ(actual->overflowCount, overflow ? 1u : 0u);
    EXPECT_EQ(actual->overflowGroups, overflow ? 1u : 0u);
    EXPECT_EQ(actual->overflowPixel, overflow ? 0u : 0xffffffffu);
    if(overflow)
        EXPECT_EQ(actual->crossingCount, NWB_HW_TRANSPARENT_OVERFLOW_COUNT);
    else if(testCase.opaque || testCase.origin.x > 2.0f)
        EXPECT_EQ(actual->crossingCount, 0u);
    else
        EXPECT_GE(actual->crossingCount, 1u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class HardwareTransmissionKernelTest : public ShadowKernelTest{
protected:
    virtual void SetUp()override;
    [[nodiscard]] bool loadKernel(AStringView name, Alloc::ScratchArena& scratchArena, ComputePipelineHandle& outPipeline);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void HardwareTransmissionKernelTest::SetUp(){
    if(
        !device().queryFeatureSupport(Feature::RayTracingAccelStruct)
        || !device().queryFeatureSupport(Feature::RayQuery) || !device().getDescriptorHeap().hasAccelStructLayout()
    )
        GTEST_SKIP() << "Native transparent shadow tests require acceleration structures and hardware ray queries.";
}

bool HardwareTransmissionKernelTest::loadKernel(
    const AStringView name,
    Alloc::ScratchArena& scratchArena,
    ComputePipelineHandle& outPipeline){
    auto& graphicsDevice = device();
    auto& heap = graphicsDevice.getDescriptorHeap();
    auto& memoryArena = arena();
    const Path sourceRoot(memoryArena, NWB_SHADOW_KERNEL_SOURCE_ROOT);
    const Path kernelRoot = sourceRoot / "tests/smoke/shadow_kernel/assets";
    Path sourcePath = kernelRoot / name;
    sourcePath.replace_extension(".slang");
    Path metadataPath = kernelRoot / name;
    metadataPath.replace_extension(".nwb");
    const Path outputRoot(memoryArena, NWB_SHADOW_KERNEL_OUTPUT_ROOT);
    ErrorCode directoryError;
    if(!CreateDirectories(outputRoot, directoryError) && directoryError)
        return false;
    Path outputPath = outputRoot / name;
    outputPath.replace_extension(".spv");
    Impl::ShaderCook shaderCook(memoryArena);
    Impl::ShaderCook::ShaderEntry entry(memoryArena);
    Impl::ShaderCook::CookVector<u8> bytecode(memoryArena);
    {
        const Common::LoggerRegistrationGuard cookLogger(*s_logger, Common::LoggerBreakPolicy::BreakOnFatal);
        const bool cooked = [&]{
            if(!shaderCook.parseShaderMeta(metadataPath, entry, scratchArena))
                return false;
            Impl::ShaderCook::CookVector<Path> includes(memoryArena);
            includes.push_back(kernelRoot);
            includes.push_back(sourceRoot / "impl/assets/graphics");
            const Impl::AssetsCsgCook::CsgShapeCookEntryVector noShapes(memoryArena);
            Path generatedCsgRoot(memoryArena);
            if(!Impl::AssetsCsgCook::EmitCsgShapeModuleIncludes(
                outputRoot, "hardware_transmission", noShapes, generatedCsgRoot, scratchArena
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
                .variantName = "default",
                .defines = nullptr,
                .includeDirectories = includes,
                .dependencies = dependencies,
                .sourcePath = sourcePath,
                .outputPath = outputPath,
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
    layoutDesc.setVisibility(ShaderType::Compute).addItem(BindingLayoutItem::PushConstants(0u, NWB_HW_TRANSPARENT_PUSH_CONSTANT_BYTES));
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


TEST_F(HardwareTransmissionKernelTest, ClosedVolumesOverlapAndCoincideWithoutCrossInstancePairing){
    using namespace __hidden_hardware_transmission_kernel_tests;
    Alloc::ScratchArena scratchArena(Name("tests/smoke/shadow_kernel/hardware_volumes"));
    ComputePipelineHandle collect;
    ComputePipelineHandle evaluate;
    ASSERT_TRUE(loadKernel("hardware_transmission_collect_cs", scratchArena, collect));
    ASSERT_TRUE(loadKernel("hardware_transmission_evaluate_cs", scratchArena, evaluate));
    const Case cases[] = {
        { .name = "single volume" },
        { .name = "separated tinted volumes", .instanceCount = 2u, .instanceStep = 3.0f },
        { .name = "overlapping tinted volumes", .instanceCount = 2u, .instanceStep = 0.5f },
        { .name = "coincident tinted instances", .instanceCount = 2u },
        { .name = "reversed candidates and instances", .instanceCount = 2u, .instanceStep = 0.5f, .reverseOrder = true },
        { .name = "four independent volumes", .instanceCount = 4u, .instanceStep = 2.0f },
        { .name = "nonuniform world scale", .scale = { 2.0f, 0.5f, 3.0f } },
        { .name = "mirrored world scale", .scale = { -2.0f, 0.5f, 3.0f } },
        { .name = "miss", .origin = { 4.0f, 0.25f, 0.0f } },
        { .name = "opaque mask excluded", .opaque = true },
    };
    for(const Case& testCase : cases)
        ASSERT_NO_FATAL_FAILURE(RunCase(device(), arena(), *collect, *evaluate, testCase, scratchArena));
}

TEST_F(HardwareTransmissionKernelTest, EdgeClustersAndDuplicatesPreserveTheOtherVolume){
    using namespace __hidden_hardware_transmission_kernel_tests;
    Alloc::ScratchArena scratchArena(Name("tests/smoke/shadow_kernel/hardware_edges"));
    ComputePipelineHandle collect;
    ComputePipelineHandle evaluate;
    ASSERT_TRUE(loadKernel("hardware_transmission_collect_cs", scratchArena, collect));
    ASSERT_TRUE(loadKernel("hardware_transmission_evaluate_cs", scratchArena, evaluate));
    const Case cases[] = {
        { .name = "shared front and back triangle diagonals", .origin = { 0.25f, 0.25f, 0.0f } },
        { .name = "duplicate same-facing boundary", .duplicateBoundary = true },
        { .name = "duplicate reversed primitive order", .reverseOrder = true, .duplicateBoundary = true },
        {
            .name = "opposite-facing tangent over B", .instanceCount = 2u, .instanceStep = 3.0f,
            .origin = { 0.0f, 0.25f, 0.0f }, .wedge = true
        },
        {
            .name = "outside A edge over B", .instanceCount = 2u, .instanceStep = 3.0f,
            .origin = { -0.001f, 0.25f, 0.0f }, .wedge = true
        },
        {
            .name = "thin A chord over B", .instanceCount = 2u, .instanceStep = 3.0f,
            .origin = { 0.001f, 0.25f, 0.0f }, .wedge = true
        },
    };
    for(const Case& testCase : cases)
        ASSERT_NO_FATAL_FAILURE(RunCase(device(), arena(), *collect, *evaluate, testCase, scratchArena));
}

TEST_F(HardwareTransmissionKernelTest, AuthoredClosedOriginsAndHardwareContinuationCompleteLongRays){
    using namespace __hidden_hardware_transmission_kernel_tests;
    Alloc::ScratchArena scratchArena(Name("tests/smoke/shadow_kernel/hardware_continuation"));
    ComputePipelineHandle collect;
    ComputePipelineHandle evaluate;
    ASSERT_TRUE(loadKernel("hardware_transmission_collect_cs", scratchArena, collect));
    ASSERT_TRUE(loadKernel("hardware_transmission_evaluate_cs", scratchArena, evaluate));
    const Case cases[] = {
        { .name = "closed origin inside", .origin = { 0.125f, 0.25f, 1.5f } },
        { .name = "finite ray ends inside closed volume", .rayLength = 1.5f },
        { .name = "unspecified retains legacy singleton", .origin = { 0.125f, 0.25f, 1.5f }, .closed = false },
        { .name = "eighteen boundaries complete on hardware", .shellCount = 9u },
        { .name = "seventeen boundaries from interior", .shellCount = 9u, .origin = { 0.125f, 0.25f, 1.5f } },
        { .name = "eight overlapping instances exceed scratch", .instanceCount = 8u, .instanceStep = 0.125f },
    };
    for(const Case& testCase : cases)
        ASSERT_NO_FATAL_FAILURE(RunCase(device(), arena(), *collect, *evaluate, testCase, scratchArena));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

