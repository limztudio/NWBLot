// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "light_space_kernel_fixture.h"

#include <impl/assets/graphics/shadow/constants.h>

#include <global/algorithm.h>
#include <global/math/convert.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_light_space_kernel_fixture{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace BufferIndex{
    enum Enum : u32{
        Scene, Mesh, Positions, Indices, Attributes, Instances, Transforms, Materials,
        Context, Deferred, Shading, Lights, Optical, Views, Counts, Events, Observation, DrawArguments, Count
    };
};
namespace TextureIndex{ enum Enum : u32{ Position, Normal, Depth, Capture, Output, Count }; };

static_assert(sizeof(LightSpaceKernel::CaptureDraw) == NWB_LIGHT_SPACE_DRAW_ARGUMENT_BYTES);
static_assert(sizeof(LightSpaceKernel::Push) == NWB_LIGHT_SPACE_PUSH_BYTES);
static_assert(sizeof(LightSpaceKernel::View) == NWB_LIGHT_SPACE_VIEW_BYTES);
static_assert(sizeof(LightSpaceKernel::Event) == NWB_LIGHT_SPACE_EVENT_BYTES);
static_assert(offsetof(LightSpaceKernel::View, map) == 96u);
static_assert(offsetof(LightSpaceKernel::View, light) == 112u);
static_assert(offsetof(LightSpaceKernel::Push, sceneRootSlot) == 56u);
static_assert(offsetof(LightSpaceKernel::Push, viewCount) == 60u);
static_assert(sizeof(LightSpaceKernel::Instance) == 64u);
static_assert(sizeof(LightSpaceKernel::Material) == 36u);
static_assert(sizeof(LightSpaceKernel::Attribute) == 16u);
static_assert(sizeof(LightSpaceKernel::Sample) == 64u);
static_assert(sizeof(LightSpaceKernel::Observation) == 256u);
static_assert(offsetof(LightSpaceKernel::Observation, samples) == 64u);
static_assert(sizeof(LightSpaceKernel::Deferred) == 176u);
static_assert(sizeof(LightSpaceKernel::Light) == 80u);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool Constant(const u32 index){
    return index == BufferIndex::Context || index == BufferIndex::Deferred || index == BufferIndex::Shading;
}

[[nodiscard]] bool Writable(const u32 index){
    return index >= BufferIndex::Views;
}

