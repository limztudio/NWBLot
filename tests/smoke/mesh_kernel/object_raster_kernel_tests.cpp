// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "object_geometry_kernel_fixture.h"

#include <impl/assets/graphics/mesh/object_geometry_constants.h>
#include <impl/assets/graphics/mesh/runtime_constants.h>
#include <impl/assets_mesh/meshlet_payload_packing.h>
#include <impl/assets_shader/cook.h>
#include <impl/ecs_render/mesh/renderer_mesh_types.h>

#include <global/algorithm.h>
#include <global/filesystem.h>
#include <global/math/convert.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_object_raster_kernel_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_Width = 32u;
constexpr u32 s_Height = 24u;
constexpr u32 s_TargetCount = 4u;
constexpr u32 s_VertexCount = 6u;
constexpr u32 s_IndexByteOffset = (s_VertexCount + 1u) * NWB_MESH_OBJECT_VERTEX_BYTE_SIZE;
constexpr u32 s_CacheBytes = AlignUp(s_IndexByteOffset + 6u * static_cast<u32>(sizeof(u32)), NWB_MESH_OBJECT_VERTEX_BYTE_SIZE);

struct Half4{ u16 x; u16 y; u16 z; u16 w; };
struct View{
    f32 worldToClip[16]{};
    f32 clipToWorld[16]{};
    f32 camera[4]{};
    f32 planes[NWB_MESH_VIEW_FRUSTUM_PLANE_COUNT][4]{};
};
struct Push{
    u32 dispatch[4]{};
    f32 viewport[4]{};
    f32 scissor[4]{};
    u32 slots[4]{};
};
struct DecodePush{ Push mesh; u32 output[4]{}; };
namespace Projection{ enum Enum : u8{ Identity, NearCrossing, TinyPositiveW, ZeroW, NegativeW, AllClipped, ChangedView }; };
struct Case{
    Projection::Enum projection = Projection::Identity;
    bool transformed = false;
    bool mirrored = false;
    bool twoSided = true;
    bool scissor = false;
};
struct Inputs{
    Float3U positions[4]{};
    Half4 normals[6]{};
    Half4 tangents[6]{};
    Float2U uvs[6]{};
    Half4 colors[6]{};
    Impl::MeshletDesc meshlet{};
    Impl::MeshletBounds bounds{};
    u8 positionRefs[8]{};
    u8 attributeRefs[24]{};
    Impl::MeshletLocalVertexRef localRefs[6]{};
    u8 indices[8]{ 0u, 1u, 2u, 3u, 4u, 5u, 0u, 0u };
    Impl::InstanceGpuData instances[2]{};
    View view;
    Push push;
};

static_assert(sizeof(Push) == NWB_MESH_PUSH_CONSTANT_BYTE_SIZE);
static_assert(sizeof(DecodePush) == NWB_MESH_COMPUTE_PUSH_CONSTANT_BYTE_SIZE);
static_assert(sizeof(View) == NWB_MESH_VIEW_FLOAT_COUNT * sizeof(f32));


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Half4 Pack(const f32 x, const f32 y, const f32 z, const f32 w){
    return { ConvertFloatToHalf(x), ConvertFloatToHalf(y), ConvertFloatToHalf(z), ConvertFloatToHalf(w) };
}

