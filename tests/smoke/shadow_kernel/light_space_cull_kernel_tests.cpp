// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "light_space_kernel_fixture.h"

#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{
namespace __hidden_light_space_cull_kernel{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace LightSpaceKernel;

struct CullCase{
    AStringView label;
    Node root{};
    Impl::InstanceGpuData transform{};
    bool failOpen = false;
    u32 retainViews = 0u;
};

constexpr u32 s_ViewCount = 8u;
constexpr u32 s_FixedBufferCount = 6u;
namespace FixedBuffer{ enum Enum : u32{ Views, Context, Materials, Transforms, Instances, Arguments }; };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void BuildViews(View (&views)[s_ViewCount]){
    View& orthographic = views[0];
    orthographic.rows[0] = { 1.0f, 0.0f, 0.0f, 0.0f };
    orthographic.rows[1] = { 0.0f, 1.0f, 0.0f, 0.0f };
    orthographic.rows[2] = { 0.0f, 0.0f, 1.0f, 0.0f };
    orthographic.rows[3] = { 0.0f, 0.0f, 0.0f, 1.0f };
    orthographic.depth = { 0.0f, 1.0f, 0.0f, 1.0f };
    orthographic.light[3] = NWB_LIGHT_SPACE_FLAG_ELIGIBLE;
    const Float3U forward[] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    const Float3U right[] = { { 0, 0, -1 }, { 0, 0, 1 }, { -1, 0, 0 }, { 1, 0, 0 }, { 1, 0, 0 }, { -1, 0, 0 } };
    const Float3U up[] = { { 0, 1, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { 0, 0, 1 }, { 0, 1, 0 }, { 0, 1, 0 } };
    constexpr f32 nearPlane = 0.25f;
    constexpr f32 farPlane = 8.0f;
    for(u32 face = 0u; face < 6u; ++face){
        View& view = views[face + 1u];
        const f32 scale = farPlane / (farPlane - nearPlane);
        view.rows[0] = { right[face].x, right[face].y, right[face].z, 0.0f };
        view.rows[1] = { up[face].x, up[face].y, up[face].z, 0.0f };
        view.rows[2] = { forward[face].x * scale, forward[face].y * scale, forward[face].z * scale, -nearPlane * scale };
        view.rows[3] = { forward[face].x, forward[face].y, forward[face].z, 0.0f };
        view.depth = { nearPlane, farPlane, 0.5f, 1.0f };
        view.light[2] = face;
        view.light[3] = NWB_LIGHT_SPACE_FLAG_ELIGIBLE | NWB_LIGHT_SPACE_FLAG_POINT;
    }
    views[7] = orthographic;
    views[7].depth.w = 0.0f;
}

void BuildCases(Vector<CullCase, Alloc::ScratchArena>& cases){
    cases.reserve(40u);
    cases.push_back({ .label = "current root before pose update", .root = { { 2.0f, -0.25f, 0.4f }, 0u, { 3.0f, 0.25f, 0.6f }, 0u } });
    cases.push_back({ .label = "inside orthographic", .root = { { -0.25f, -0.25f, 0.4f }, 0u, { 0.25f, 0.25f, 0.6f }, 0u } });
    for(u32 axis = 0u; axis < 3u; ++axis){
        for(u32 side = 0u; side < 2u; ++side){
            CullCase outside{ .label = "strict orthographic outside", .root = cases[1].root };
            outside.root.minimum.raw[axis] = side == 0u ? -3.0f : 2.0f;
            outside.root.maximum.raw[axis] = side == 0u ? -2.0f : 3.0f;
            cases.push_back(outside);
            CullCase touch{ .label = "orthographic boundary touch", .root = cases[1].root };
            const f32 boundary = side != 0u ? 1.0f : axis == 2u ? 0.0f : -1.0f;
            touch.root.minimum.raw[axis] = boundary - (side != 0u ? 0.0f : 0.125f);
            touch.root.maximum.raw[axis] = boundary + (side != 0u ? 0.125f : 0.0f);
            touch.retainViews = 1u;
            cases.push_back(touch);
            CullCase straddle = touch;
            straddle.label = "orthographic boundary straddle";
            straddle.root.minimum.raw[axis] = boundary - 0.125f;
            straddle.root.maximum.raw[axis] = boundary + 0.125f;
            cases.push_back(straddle);
        }
    }
    for(u32 axis = 0u; axis < 3u; ++axis){
        for(u32 side = 0u; side < 2u; ++side){
            CullCase face{ .label = "cube face caster", .root = { { -0.2f, -0.2f, -0.2f }, 0u, { 0.2f, 0.2f, 0.2f }, 0u } };
            Float3U translation{};
            translation.raw[axis] = side == 0u ? -3.0f : 3.0f;
            face.transform.translation = Float3UInt(translation.x, translation.y, translation.z, 0u);
            cases.push_back(face);
        }
    }
    for(u32 endpoint = 0u; endpoint < 2u; ++endpoint){
        const f32 boundary = endpoint == 0u ? 0.25f : 8.0f;
        CullCase touch{ .label = "cube near or far plane touch", .root = { { -0.1f, -0.1f, boundary }, 0u,
            { 0.1f, 0.1f, boundary }, 0u }, .retainViews = 1u << 5u };
        if(endpoint == 0u)
            touch.root.minimum.z -= 0.125f;
        else
            touch.root.maximum.z += 0.125f;
        cases.push_back(touch);
        CullCase straddle = touch;
        straddle.label = "cube near or far plane straddle";
        straddle.root.minimum.z = boundary - 0.125f;
        straddle.root.maximum.z = boundary + 0.125f;
        cases.push_back(straddle);
    }
    CullCase seam{ .label = "cube seam touches and straddles", .root = { { 1.0f, -0.2f, 1.0f }, 0u, { 2.0f, 0.2f, 2.0f }, 0u } };
    cases.push_back(seam);
    CullCase transformed{ .label = "rotated mirrored nonuniform current root", .root = cases[1].root };
    transformed.transform.rotation = { 0.0f, 0.0f, 0.707106769f, 0.707106769f };
    transformed.transform.scale = { -2.0f, 0.5f, 1.5f, 0.0f };
    transformed.transform.translation = Float3UInt(3.0f, 0.0f, 0.5f, 0u);
    cases.push_back(transformed);
    CullCase invalid{ .label = "nonfinite root retains", .root = cases[0].root, .failOpen = true };
    invalid.root.minimum.x = BitCast<f32>(0x7fc00000u);
    cases.push_back(invalid);
    invalid.root = cases[0].root;
    invalid.label = "inverted root retains";
    invalid.root.minimum.x = invalid.root.maximum.x + 1.0f;
    cases.push_back(invalid);
    invalid.root = cases[0].root;
    invalid.label = "subnormal root retains";
    invalid.root.minimum.y = BitCast<f32>(1u);
    cases.push_back(invalid);
    invalid.root = cases[0].root;
    invalid.label = "nonfinite transform retains";
    invalid.transform.scale.x = BitCast<f32>(0x7f800000u);
    cases.push_back(invalid);
    invalid = { .label = "overflowing transform retains", .root = cases[0].root, .failOpen = true };
    invalid.transform.scale.x = Limit<f32>::s_Max;
    cases.push_back(invalid);
}

void CheckProof(const CullCase& testCase, const View& view, const CaptureDraw& draw, const u32 primitiveCount){
    SCOPED_TRACE(testCase.label.data());
    EXPECT_EQ(draw.instanceCount, 1u);
    EXPECT_EQ(draw.firstVertex, 0u);
    EXPECT_EQ(draw.firstInstance, 0u);
    EXPECT_TRUE(draw.vertexCount == 0u || draw.vertexCount == primitiveCount * 3u);
    if(testCase.failOpen || view.depth.w == 0.0f){
        EXPECT_EQ(draw.vertexCount, primitiveCount * 3u);
        return;
    }
    const auto& transform = testCase.transform;
    const f64 translation[] = { transform.translation.x, transform.translation.y, transform.translation.z };
    const f64 x = transform.rotation.x;
    const f64 y = transform.rotation.y;
    const f64 z = transform.rotation.z;
    const f64 w = transform.rotation.w;
    // Independent FP64 affine rotation matrix, followed by exact stored projection coefficients.
    const f64 rotation[3][3] = {
        { 1.0 - 2.0 * (y * y + z * z), 2.0 * (x * y - z * w), 2.0 * (x * z + y * w) },
        { 2.0 * (x * y + z * w), 1.0 - 2.0 * (x * x + z * z), 2.0 * (y * z - x * w) },
        { 2.0 * (x * z - y * w), 2.0 * (y * z + x * w), 1.0 - 2.0 * (x * x + y * y) },
    };
    f64 planeMaximum[6] = { -Limit<f64>::s_Max, -Limit<f64>::s_Max, -Limit<f64>::s_Max,
        -Limit<f64>::s_Max, -Limit<f64>::s_Max, -Limit<f64>::s_Max };
    for(u32 corner = 0u; corner < 8u; ++corner){
        f64 local[3];
        f64 world[4] = { 0.0, 0.0, 0.0, 1.0 };
        for(u32 axis = 0u; axis < 3u; ++axis)
            local[axis] = static_cast<f64>((corner & (1u << axis)) != 0u
                ? testCase.root.maximum.raw[axis] : testCase.root.minimum.raw[axis]) * transform.scale.raw[axis];
        for(u32 row = 0u; row < 3u; ++row){
            world[row] = translation[row];
            for(u32 column = 0u; column < 3u; ++column)
                world[row] += rotation[row][column] * local[column];
        }
        f64 clip[4]{};
        for(u32 row = 0u; row < 4u; ++row){
            for(u32 column = 0u; column < 4u; ++column)
                clip[row] += static_cast<f64>(view.rows[row].raw[column]) * world[column];
        }
        const f64 planes[] = { clip[0] + clip[3], clip[3] - clip[0], clip[1] + clip[3],
            clip[3] - clip[1], clip[2], clip[3] - clip[2] };
        for(u32 plane = 0u; plane < 6u; ++plane)
            planeMaximum[plane] = Max(planeMaximum[plane], planes[plane]);
    }
    bool outside = false;
    bool wellSeparated = false;
    for(const f64 maximum : planeMaximum){
        outside |= maximum < 0.0;
        wellSeparated |= maximum < -0.001;
    }
    if(draw.vertexCount == 0u)
        EXPECT_TRUE(outside);
    if(wellSeparated)
        EXPECT_EQ(draw.vertexCount, 0u);
    if(!outside)
        EXPECT_EQ(draw.vertexCount, primitiveCount * 3u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void LightSpaceKernelTest::runCullCases(const LightSpaceKernel::Programs& programs){
    using namespace LightSpaceKernel;
    using namespace __hidden_light_space_cull_kernel;
    Alloc::ScratchArena scratchArena(Name("tests/shadow_kernel/light_space_cull"));
    auto& graphicsDevice = device();
    auto& heap = graphicsDevice.getDescriptorHeap();
    Vector<CullCase, Alloc::ScratchArena> cases(scratchArena);
    BuildCases(cases);
    const u32 count = static_cast<u32>(cases.size());
    View views[s_ViewCount]{};
    BuildViews(views);
    Context context{};
    Vector<Material, Alloc::ScratchArena> materials(count, scratchArena);
    Vector<Instance, Alloc::ScratchArena> instances(count, scratchArena);
    Vector<Impl::InstanceGpuData, Alloc::ScratchArena> transforms(scratchArena);
    transforms.reserve(count);
    Vector<CaptureDraw, Alloc::ScratchArena> arguments(s_ViewCount * count + 1u, scratchArena);
    NWB_MEMSET(arguments.data(), 0xa5, arguments.size() * sizeof(CaptureDraw));
    const u32 bufferCount = s_FixedBufferCount + count;
    Vector<BufferHandle, Alloc::ScratchArena> buffers(bufferCount, scratchArena);
    Vector<GpuDescriptorHandle, Alloc::ScratchArena> slots(bufferCount, scratchArena);
    ScopeExit release([&]()noexcept{
        for(const auto slot : slots){
            if(slot.valid())
                heap.free(slot);
        }
        heap.collectRetired();
    });
    const usize fixedSizes[] = { sizeof(views), sizeof(context), materials.size() * sizeof(Material), count * sizeof(Impl::InstanceGpuData),
        instances.size() * sizeof(Instance), arguments.size() * sizeof(CaptureDraw) };
    for(u32 index = 0u; index < bufferCount; ++index){
        BufferDesc desc;
        desc
            .setByteSize(index < s_FixedBufferCount ? fixedSizes[index] : sizeof(Node))
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
        ;
        if(index == FixedBuffer::Context)
            desc.setIsConstantBuffer(true);
        else
            desc.setCanHaveRawViews(true);
        if(index == FixedBuffer::Arguments)
            desc.setCanHaveUAVs(true).setCpuAccess(CpuAccessMode::Read);
        buffers[index] = graphicsDevice.createBuffer(desc);
        ASSERT_TRUE(buffers[index]);
        slots[index] = heap.allocate(index == FixedBuffer::Context ? GpuDescriptorClass::UniformBuffer : GpuDescriptorClass::StorageBuffer);
        ASSERT_TRUE(slots[index].valid());
        const DescriptorWriteItem item = index == FixedBuffer::Context ? DescriptorWriteItem::ConstantBuffer(0u, buffers[index].get())
            : index == FixedBuffer::Arguments ? DescriptorWriteItem::RawBuffer_UAV(0u, buffers[index].get())
            : DescriptorWriteItem::RawBuffer_SRV(0u, buffers[index].get());
        ASSERT_TRUE(heap.write(slots[index], item));
    }
    for(u32 index = 0u; index < count; ++index){
        materials[index].meshInstanceIndex = index;
        materials[index].nodeSlot = slots[s_FixedBufferCount + index].slot();
        instances[index].primitiveCount = 1u + index % 7u;
        transforms.push_back(cases[index].transform);
    }
    context.scene[1] = slots[FixedBuffer::Instances].slot();
    context.scene[2] = slots[FixedBuffer::Materials].slot();
    context.material[0] = slots[FixedBuffer::Transforms].slot();
    Push push{};
    push.viewSlot = slots[FixedBuffer::Views].slot();
    push.materialContextSlot = slots[FixedBuffer::Context].slot();
    push.outputSlot = slots[FixedBuffer::Arguments].slot();
    push.instanceCount = count;
    push.viewCount = s_ViewCount;
    const void* sources[] = { views, &context, materials.data(), transforms.data(), instances.data(), arguments.data() };
    BufferDesc readbackDesc;
    readbackDesc
        .setByteSize(fixedSizes[FixedBuffer::Arguments])
        .setCpuAccess(CpuAccessMode::Read)
        .setInitialState(ResourceStates::Common)
        .setKeepInitialState(true)
    ;
    const BufferHandle firstArguments = graphicsDevice.createBuffer(readbackDesc);
    ASSERT_TRUE(firstArguments);
    const CommandListHandle commands = graphicsDevice.createCommandList();
    ASSERT_TRUE(commands);
    commands->open();
    for(u32 index = 0u; index < bufferCount; ++index){
        const void* source = index < s_FixedBufferCount ? sources[index] : &cases[index - s_FixedBufferCount].root;
        const usize size = index < s_FixedBufferCount ? fixedSizes[index] : sizeof(Node);
        ASSERT_TRUE(commands->tryWriteBuffer(*buffers[index], source, size));
        commands->setBufferState(buffers[index].get(), index == FixedBuffer::Context ? ResourceStates::ConstantBuffer
            : index == FixedBuffer::Arguments ? ResourceStates::UnorderedAccess : ResourceStates::ShaderResource);
    }
    commands->commitBarriers();
    for(u32 epoch = 0u; epoch < 2u; ++epoch){
        if(epoch != 0u){
            // A later accepted pose changes only the device root, without replacing its descriptor or the transform.
            ASSERT_TRUE(commands->tryWriteBuffer(*buffers[s_FixedBufferCount], &cases[1].root, sizeof(Node)));
            commands->setBufferState(buffers[s_FixedBufferCount].get(), ResourceStates::ShaderResource);
            commands->setBufferState(buffers[FixedBuffer::Arguments].get(), ResourceStates::UnorderedAccess, true);
            commands->commitBarriers();
        }
        commands->setComputeState(ComputeState{}.setPipeline(programs.cull.get()));
        heap.bindCompute(*commands, *programs.cull);
        commands->setPushConstants(&push, sizeof(push));
        commands->dispatch(1u, 1u, 1u);
        if(epoch == 0u)
            commands->copyBuffer(*firstArguments, 0u, *buffers[FixedBuffer::Arguments], 0u, fixedSizes[FixedBuffer::Arguments]);
    }
    ASSERT_FALSE(commands->commandRecordingFailed());
    commands->close();
    ASSERT_FALSE(commands->commandRecordingFailed());
    CommandList* const lists[] = { commands.get() };
    ASSERT_TRUE(graphicsDevice.executeCommandLists(lists, LengthOf(lists), commands->getDescription().physicalQueue, QueueSubmissionDesc{}).valid());
    ASSERT_TRUE(graphicsDevice.waitForIdle());
    for(u32 epoch = 0u; epoch < 2u; ++epoch){
        Buffer& source = epoch == 0u ? *firstArguments : *buffers[FixedBuffer::Arguments];
        const auto* output = static_cast<const CaptureDraw*>(graphicsDevice.mapBuffer(source, CpuAccessMode::Read));
        ASSERT_NE(output, nullptr);
        EXPECT_EQ(NWB_MEMCMP(output + arguments.size() - 1u, &arguments.back(), sizeof(CaptureDraw)), 0);
        for(u32 view = 0u; view < s_ViewCount; ++view){
            SCOPED_TRACE(view);
            for(u32 index = 0u; index < count; ++index){
                CullCase expected = cases[index];
                if(epoch != 0u && index == 0u)
                    expected.root = cases[1].root;
                const CaptureDraw& draw = output[view * count + index];
                CheckProof(expected, views[view], draw, instances[index].primitiveCount);
                if((expected.retainViews & (1u << view)) != 0u)
                    EXPECT_EQ(draw.vertexCount, instances[index].primitiveCount * 3u);
            }
        }
        EXPECT_EQ(output[0].vertexCount, epoch == 0u ? 0u : instances[0].primitiveCount * 3u);
        graphicsDevice.unmapBuffer(source);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(LightSpaceKernelTest, ConservativeDrawArgumentsFollowCurrentRootsAndTransforms){
    const Common::LoggerRegistrationGuard diagnostics(*s_logger, Common::LoggerBreakPolicy::BreakOnFatal);
    ScopeExit report([&]()noexcept{
        if(::testing::Test::HasFailure()){
            s_logger->emitErrorsToStderr();
            s_logger->emitMessagesContainingToStderr(NWB_TEXT("Vulkan debug: [severity=error"));
        }
    });
    Alloc::ScratchArena scratchArena(Name("tests/shadow_kernel/light_space_cull_cook"));
    LightSpaceKernel::Programs programs;
    ASSERT_TRUE(loadPrograms(scratchArena, programs));
    runCullCases(programs);
    EXPECT_EQ(s_logger->errorCount(), 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