void Dispatch(GraphicsBackend::Device& device, CommandList& commands, ComputePipeline& pipeline,
    const LightSpaceKernel::Push& push, const u32 groupsX = 1u){
    commands.setComputeState(ComputeState{}.setPipeline(&pipeline));
    device.getDescriptorHeap().bindCompute(commands, pipeline);
    commands.setPushConstants(&push, sizeof(push));
    commands.dispatch(groupsX, 1u, 1u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void LightSpaceKernelTest::runCase(const LightSpaceKernel::Case& testCase,
    const LightSpaceKernel::Programs& programs, LightSpaceKernel::Quality* const quality){
    using namespace LightSpaceKernel;
    using namespace __hidden_light_space_kernel_fixture;
    SCOPED_TRACE(testCase.label.data());
    Alloc::ScratchArena scratchArena(Name("tests/shadow_kernel/light_space_case"));
    auto& graphicsDevice = device();
    auto& heap = graphicsDevice.getDescriptorHeap();
    Inputs input(scratchArena);
    BuildInputs(testCase, input, scratchArena);
    const bool receiverCase = testCase.receiverExpectation != ReceiverExpectation::None;
    const bool halfReceiver = receiverCase && testCase.receiverHalf;
    const u32 viewCount = testCase.point ? 6u : 1u;
    const u32 pixelCount = s_MapSize * s_MapSize * viewCount;
    Vector<View, Alloc::ScratchArena> views(viewCount + 1u, scratchArena);
    for(u32 index = 0u; index < viewCount; ++index){
        View& view = views[index];
        view.map[0] = s_MapSize;
        view.map[1] = s_MapSize;
        view.map[2] = index * s_MapSize * s_MapSize;
        view.map[3] = index;
        const bool missing = (testCase.missingFace && index == testCase.face)
            || (testCase.softExpectation == SoftExpectation::MissingNeighbor && index == 0u);
        view.light[0] = missing ? 1u : 0u;
        view.light[1] = s_OutputLayer;
        view.light[2] = index;
        view.light[3] = (testCase.point ? NWB_LIGHT_SPACE_FLAG_POINT : 0u)
            | (testCase.invalidView ? 0u : NWB_LIGHT_SPACE_FLAG_ELIGIBLE);
    }
    NWB_MEMSET(&views.back(), 0xa5, sizeof(View));
    Vector<u32, Alloc::ScratchArena> counts(pixelCount + 1u, scratchArena);
    counts.back() = 0xa5a5a5a5u;
    Vector<Event, Alloc::ScratchArena> events(pixelCount * NWB_LIGHT_SPACE_EVENTS_PER_TEXEL + 1u, scratchArena);
    NWB_MEMSET(&events.back(), 0xa5, sizeof(Event));
    Vector<CaptureDraw, Alloc::ScratchArena> arguments(viewCount * testCase.count + 1u, scratchArena);
    NWB_MEMSET(arguments.data(), 0xa5, arguments.size() * sizeof(CaptureDraw));
    Observation observations[2];
    NWB_MEMSET(observations, 0xa5, sizeof(observations));
    Vector<u32, Alloc::ScratchArena> optical(NWB_RT_OPTICAL_SCENE_HEADER_BYTES / sizeof(u32)
        + Max(testCase.count, 1u) * NWB_RT_OPTICAL_INSTANCE_BYTES / sizeof(u32), scratchArena);
    for(u32 axis = 0u; axis < 3u; ++axis){
        optical[NWB_RT_OPTICAL_SCENE_BOUNDS_MIN_OFFSET / sizeof(u32) + axis] = BitCast<u32>(input.scene[0].minimum.raw[axis]);
        optical[NWB_RT_OPTICAL_SCENE_BOUNDS_MAX_OFFSET / sizeof(u32) + axis] = BitCast<u32>(input.scene[0].maximum.raw[axis]);
    }
    optical[NWB_RT_OPTICAL_SCENE_TRANSPARENT_COUNT_OFFSET / sizeof(u32)] = testCase.opaque ? 0u : testCase.count;
    optical[NWB_RT_OPTICAL_SCENE_FLAGS_OFFSET / sizeof(u32)] = NWB_RT_OPTICAL_SCENE_FLAG_BOUNDS_VALID;
    for(u32 instance = 0u; instance < testCase.count; ++instance){
        const u32 word = (NWB_RT_OPTICAL_SCENE_HEADER_BYTES + instance * NWB_RT_OPTICAL_INSTANCE_BYTES)
            / static_cast<u32>(sizeof(u32));
        optical[word + NWB_RT_OPTICAL_INSTANCE_ENTITY_OFFSET / sizeof(u32)] = 100u + instance;
        optical[word + NWB_RT_OPTICAL_INSTANCE_BOUNDARY_OFFSET / sizeof(u32)]
            = testCase.closed ? NWB_RT_OPTICAL_BOUNDARY_CLOSED_NESTED : NWB_RT_OPTICAL_BOUNDARY_UNSPECIFIED;
        optical[word + NWB_RT_OPTICAL_INSTANCE_FLAGS_OFFSET / sizeof(u32)] = NWB_RT_OPTICAL_INSTANCE_FLAG_BOUNDS_VALID
            | (testCase.opaque ? 0u : NWB_RT_OPTICAL_INSTANCE_FLAG_TRANSPARENT);
    }
    Context context{};
    Deferred deferred{};
    const Float4U shading{ 0.0f, 0.0f, 0.0f, 1.0f };
    const void* sources[BufferIndex::Count] = {
        input.scene.data(), input.mesh.data(), input.positions.data(), input.indices.data(), input.attributes.data(),
        input.instances.data(), input.transforms.data(), input.materials.data(), &context, &deferred, &shading, &input.light,
        optical.data(), views.data(), counts.data(), events.data(), observations, arguments.data(),
    };
    const usize sizes[BufferIndex::Count] = {
        input.scene.size() * sizeof(Node), input.mesh.size() * sizeof(Node), input.positions.size() * sizeof(Float3U),
        input.indices.size() * sizeof(u32), input.attributes.size() * sizeof(Attribute), input.instances.size() * sizeof(Instance),
        input.transforms.size() * sizeof(Impl::InstanceGpuData), input.materials.size() * sizeof(Material),
        sizeof(context), sizeof(deferred), sizeof(shading), sizeof(input.light), optical.size() * sizeof(u32), views.size() * sizeof(View),
        counts.size() * sizeof(u32), events.size() * sizeof(Event), sizeof(observations), arguments.size() * sizeof(CaptureDraw),
    };
    BufferHandle buffers[BufferIndex::Count];
    GpuDescriptorHandle bufferSlots[BufferIndex::Count]{};
    TextureHandle textures[TextureIndex::Count];
    GpuDescriptorHandle textureSlots[TextureIndex::Count]{};
    ScopeExit release([&]()noexcept{
        for(const auto slot : bufferSlots){
            if(slot.valid())
                heap.free(slot);
        }
        for(const auto slot : textureSlots){
            if(slot.valid())
                heap.free(slot);
        }
        heap.collectRetired();
    });
    for(u32 index = 0u; index < BufferIndex::Count; ++index){
        BufferDesc desc;
        desc.setByteSize(sizes[index]).setInitialState(ResourceStates::Common).setKeepInitialState(true);
        if(Constant(index))
            desc.setIsConstantBuffer(true);
        else
            desc.setCanHaveRawViews(true);
        if(Writable(index))
            desc.setCanHaveUAVs(true).setCpuAccess(CpuAccessMode::Read);
        if(index == BufferIndex::DrawArguments)
            desc.setIsDrawIndirectArgs(true);
        buffers[index] = graphicsDevice.createBuffer(desc);
        ASSERT_TRUE(buffers[index]);
        bufferSlots[index] = heap.allocate(Constant(index) ? GpuDescriptorClass::UniformBuffer : GpuDescriptorClass::StorageBuffer);
        ASSERT_TRUE(bufferSlots[index].valid());
        const DescriptorWriteItem item = Constant(index) ? DescriptorWriteItem::ConstantBuffer(0u, buffers[index].get())
            : Writable(index) ? DescriptorWriteItem::RawBuffer_UAV(0u, buffers[index].get())
            : DescriptorWriteItem::RawBuffer_SRV(0u, buffers[index].get());
        ASSERT_TRUE(heap.write(bufferSlots[index], item));
    }
    for(u32 index = 0u; index < TextureIndex::Count; ++index){
        TextureDesc desc;
        desc
            .setWidth(s_FullWidth)
            .setHeight(s_FullHeight)
            .setFormat(Format::RGBA32_FLOAT)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
        ;
        if(halfReceiver && (index == TextureIndex::Position || index == TextureIndex::Normal))
            desc.setFormat(Format::RGBA16_FLOAT);
        if(index == TextureIndex::Capture){
            desc
                .setWidth(s_MapSize)
                .setHeight(s_MapSize)
                .setDimension(TextureDimension::Texture2DArray)
                .setArraySize(viewCount)
                .setFormat(Format::D32)
                .setInRenderTarget(true)
            ;
        }
        if(index == TextureIndex::Output){
            desc
                .setWidth(s_OutputSize)
                .setHeight(s_OutputSize)
                .setDimension(TextureDimension::Texture2DArray)
                .setArraySize(2u)
                .setFormat(Format::RGBA16_FLOAT)
                .setInUAV(true)
            ;
        }
        textures[index] = graphicsDevice.createTexture(desc);
        ASSERT_TRUE(textures[index]);
        const auto descriptorClass = index == TextureIndex::Output ? GpuDescriptorClass::StorageImage
            : index == TextureIndex::Capture ? GpuDescriptorClass::SampledImage2DArray : GpuDescriptorClass::SampledImage;
        textureSlots[index] = heap.allocate(descriptorClass);
        ASSERT_TRUE(textureSlots[index].valid());
        const DescriptorWriteItem item = index == TextureIndex::Output
            ? DescriptorWriteItem::Texture_UAV(0u, textures[index].get()) : DescriptorWriteItem::Texture_SRV(0u, textures[index].get());
        ASSERT_TRUE(heap.write(textureSlots[index], item));
    }
    const StagingTextureHandle outputReadback = graphicsDevice.createStagingTexture(textures[TextureIndex::Output]->getDescription(), CpuAccessMode::Read);
    const StagingTextureHandle mapReadback = graphicsDevice.createStagingTexture(textures[TextureIndex::Output]->getDescription(), CpuAccessMode::Read);
    const StagingTextureHandle depthReadback = graphicsDevice.createStagingTexture(textures[TextureIndex::Capture]->getDescription(), CpuAccessMode::Read);
    ASSERT_TRUE(outputReadback);
    ASSERT_TRUE(mapReadback);
    ASSERT_TRUE(depthReadback);
    BufferHandle referenceBuffers[2];
    for(u32 index = 0u; index < 2u; ++index){
        BufferDesc desc;
        desc
            .setByteSize(sizes[BufferIndex::Counts + index])
            .setCpuAccess(CpuAccessMode::Read)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
        ;
        referenceBuffers[index] = graphicsDevice.createBuffer(desc);
        ASSERT_TRUE(referenceBuffers[index]);
    }
    const StagingTextureHandle referenceDepth = graphicsDevice.createStagingTexture(textures[TextureIndex::Capture]->getDescription(), CpuAccessMode::Read);
    ASSERT_TRUE(referenceDepth);
    FramebufferHandle framebuffers[6];
    GraphicsPipelineHandle capture[2];
    for(u32 index = 0u; index < viewCount; ++index){
        FramebufferDesc framebufferDesc;
        framebufferDesc.setDepthAttachment(textures[TextureIndex::Capture].get(), TextureSubresourceSet(0u, 1u, index, 1u));
        framebuffers[index] = graphicsDevice.createFramebuffer(framebufferDesc);
        ASSERT_TRUE(framebuffers[index]);
        if(index != 0u)
            continue;
        for(u32 mode = 0u; mode < 2u; ++mode){
            RasterState raster;
            raster.setCullMode(RasterCullMode::None).enableDepthClip().enableScissor();
            DepthStencilState depth;
            depth.setDepthTestEnable(mode == 0u).setDepthWriteEnable(mode == 0u).setDepthFunc(ComparisonFunc::Less);
            RenderState render;
            render.setRasterState(raster).setDepthStencilState(depth);
            GraphicsPipelineDesc desc;
            desc
                .setVertexShader(programs.vertex)
                .setRenderState(render)
                .addBindingLayout(programs.layout)
                .addBindingLayout(heap.getResourceLayout())
                .addBindingLayout(heap.getSamplerLayout())
            ;
            if(mode != 0u)
                desc.setPixelShader(programs.pixel);
            capture[mode] = graphicsDevice.createGraphicsPipeline(desc, FramebufferInfo(framebufferDesc));
            ASSERT_TRUE(capture[mode]);
        }
    }
    for(Material& material : input.materials){
        material.positionSlot = bufferSlots[BufferIndex::Positions].slot();
        material.indexSlot = bufferSlots[BufferIndex::Indices].slot();
        material.attributeSlot = bufferSlots[BufferIndex::Attributes].slot();
        material.nodeSlot = bufferSlots[BufferIndex::Mesh].slot();
    }
    context.scene[0] = bufferSlots[BufferIndex::Scene].slot();
    context.scene[1] = bufferSlots[BufferIndex::Instances].slot();
    context.scene[2] = bufferSlots[BufferIndex::Materials].slot();
    context.scene[3] = bufferSlots[BufferIndex::Positions].slot();
    context.material[0] = bufferSlots[BufferIndex::Transforms].slot();
    context.material[1] = bufferSlots[BufferIndex::Optical].slot();
    context.material[2] = testCase.count;
    deferred.slots[0][1] = textureSlots[TextureIndex::Normal].slot();
    deferred.slots[0][2] = textureSlots[TextureIndex::Position].slot();
    deferred.slots[0][3] = textureSlots[TextureIndex::Depth].slot();
    deferred.slots[3][2] = bufferSlots[BufferIndex::Shading].slot();
    deferred.slots[3][3] = bufferSlots[BufferIndex::Lights].slot();
    Push push{};
    push.viewSlot = bufferSlots[BufferIndex::Views].slot();
    push.materialContextSlot = bufferSlots[BufferIndex::Context].slot();
    push.countsSlot = bufferSlots[BufferIndex::Counts].slot();
    push.eventsSlot = bufferSlots[BufferIndex::Events].slot();
    push.depthSlot = textureSlots[TextureIndex::Capture].slot();
    push.instanceCount = testCase.count;
    push.sampleCount = testCase.sampleCount;
    push.frameIndex = testCase.frameIndex;
    push.deferredResourcesSlot = bufferSlots[BufferIndex::Deferred].slot();
    push.outputSlot = textureSlots[TextureIndex::Output].slot();
    push.sceneRootSlot = bufferSlots[BufferIndex::Scene].slot();
    push.viewCount = viewCount;
    const CommandListHandle commands = graphicsDevice.createCommandList();
    ASSERT_TRUE(commands);
    commands->open();
    for(u32 index = 0u; index < BufferIndex::Count; ++index){
        ASSERT_TRUE(commands->tryWriteBuffer(*buffers[index], sources[index], sizes[index]));
        commands->setBufferState(buffers[index].get(), Constant(index) ? ResourceStates::ConstantBuffer
            : Writable(index) ? ResourceStates::UnorderedAccess : ResourceStates::ShaderResource);
    }
    Float4U pixels[s_FullWidth * s_FullHeight];
    Half4 halfPixels[s_FullWidth * s_FullHeight];
    const Float3U encodedNormal = receiverCase ? input.encodedNormal
        : Float3U(input.normal.x * 0.5f + 0.5f, input.normal.y * 0.5f + 0.5f, input.normal.z * 0.5f + 0.5f);
    for(u32 index = 0u; index < 3u; ++index){
        const Float4U value = index == TextureIndex::Position ? Float4U(input.receiver.x, input.receiver.y, input.receiver.z, 1.0f)
            : index == TextureIndex::Normal ? Float4U(encodedNormal.x, encodedNormal.y, encodedNormal.z, 1.0f) : Float4U(testCase.background ? 1.0f : 0.5f, 0.0f, 0.0f, 0.0f);
        for(auto& pixel : pixels)
            pixel = value;
        if(halfReceiver && index != TextureIndex::Depth){
            for(auto& pixel : halfPixels){
                for(u32 channel = 0u; channel < 4u; ++channel)
                    pixel.values[channel] = ConvertFloatToHalf(value.raw[channel]);
            }
            ASSERT_TRUE(commands->tryWriteTexture(*textures[index], 0u, 0u, halfPixels, s_FullWidth * sizeof(Half4)));
        }
        else{
            ASSERT_TRUE(commands->tryWriteTexture(*textures[index], 0u, 0u, pixels, s_FullWidth * sizeof(Float4U)));
        }
        commands->setTextureState(textures[index].get(), s_AllSubresources, ResourceStates::ShaderResource);
    }
    commands->clearTextureFloat(*textures[TextureIndex::Output], s_AllSubresources, Color(19.0f));
    commands->setTextureState(textures[TextureIndex::Output].get(), s_AllSubresources, ResourceStates::UnorderedAccess);
    commands->commitBarriers();
    Push viewPush = push;
    viewPush.outputSlot = bufferSlots[BufferIndex::DrawArguments].slot();
    Dispatch(graphicsDevice, *commands, *programs.view, viewPush);
    commands->setBufferState(buffers[BufferIndex::Views].get(), ResourceStates::ShaderResource);
    commands->setBufferState(buffers[BufferIndex::DrawArguments].get(), ResourceStates::IndirectArgument);
    commands->commitBarriers();
    const u32 mode = testCase.opaque ? 0u : 1u;
    // The unchanged direct capture is the raster oracle; the second capture uses only production-generated indirect arguments.
    for(u32 replay = 0u; replay < 2u; ++replay){
        if(replay != 0u){
            ASSERT_TRUE(commands->tryWriteBuffer(*buffers[BufferIndex::Counts], counts.data(), counts.size() * sizeof(u32)));
            ASSERT_TRUE(commands->tryWriteBuffer(*buffers[BufferIndex::Events], events.data(), events.size() * sizeof(Event)));
            commands->setBufferState(buffers[BufferIndex::Counts].get(), ResourceStates::UnorderedAccess);
            commands->setBufferState(buffers[BufferIndex::Events].get(), ResourceStates::UnorderedAccess);
            commands->commitBarriers();
        }
        for(u32 view = 0u; view < viewCount; ++view){
            RenderPassParameters parameters;
            parameters.depthAttachmentActions.loadAction = RenderPassLoadAction::Clear;
            parameters.depthClearValue = 1.0f;
            commands->beginRenderPass(*framebuffers[view], parameters);
            ViewportState viewport;
            viewport.addViewport(Viewport(static_cast<f32>(s_MapSize), static_cast<f32>(s_MapSize)));
            viewport.addScissorRect(Rect(0, s_MapSize, 0, s_MapSize));
            GraphicsState state;
            state
                .setPipeline(capture[mode].get())
                .setFramebuffer(framebuffers[view].get())
                .setViewport(viewport)
                .setIndirectParams(buffers[BufferIndex::DrawArguments].get())
            ;
            commands->setGraphicsState(state);
            heap.bindGraphics(*commands, *capture[mode]);
            push.viewIndex = view;
            for(u32 instance = 0u; instance < testCase.count; ++instance){
                push.instanceIndex = instance;
                commands->setPushConstants(&push, sizeof(push));
                if(replay == 0u)
                    commands->draw(DrawArguments{}.setVertexCount(static_cast<u32>(input.indices.size())));
                else
                    commands->drawIndirect((view * testCase.count + instance) * NWB_LIGHT_SPACE_DRAW_ARGUMENT_BYTES);
            }
            commands->endRenderPass();
        }
        commands->setBufferState(buffers[BufferIndex::Counts].get(), ResourceStates::ShaderResource);
        commands->setBufferState(buffers[BufferIndex::Events].get(), ResourceStates::UnorderedAccess, true);
        commands->commitBarriers();
        Push shadePush = push;
        shadePush.width = pixelCount;
        const u32 shadeGroups = AlignUp(pixelCount, NWB_LIGHT_SPACE_SHADE_GROUP_SIZE) / NWB_LIGHT_SPACE_SHADE_GROUP_SIZE;
        Dispatch(graphicsDevice, *commands, *programs.shade, shadePush, shadeGroups);

        if(replay == 0u){
            for(u32 index = 0u; index < 2u; ++index)
                commands->copyBuffer(*referenceBuffers[index], 0u, *buffers[BufferIndex::Counts + index], 0u, sizes[BufferIndex::Counts + index]);
            for(u32 layer = 0u; layer < viewCount; ++layer){
                const TextureSlice slice = TextureSlice{}.setArraySlice(layer);
                commands->copyTexture(*referenceDepth, slice, *textures[TextureIndex::Capture], slice);
            }
        }
    }
    if(testCase.corruptNeighbor){
        commands->setBufferState(buffers[BufferIndex::Events].get(), ResourceStates::UnorderedAccess, true);
        commands->commitBarriers();
        Dispatch(graphicsDevice, *commands, *programs.poison, push);
    }
    if(testCase.corruptEvent){
        for(usize index = 0u; index + 1u < events.size(); ++index)
            events[index].depth = BitCast<f32>(0x7fc00000u);
        ASSERT_TRUE(commands->tryWriteBuffer(*buffers[BufferIndex::Events], events.data(), events.size() * sizeof(Event)));
    }
    commands->setBufferState(buffers[BufferIndex::Counts].get(), ResourceStates::ShaderResource);
    commands->setBufferState(buffers[BufferIndex::Events].get(), ResourceStates::ShaderResource);
    commands->setTextureState(textures[TextureIndex::Capture].get(), s_AllSubresources, ResourceStates::ShaderResource);
    commands->commitBarriers();
    Dispatch(graphicsDevice, *commands, *programs.resolve[mode], push);
    for(u32 layer = 0u; layer < 2u; ++layer){
        const TextureSlice slice = TextureSlice{}.setArraySlice(layer);
        commands->copyTexture(*mapReadback, slice, *textures[TextureIndex::Output], slice);
    }
    commands->setTextureState(textures[TextureIndex::Output].get(), s_AllSubresources, ResourceStates::UnorderedAccess, true);
    commands->commitBarriers();
    Dispatch(graphicsDevice, *commands, *programs.fallback[mode], push);
    push.sceneRootSlot = bufferSlots[BufferIndex::Observation].slot();
    Dispatch(graphicsDevice, *commands, *programs.observe[mode], push);
    for(u32 layer = 0u; layer < 2u; ++layer){
        const TextureSlice slice = TextureSlice{}.setArraySlice(layer);
        commands->copyTexture(*outputReadback, slice, *textures[TextureIndex::Output], slice);
    }
    for(u32 layer = 0u; layer < viewCount; ++layer){
        const TextureSlice slice = TextureSlice{}.setArraySlice(layer);
        commands->copyTexture(*depthReadback, slice, *textures[TextureIndex::Capture], slice);
    }
    ASSERT_FALSE(commands->commandRecordingFailed());
    commands->close();
    ASSERT_FALSE(commands->commandRecordingFailed());
    CommandList* const lists[] = { commands.get() };
    ASSERT_TRUE(graphicsDevice.executeCommandLists(lists, LengthOf(lists), commands->getDescription().physicalQueue, QueueSubmissionDesc{}).valid());
    ASSERT_TRUE(graphicsDevice.waitForIdle());
    CheckCaptureParity(
        graphicsDevice, *referenceBuffers[0], *referenceBuffers[1], *referenceDepth,
        *buffers[BufferIndex::Counts], *buffers[BufferIndex::Events], *depthReadback, pixelCount, viewCount,
        testCase.corruptEvent || testCase.corruptNeighbor
    );
    const auto* drawData = static_cast<const CaptureDraw*>(graphicsDevice.mapBuffer(*buffers[BufferIndex::DrawArguments], CpuAccessMode::Read));
    ASSERT_NE(drawData, nullptr);
    EXPECT_EQ(NWB_MEMCMP(drawData + arguments.size() - 1u, &arguments.back(), sizeof(CaptureDraw)), 0);
    u32 culled = 0u;
    for(u32 index = 0u; index < viewCount * testCase.count; ++index){
        const CaptureDraw& draw = drawData[index];
        EXPECT_TRUE(draw.vertexCount == 0u || draw.vertexCount == input.indices.size());
        EXPECT_EQ(draw.instanceCount, 1u);
        EXPECT_EQ(draw.firstVertex, 0u);
        EXPECT_EQ(draw.firstInstance, 0u);
        culled += draw.vertexCount == 0u ? 1u : 0u;
    }
    if(testCase.point && testCase.count != 0u && !testCase.nearClip && !testCase.invalidView && !testCase.invalidFit)
        EXPECT_GT(culled, 0u);
    graphicsDevice.unmapBuffer(*buffers[BufferIndex::DrawArguments]);
    const auto* observationData = static_cast<const Observation*>(graphicsDevice.mapBuffer(*buffers[BufferIndex::Observation], CpuAccessMode::Read));
    ASSERT_NE(observationData, nullptr);
    EXPECT_EQ(NWB_MEMCMP(&observationData[1], &observations[1], sizeof(Observation)), 0);
    const Observation observed = observationData[0];
    graphicsDevice.unmapBuffer(*buffers[BufferIndex::Observation]);
    const Float3U expected = Expected(testCase, input);
    const Float3U expectedSoftware = Expected(testCase, input, true);
    const bool fallback = testCase.count > NWB_LIGHT_SPACE_EVENTS_PER_TEXEL / 2u || testCase.invalidView
        || testCase.missingFace || testCase.corruptEvent || testCase.nearClip || testCase.invalidFit
        || testCase.receiverExpectation == ReceiverExpectation::Singular;
    const bool soft = testCase.sourceSize > 0.0f;
    if(!soft)
        EXPECT_EQ(observed.mapReady, fallback ? 0u : 1u);
    if(quality != nullptr){
        quality->faceMask |= observed.faceMask;
        quality->fallbackMask |= observed.fallbackMask;
        quality->geometricPenumbra |= observed.software.x > 0.0f && observed.software.x < 1.0f;
    }
    Float3U sampledExpected{};
    for(u32 sample = 0u; sample < testCase.sampleCount; ++sample){
        const Sample& ray = observed.samples[sample];
        const Float3U analytic = ExpectedRay(testCase, input, ray.direction, ray.maximum, true);
        for(u32 channel = 0u; channel < 3u; ++channel){
            EXPECT_NEAR(ray.software.raw[channel], analytic.raw[channel], 0.004f);
            if(receiverCase)
                EXPECT_FLOAT_EQ(analytic.raw[channel], testCase.receiverExpectation == ReceiverExpectation::Blocked ? 0.0f : 1.0f);
            sampledExpected.raw[channel] += analytic.raw[channel] / static_cast<f32>(testCase.sampleCount);
        }
        EXPECT_EQ(ray.accepted == 0u, (observed.fallbackMask & (1u << sample)) != 0u);
        EXPECT_EQ(ray.accepted, ray.reprojectedAccepted);
        if(ray.accepted != 0u){
            for(u32 channel = 0u; channel < 3u; ++channel)
                EXPECT_NEAR(ray.selected.raw[channel], ray.reprojected.raw[channel], 0.000001f);
        }
    }
    if(soft){
        EXPECT_TRUE(IsFinite(observed.blockerDistance));
        EXPECT_GE(observed.blockerDistance, 0.0f);
        if(testCase.softExpectation == SoftExpectation::MissingNeighbor || testCase.corruptNeighbor){
            EXPECT_EQ(observed.blockerReady, 0u);
            EXPECT_EQ(observed.fallbackMask, (1u << testCase.sampleCount) - 1u);
        }
        else{
            EXPECT_EQ(observed.blockerReady, 1u);
            EXPECT_EQ(observed.mapReady, 1u);
        }
        if(testCase.softExpectation == SoftExpectation::Blocked || testCase.softExpectation == SoftExpectation::Interior)
            EXPECT_GT(observed.blockerDistance, 0.0f);
    }
    if(!testCase.invalidView && !testCase.missingFace && !testCase.nearClip && !testCase.invalidFit){
        EXPECT_EQ(observed.projected, 1u);
        if(testCase.point){
            const Float3U magnitude{ Abs(input.receiver.x), Abs(input.receiver.y), Abs(input.receiver.z) };
            const u32 face = magnitude.x >= Max(magnitude.y, magnitude.z) ? (input.receiver.x >= 0.0f ? 0u : 1u)
                : magnitude.y >= magnitude.z ? (input.receiver.y >= 0.0f ? 2u : 3u) : (input.receiver.z >= 0.0f ? 4u : 5u);
            EXPECT_EQ(observed.face, face);
        }
        if(!testCase.opaque && !soft)
            EXPECT_EQ(observed.count, testCase.count * 2u);
    }
    for(u32 channel = 0u; channel < 3u; ++channel){
        EXPECT_TRUE(IsFinite(observed.software.raw[channel]));
        EXPECT_NEAR(observed.software.raw[channel], soft ? sampledExpected.raw[channel] : expectedSoftware.raw[channel], 0.004f);
        if(observed.mapReady != 0u && !soft){
            EXPECT_TRUE(IsFinite(observed.mapped.raw[channel]));
            // Raster samples are at texel centers; radial chords and normals differ slightly from the central receiver ray.
            EXPECT_NEAR(observed.mapped.raw[channel], expected.raw[channel], testCase.seam != 0 ? 0.03f : 0.007f);
        }
    }
    for(u32 index = BufferIndex::Views; index <= BufferIndex::Events; ++index){
        const u8* data = static_cast<const u8*>(graphicsDevice.mapBuffer(*buffers[index], CpuAccessMode::Read));
        ASSERT_NE(data, nullptr);
        const usize guardBytes = index == BufferIndex::Views ? sizeof(View) : index == BufferIndex::Counts ? sizeof(u32) : sizeof(Event);
        EXPECT_EQ(NWB_MEMCMP(data + sizes[index] - guardBytes, static_cast<const u8*>(sources[index]) + sizes[index] - guardBytes, guardBytes), 0);
        if(index == BufferIndex::Views && testCase.point && testCase.count > 0u){
            const auto* produced = reinterpret_cast<const View*>(data);
            for(u32 view = 0u; view < viewCount; ++view){
                if(produced[view].depth.w == 0.0f)
                    continue;
                const f32 baseNear = static_cast<f32>(NWB_SHADOW_RAY_MIN_DISTANCE * 0.25);
                const f32 provenNear = produced[view].depth.z * (0.5f / Sqrt(3.0f));
                EXPECT_GE(produced[view].depth.x, baseNear);
                EXPECT_LE(produced[view].depth.x, produced[view].depth.z / Sqrt(3.0f));
                if(provenNear > baseNear)
                    EXPECT_GT(produced[view].depth.x, baseNear);
            }
        }
        if(index == BufferIndex::Events && observed.projected != 0u && !testCase.opaque && !testCase.corruptEvent){
            const auto* captured = reinterpret_cast<const Event*>(data);
            u32 perInstance[NWB_LIGHT_SPACE_EVENTS_PER_TEXEL]{};
            for(u32 event = 0u; event < Min(observed.count, NWB_LIGHT_SPACE_EVENTS_PER_TEXEL); ++event){
                const Event& value = captured[observed.pixelIndex * NWB_LIGHT_SPACE_EVENTS_PER_TEXEL + event];
                EXPECT_TRUE(IsFinite(value.depth));
                const u32 instance = value.instanceAndFacing & NWB_LIGHT_SPACE_EVENT_INSTANCE_MASK;
                EXPECT_LT(instance, testCase.count);
                EXPECT_LT(value.primitive, static_cast<u32>(input.indices.size()) / 3u);
                if(instance < LengthOf(perInstance))
                    ++perInstance[instance];
            }
            if(!fallback && !soft){
                for(u32 instance = 0u; instance < testCase.count; ++instance)
                    EXPECT_EQ(perInstance[instance], 2u);
            }
        }
        graphicsDevice.unmapBuffer(*buffers[index]);
    }
    if(observed.projected != 0u){
        const u32 face = testCase.point ? observed.face : 0u;
        usize pitch = 0u;
        const TextureSlice slice = TextureSlice{}.setArraySlice(face);
        const u8* depth = static_cast<const u8*>(graphicsDevice.mapStagingTexture(*depthReadback, slice, CpuAccessMode::Read, &pitch));
        ASSERT_NE(depth, nullptr);
        const u32 localPixel = observed.pixelIndex % (s_MapSize * s_MapSize);
        f32 value = 0.0f;
        NWB_MEMCPY(&value, sizeof(value), depth + (localPixel / s_MapSize) * pitch + (localPixel % s_MapSize) * sizeof(f32), sizeof(value));
        if(testCase.opaque && testCase.count > 0u && !soft)
            EXPECT_LT(value, 1.0f);
        else if(testCase.opaque && soft)
            EXPECT_TRUE(IsFinite(value));
        else
            EXPECT_FLOAT_EQ(value, 1.0f);
        graphicsDevice.unmapStagingTexture(*depthReadback);
    }
    for(u32 layer = 0u; layer < 2u; ++layer){
        usize pitch = 0u;
        usize mapPitch = 0u;
        const TextureSlice slice = TextureSlice{}.setArraySlice(layer);
        const u8* mapData = static_cast<const u8*>(graphicsDevice.mapStagingTexture(*mapReadback, slice, CpuAccessMode::Read, &mapPitch));
        ASSERT_NE(mapData, nullptr);
        const u8* mapped = static_cast<const u8*>(graphicsDevice.mapStagingTexture(*outputReadback, slice, CpuAccessMode::Read, &pitch));
        ASSERT_NE(mapped, nullptr);
        for(u32 y = 0u; y < s_OutputSize; ++y){
            for(u32 x = 0u; x < s_OutputSize; ++x){
                Half4 pixel{};
                NWB_MEMCPY(&pixel, sizeof(pixel), mapped + y * pitch + x * sizeof(pixel), sizeof(pixel));
                Half4 beforeFallback{};
                NWB_MEMCPY(&beforeFallback, sizeof(beforeFallback), mapData + y * mapPitch + x * sizeof(beforeFallback), sizeof(beforeFallback));
                const bool active = layer == s_OutputLayer && x < (s_FullWidth + 1u) / 2u && y < (s_FullHeight + 1u) / 2u;
                if(active){
                    const f32 marker = ConvertHalfToFloat(beforeFallback.values[3]);
                    EXPECT_TRUE(marker == 0.0f || marker == 1.0f);
                    if(marker == 1.0f)
                        EXPECT_EQ(NWB_MEMCMP(&beforeFallback, &pixel, sizeof(pixel)), 0);
                    if(x == 0u && y == 0u)
                        EXPECT_FLOAT_EQ(marker, testCase.background || observed.mapReady != 0u ? 1.0f : 0.0f);
                }
                else{
                    EXPECT_EQ(NWB_MEMCMP(&beforeFallback, &pixel, sizeof(pixel)), 0);
                }
                for(u32 channel = 0u; channel < 4u; ++channel){
                    const f32 actual = ConvertHalfToFloat(pixel.values[channel]);
                    if(!active){
                        EXPECT_FLOAT_EQ(actual, 19.0f);
                        continue;
                    }
                    if(channel == 3u || testCase.background){
                        EXPECT_FLOAT_EQ(actual, 1.0f);
                        continue;
                    }
                    EXPECT_TRUE(IsFinite(actual));
                    EXPECT_GE(actual, 0.0f);
                    EXPECT_LE(actual, 1.0f);
                    // The RGBA16_FLOAT map output carries half quantization; a fully lit receiver may round one half code below 1.0 (0x3bff). Keep the analytic-path checks exact and allow that single representable neighbor here.
                    if(receiverCase){
                        const f32 receiverExpected = testCase.receiverExpectation == ReceiverExpectation::Blocked ? 0.0f : 1.0f;
                        const u16 actualReceiverHalf = ConvertFloatToHalf(actual);
                        const u16 expectedReceiverHalf = ConvertFloatToHalf(receiverExpected);
                        EXPECT_LE(Abs(static_cast<i32>(actualReceiverHalf) - static_cast<i32>(expectedReceiverHalf)), 1);
                    }
                    if(!soft || (x == 0u && y == 0u)){
                        const f32 pathExpected = !soft && fallback ? observed.software.raw[channel] : observed.mapped.raw[channel];
                        EXPECT_NEAR(actual, pathExpected, 0.001f);
                    }
                    if(soft){
                        if(testCase.softExpectation == SoftExpectation::Lit)
                            EXPECT_GE(actual, ConvertHalfToFloat(static_cast<u16>(0x3bff)));
                        else if(testCase.softExpectation == SoftExpectation::Blocked)
                            EXPECT_LE(actual, ConvertHalfToFloat(static_cast<u16>(0x0001)));
                        else if(testCase.softExpectation == SoftExpectation::Interior){
                            EXPECT_GT(actual, 0.0f);
                            EXPECT_LT(actual, 0.99f);
                            EXPECT_NEAR(actual, expected.raw[channel], 0.02f);
                        }
                        if(quality != nullptr && channel == 0u && actual > 0.0f && actual < 1.0f)
                            quality->penumbra = true;
                    }
                }
            }
        }
        graphicsDevice.unmapStagingTexture(*outputReadback);
        graphicsDevice.unmapStagingTexture(*mapReadback);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