void BuildInputs(const Case& testCase, Inputs& input){
    input.positions[0] = Float3U(-0.75f, -0.75f, 0.5f);
    input.positions[1] = Float3U(0.75f, -0.75f, 0.5f);
    input.positions[2] = Float3U(0.75f, 0.75f, 0.5f);
    input.positions[3] = Float3U(-0.75f, 0.75f, 0.5f);
    constexpr u16 positions[] = { 0u, 1u, 2u, 0u, 2u, 3u };
    for(u32 vertex = 0u; vertex < 6u; ++vertex){
        input.localRefs[vertex] = { positions[vertex], static_cast<u16>(vertex) };
        input.normals[vertex] = vertex < 3u ? Pack(0.0f, 0.0f, 1.0f, 0.0f) : Pack(0.0f, 1.0f, 0.0f, 0.0f);
        input.tangents[vertex] = Pack(1.0f, 0.0f, 0.0f, vertex < 3u ? 1.0f : -1.0f);
        input.uvs[vertex] = Float2U(vertex < 3u ? 0.25f : 2.25f, static_cast<f32>(vertex % 3u) * 0.25f);
        input.colors[vertex] = Pack(static_cast<f32>(vertex) * 0.125f, 0.25f, 0.75f, 1.0f);
        for(u32 channel = 0u; channel < 4u; ++channel)
            input.attributeRefs[channel * 6u + vertex] = static_cast<u8>(vertex);
    }
    for(u32 position = 0u; position < 4u; ++position)
        input.positionRefs[position] = static_cast<u8>(position);
    input.meshlet.counts = Impl::PackMeshletCounts(6u, 2u, 4u, 6u);
    input.meshlet.encoding = Impl::PackMeshletRefEncoding(Impl::MeshletRefDeltaWidth::U8, Impl::MeshletRefDeltaWidth::U8,
        Impl::MeshletRefDeltaWidth::U8, Impl::MeshletRefDeltaWidth::U8, Impl::MeshletRefDeltaWidth::U8, Impl::MeshletRefDeltaWidth::U8);
    input.bounds.sphere = Float4U(0.0f, 0.0f, 0.5f, 2.0f);
    for(u32 diagonal = 0u; diagonal < 4u; ++diagonal){
        input.view.worldToClip[diagonal * 5u] = 1.0f;
        input.view.clipToWorld[diagonal * 5u] = 1.0f;
    }
    if(testCase.projection == Projection::NearCrossing){
        input.positions[0].z = -0.25f;
        input.positions[3].z = -0.25f;
    }
    if(testCase.projection >= Projection::TinyPositiveW && testCase.projection <= Projection::NegativeW){
        input.view.worldToClip[10] = 0.5f;
        input.view.worldToClip[14] = 1.0f;
        input.view.worldToClip[15] = 0.0f;
        input.positions[0].z = testCase.projection == Projection::TinyPositiveW ? 0.000001f
            : testCase.projection == Projection::ZeroW ? 0.0f : -0.125f;
        input.positions[1].z = 1.0f;
        input.positions[2].z = 1.0f;
        input.positions[3].z = 1.0f;
    }
    if(testCase.projection == Projection::AllClipped){
        for(auto& position : input.positions)
            position.x += 4.0f;
    }
    if(testCase.projection == Projection::ChangedView){
        input.view.worldToClip[0] = 0.75f;
        input.view.worldToClip[5] = 0.625f;
        input.view.worldToClip[3] = 0.125f;
        input.view.worldToClip[7] = -0.0625f;
    }
    input.instances[0].translation.x = 128.0f;
    if(testCase.transformed){
        input.instances[1].rotation = Float4(0.0f, 0.0f, 0.25881904f, 0.9659258f);
        input.instances[1].translation = Float3UInt(0.125f, -0.0625f, 0.03125f, 0u);
        input.instances[1].scale = Float4(testCase.mirrored ? -0.625f : 0.625f, 1.125f, 0.75f, 0.0f);
    }
    input.push.dispatch[0] = 1u;
    input.push.dispatch[1] = 1u;
    input.push.dispatch[3] = testCase.scissor ? NWB_MESH_DISPATCH_FLAG_SCISSOR_CULL : 0u;
    input.push.viewport[2] = static_cast<f32>(s_Width);
    input.push.viewport[3] = static_cast<f32>(s_Height);
    input.push.scissor[0] = testCase.scissor ? 8.0f : 0.0f;
    input.push.scissor[1] = testCase.scissor ? 4.0f : 0.0f;
    input.push.scissor[2] = testCase.scissor ? 24.0f : static_cast<f32>(s_Width);
    input.push.scissor[3] = testCase.scissor ? 20.0f : static_cast<f32>(s_Height);
}

[[nodiscard]] bool LoadRasterShaders(
    GraphicsBackend::Device& device,
    Alloc::GlobalArena& arena,
    Alloc::ScratchArena& scratch,
    ShaderHandle& legacyVertex,
    ShaderHandle& pixel){
    const Path sourceRoot(arena, NWB_MESH_KERNEL_SOURCE_ROOT);
    const Path graphicsRoot = sourceRoot / "impl/assets/graphics";
    const Path outputRoot(arena, NWB_MESH_KERNEL_OUTPUT_ROOT);
    const Path sources[] = {
        graphicsRoot / "mesh/emulation_vs.slang",
        sourceRoot / "tests/smoke/mesh_kernel/assets/object_raster_ps.slang"
    };
    const Path outputs[] = { outputRoot / "object_raster_reference_vs.spv", outputRoot / "object_raster_ps.spv" };
    ShaderHandle* const shaders[] = { &legacyVertex, &pixel };
    Impl::ShaderCook cook(arena);
    Impl::ShaderCook::CookVector<Path> includes(arena);
    includes.push_back(graphicsRoot);
    for(u32 index = 0u; index < 2u; ++index){
        Impl::ShaderCook::CookVector<Path> dependencies(arena);
        if(!cook.gatherShaderDependencies(sources[index], includes, dependencies, scratch))
            return false;
        const Impl::ShaderCook::ShaderCompilerRequest request{
            .shaderName = index == 0u ? "object_raster_reference_vs" : "object_raster_ps",
            .stage = index == 0u ? "vs" : "ps",
            .targetProfile = "spirv_1_5", .entryPoint = "main", .variantName = "default",
            .includeDirectories = includes, .dependencies = dependencies,
            .sourcePath = sources[index], .outputPath = outputs[index]
        };
        Impl::ShaderCook::CookVector<u8> code(arena);
        if(!cook.compileVariant(request, code) || code.empty())
            return false;
        ShaderDesc desc(arena);
        desc.setShaderType(index == 0u ? ShaderType::Vertex : ShaderType::Pixel).setEntryName("main");
        *shaders[index] = device.createShader(desc, code.data(), code.size());
        if(!*shaders[index])
            return false;
    }
    return true;
}

void RunCase(
    GraphicsBackend::Device& device,
    Alloc::GlobalArena& arena,
    ComputePipeline& reference,
    const ObjectGeometryKernels& object,
    const ShaderHandle& legacyVertex,
    const ShaderHandle& pixelShader,
    const Case& testCase){
    SCOPED_TRACE(static_cast<u32>(testCase.projection));
    SCOPED_TRACE(testCase.transformed);
    SCOPED_TRACE(testCase.mirrored);
    SCOPED_TRACE(testCase.twoSided);
    SCOPED_TRACE(testCase.scissor);
    Inputs input;
    BuildInputs(testCase, input);
    auto& heap = device.getDescriptorHeap();
    const void* const sources[] = { input.positions, input.normals, input.tangents, input.uvs, input.colors, &input.meshlet,
        input.positions, &input.bounds, input.positionRefs, input.attributeRefs, input.localRefs, input.indices, input.instances, &input.view };
    const usize sizes[] = { sizeof(input.positions), sizeof(input.normals), sizeof(input.tangents), sizeof(input.uvs),
        sizeof(input.colors), sizeof(input.meshlet), sizeof(input.positions), sizeof(input.bounds), sizeof(input.positionRefs),
        sizeof(input.attributeRefs), sizeof(input.localRefs), sizeof(input.indices), sizeof(input.instances), sizeof(input.view) };
    BufferHandle buffers[16];
    GpuDescriptorHandle descriptors[16]{};
    ScopeExit release([&]()noexcept{
        for(const auto& descriptor : descriptors){
            if(descriptor.valid())
                heap.free(descriptor);
        }
        heap.collectRetired();
    });
    for(u32 index = 0u; index < 16u; ++index){
        BufferDesc desc;
        desc.setInitialState(ResourceStates::Common).setKeepInitialState(true);
        if(index < 14u){
            desc.setByteSize(sizes[index]);
            if(index == 13u)
                desc.setIsConstantBuffer(true);
            else
                desc.setCanHaveRawViews(true);
        }
        else{
            desc.setByteSize(index == 14u ? 6u * NWB_MESH_EMULATION_VERTEX_BYTE_SIZE : s_CacheBytes)
                .setStructStride(index == 14u ? NWB_MESH_EMULATION_VERTEX_BYTE_SIZE : NWB_MESH_OBJECT_VERTEX_BYTE_SIZE)
                .setIsVertexBuffer(true).setIsIndexBuffer(index == 15u).setCanHaveRawViews(true).setCanHaveUAVs(true);
        }
        buffers[index] = device.createBuffer(desc);
        ASSERT_TRUE(buffers[index]);
        descriptors[index] = heap.allocate(index == 13u ? GpuDescriptorClass::UniformBuffer : GpuDescriptorClass::StorageBuffer);
        ASSERT_TRUE(descriptors[index].valid());
        const DescriptorWriteItem item = index == 13u ? DescriptorWriteItem::ConstantBuffer(0u, buffers[index].get())
            : index < 14u ? DescriptorWriteItem::RawBuffer_SRV(0u, buffers[index].get())
            : DescriptorWriteItem::RawBuffer_UAV(0u, buffers[index].get());
        ASSERT_TRUE(heap.write(descriptors[index], item));
    }
    for(auto& instance : input.instances){
        for(u32 slot = 0u; slot < NWB_MESH_INSTANCE_GEOMETRY_SLOT_COUNT; ++slot)
            instance.geometryHeapSlots[slot] = descriptors[slot].slot();
    }
    input.push.slots[0] = descriptors[12].slot();
    input.push.slots[1] = descriptors[6].slot();
    input.push.slots[2] = descriptors[13].slot();
    TextureDesc targetDesc;
    targetDesc.setWidth(s_Width).setHeight(s_Height).setFormat(Format::RGBA32_FLOAT).setInRenderTarget(true)
        .setInitialState(ResourceStates::Common).setKeepInitialState(true);
    TextureHandle targets[2][s_TargetCount];
    StagingTextureHandle readbacks[2][s_TargetCount];
    FramebufferHandle framebuffers[2];
    GraphicsPipelineHandle pipelines[2];
    BindingLayoutDesc layoutDesc(arena);
    layoutDesc.setVisibility(ShaderType::All).addItem(BindingLayoutItem::PushConstants(0u, sizeof(Push)));
    const BindingLayoutHandle layout = device.createBindingLayout(layoutDesc);
    ASSERT_TRUE(layout);
    const ShaderHandle vertices[] = { legacyVertex, object.vertex };
    constexpr Format::Enum formats[] = { Format::RGBA32_FLOAT, Format::RGBA16_FLOAT, Format::RGBA16_FLOAT,
        Format::RG32_FLOAT, Format::RGBA16_FLOAT, Format::RGBA32_FLOAT };
    constexpr u32 offsets[] = { 0u, 16u, 24u, 32u, 40u, 48u };
    const Name names[] = { Name("POSITION"), Name("NORMAL"), Name("TANGENT"), Name("TEXCOORD"), Name("COLOR"), Name("POSITION1") };
    for(u32 arm = 0u; arm < 2u; ++arm){
        FramebufferDesc framebufferDesc;
        for(u32 target = 0u; target < s_TargetCount; ++target){
            targets[arm][target] = device.createTexture(targetDesc);
            readbacks[arm][target] = device.createStagingTexture(targetDesc, CpuAccessMode::Read);
            ASSERT_TRUE(targets[arm][target]);
            ASSERT_TRUE(readbacks[arm][target]);
            framebufferDesc.addColorAttachment(targets[arm][target].get());
        }
        framebuffers[arm] = device.createFramebuffer(framebufferDesc);
        ASSERT_TRUE(framebuffers[arm]);
        VertexAttributeDesc attributes[6];
        const u32 attributeCount = arm == 0u ? 6u : 5u;
        for(u32 index = 0u; index < attributeCount; ++index){
            attributes[index].setName(names[index]).setFormat(formats[index]).setOffset(offsets[index])
                .setElementStride(arm == 0u ? NWB_MESH_EMULATION_VERTEX_BYTE_SIZE : NWB_MESH_OBJECT_VERTEX_BYTE_SIZE);
        }
        const InputLayoutHandle inputLayout = device.createInputLayout(attributes, attributeCount, vertices[arm].get());
        ASSERT_TRUE(inputLayout);
        RasterState raster;
        raster.setCullMode(testCase.twoSided ? RasterCullMode::None : RasterCullMode::Back).setFrontCounterClockwise(false)
            .enableDepthClip().enableScissor();
        DepthStencilState depth;
        depth.disableDepthTest().disableDepthWrite();
        RenderState render;
        render.setRasterState(raster).setDepthStencilState(depth);
        GraphicsPipelineDesc desc;
        desc.setInputLayout(inputLayout).setVertexShader(vertices[arm]).setPixelShader(pixelShader).setRenderState(render)
            .addBindingLayout(layout).addBindingLayout(heap.getResourceLayout()).addBindingLayout(heap.getSamplerLayout());
        pipelines[arm] = device.createGraphicsPipeline(desc, FramebufferInfo(framebufferDesc));
        ASSERT_TRUE(pipelines[arm]);
    }
    const CommandListHandle commands = device.createCommandList();
    ASSERT_TRUE(commands);
    commands->open();
    for(u32 index = 0u; index < 14u; ++index){
        ASSERT_TRUE(commands->tryWriteBuffer(*buffers[index], sources[index], sizes[index]));
        commands->setBufferState(buffers[index].get(), index == 13u ? ResourceStates::ConstantBuffer : ResourceStates::ShaderResource);
    }
    for(u32 arm = 0u; arm < 2u; ++arm){
        commands->setBufferState(buffers[14u + arm].get(), ResourceStates::UnorderedAccess, true);
        commands->commitBarriers();
        ComputePipeline& pipeline = arm == 0u ? reference : *object.decode;
        commands->setComputeState(ComputeState{}.setPipeline(&pipeline));
        heap.bindCompute(*commands, pipeline);
        input.push.slots[3] = descriptors[14u + arm].slot();
        if(arm == 0u)
            commands->setPushConstants(&input.push, sizeof(input.push));
        else{
            const DecodePush decode{ input.push, { s_IndexByteOffset, 0u, 0u, 0u } };
            commands->setPushConstants(&decode, sizeof(decode));
        }
        commands->dispatch(1u, 1u, 1u);
        commands->setBufferState(buffers[14u + arm].get(), arm == 0u ? ResourceStates::VertexBuffer
            : ResourceStates::VertexBuffer | ResourceStates::IndexBuffer);
    }
    commands->commitBarriers();
    for(u32 arm = 0u; arm < 2u; ++arm){
        RenderPassParameters clear;
        for(u32 target = 0u; target < s_TargetCount; ++target)
            clear.colorAttachmentActions[target].loadAction = RenderPassLoadAction::Clear;
        commands->beginRenderPass(*framebuffers[arm], clear);
        ViewportState viewport;
        viewport.addViewport(Viewport(static_cast<f32>(s_Width), static_cast<f32>(s_Height)));
        viewport.addScissorRect(Rect(static_cast<i32>(input.push.scissor[0]), static_cast<i32>(input.push.scissor[2]),
            static_cast<i32>(input.push.scissor[1]), static_cast<i32>(input.push.scissor[3])));
        GraphicsState state;
        state.setPipeline(pipelines[arm].get()).setFramebuffer(framebuffers[arm].get()).setViewport(viewport)
            .addVertexBuffer(VertexBufferBinding{}.setBuffer(buffers[14u + arm].get()));
        if(arm != 0u)
            state.setIndexBuffer(IndexBufferBinding{}.setBuffer(buffers[15].get()).setOffset(s_IndexByteOffset).setFormat(Format::R32_UINT));
        commands->setGraphicsState(state);
        heap.bindGraphics(*commands, *pipelines[arm]);
        commands->setPushConstants(&input.push, sizeof(input.push));
        const DrawArguments draw = DrawArguments{}.setVertexCount(6u);
        if(arm == 0u)
            commands->draw(draw);
        else
            commands->drawIndexed(draw);
        commands->endRenderPass();
        for(u32 target = 0u; target < s_TargetCount; ++target)
            commands->copyTexture(*readbacks[arm][target], TextureSlice{}, *targets[arm][target], TextureSlice{});
    }
    ASSERT_FALSE(commands->commandRecordingFailed());
    commands->close();
    ASSERT_FALSE(commands->commandRecordingFailed());
    CommandList* const lists[] = { commands.get() };
    ASSERT_TRUE(device.executeCommandLists(lists, LengthOf(lists), commands->getDescription().physicalQueue, QueueSubmissionDesc{}).valid());
    ASSERT_TRUE(device.waitForIdle());
    u32 covered = 0u;
    bool firstSeam = false;
    bool secondSeam = false;
    for(u32 target = 0u; target < s_TargetCount; ++target){
        const u8* mapped[2]{};
        usize pitches[2]{};
        ScopeExit unmap([&]()noexcept{
            for(u32 arm = 0u; arm < 2u; ++arm){
                if(mapped[arm] != nullptr)
                    device.unmapStagingTexture(*readbacks[arm][target]);
            }
        });
        for(u32 arm = 0u; arm < 2u; ++arm){
            mapped[arm] = static_cast<const u8*>(device.mapStagingTexture(*readbacks[arm][target], TextureSlice{}, CpuAccessMode::Read, &pitches[arm]));
            ASSERT_NE(mapped[arm], nullptr);
        }
        for(u32 y = 0u; y < s_Height; ++y){
            for(u32 x = 0u; x < s_Width; ++x){
                SCOPED_TRACE(target);
                SCOPED_TRACE(x);
                SCOPED_TRACE(y);
                Float4U values[2];
                for(u32 arm = 0u; arm < 2u; ++arm)
                    NWB_MEMCPY(&values[arm], sizeof(Float4U), mapped[arm] + y * pitches[arm] + x * sizeof(Float4U), sizeof(Float4U));
                if(target == 0u){
                    // Coverage is exact; tolerance is only for FP32 varying interpolation across shader stages.
                    EXPECT_EQ(values[0].w, values[1].w);
                    if(values[0].w == 1.0f)
                        ++covered;
                }
                if(target == 2u){
                    firstSeam |= Abs(values[1].x - 0.25f) < 0.00001f;
                    secondSeam |= Abs(values[1].x - 2.25f) < 0.00001f;
                }
                for(u32 channel = 0u; channel < 4u; ++channel){
                    EXPECT_TRUE(IsFinite(values[0].raw[channel]));
                    EXPECT_TRUE(IsFinite(values[1].raw[channel]));
                    const f32 tolerance = 0.00002f * Max(1.0f, Abs(values[0].raw[channel]));
                    EXPECT_NEAR(values[0].raw[channel], values[1].raw[channel], tolerance);
                }
            }
        }
    }
    if(testCase.projection == Projection::AllClipped)
        EXPECT_EQ(covered, 0u);
    else if(testCase.twoSided)
        EXPECT_GT(covered, 0u);
    if(testCase.projection == Projection::Identity && !testCase.transformed && testCase.twoSided && !testCase.scissor){
        EXPECT_TRUE(firstSeam);
        EXPECT_TRUE(secondSeam);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(MeshKernelTest, PersistentIndexedRasterMatchesExpandedVertexShaderCoverageAndVaryings){
    using namespace __hidden_object_raster_kernel_tests;
    const Common::LoggerRegistrationGuard diagnosticLogger(*s_logger, Common::LoggerBreakPolicy::BreakOnFatal);
    const auto run = [&](){
        Alloc::ScratchArena scratch(Name("tests/smoke/mesh_kernel/object_raster"));
        ComputePipelineHandle reference;
        ObjectGeometryKernels object;
        ShaderHandle legacyVertex;
        ShaderHandle pixel;
        ASSERT_TRUE(loadMeshKernel(false, scratch, reference));
        ASSERT_TRUE(LoadObjectGeometryKernels(device(), arena(), scratch, object));
        ASSERT_TRUE(LoadRasterShaders(device(), arena(), scratch, legacyVertex, pixel));
        for(u32 projection = Projection::Identity; projection <= Projection::ChangedView; ++projection){
            const Case testCase{ static_cast<Projection::Enum>(projection) };
            ASSERT_NO_FATAL_FAILURE(RunCase(device(), arena(), *reference, object, legacyVertex, pixel, testCase));
        }
        for(const bool mirrored : { false, true }){
            for(const bool twoSided : { false, true }){
                const Case testCase{ Projection::Identity, true, mirrored, twoSided };
                ASSERT_NO_FATAL_FAILURE(RunCase(device(), arena(), *reference, object, legacyVertex, pixel, testCase));
            }
        }
        const Case scissor{ Projection::Identity, false, false, true, true };
        ASSERT_NO_FATAL_FAILURE(RunCase(device(), arena(), *reference, object, legacyVertex, pixel, scissor));
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

