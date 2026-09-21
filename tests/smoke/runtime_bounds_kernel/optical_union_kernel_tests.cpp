// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "runtime_bounds_kernel_fixture.h"

#include <impl/assets/graphics/mesh/runtime_bounds_constants.h>
#include <impl/assets/graphics/raytrace/optical_bounds_finalize_constants.h>
#include <impl/assets/graphics/raytrace/optical_scene_constants.h>

#include <global/bit.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_optical_union_kernel_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct Bounds{
    Float3U minimum{ -2.0f, -3.0f, -4.0f };
    u32 flags = NWB_RUNTIME_MESH_BOUNDS_FLAG_VALID;
    Float3U maximum{ 5.0f, 7.0f, 11.0f };
    u32 reserved = 0u;
};

struct Header{
    Float3U minimum{ -100.0f, -20.0f, -30.0f };
    u32 transparentCount;
    Float3U maximum{ -90.0f, -10.0f, -20.0f };
    u32 flags = 0x40u;
};

struct Instance{
    u32 entity;
    u32 priority;
    u32 boundary;
    u32 flags;
};

struct RuntimeInput{
    Float4U rows[3]{ { 1.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f, 0.0f } };
    u32 boundsSlot;
    u32 instanceIndex;
    u32 reserved[2]{};
};

struct Push{
    u32 sourceSlot;
    u32 inputsSlot;
    u32 outputSlot;
    u32 instanceCount;
    u32 runtimeCount;
    u32 staticComplete;
};

namespace Mode{
    enum Enum : u8{
        Identity,
        Mirrored,
        Nonuniform,
        RotationShear,
        LargeCancellation,
        SubnormalScale,
        Collapsed,
        MissingSlot,
        InvalidLocalFlag,
        NonfiniteLocal,
        InvertedLocal,
        NonfiniteTransform,
        InfiniteTranslation,
        OverflowedTransform,
        InvalidInstanceIndex,
        OpaqueContributor,
        IncompleteStatic,
        NonfiniteStatic,
        RuntimeCountMismatch,
    };
};

struct Case{
    AStringView label;
    u32 runtimeCount = 1u;
    u32 staticCount = 0u;
    Mode::Enum mode = Mode::Identity;
};

struct Oracle{
    f64 minimum[3]{ Limit<f64>::s_Max, Limit<f64>::s_Max, Limit<f64>::s_Max };
    f64 maximum[3]{ -Limit<f64>::s_Max, -Limit<f64>::s_Max, -Limit<f64>::s_Max };
    f64 tolerance[3]{};
};

static_assert(sizeof(Bounds) == NWB_RUNTIME_MESH_BOUNDS_BYTE_SIZE);
static_assert(offsetof(Bounds, flags) == NWB_RUNTIME_MESH_BOUNDS_FLAGS_OFFSET);
static_assert(offsetof(Bounds, maximum) == NWB_RUNTIME_MESH_BOUNDS_MAX_OFFSET);
static_assert(sizeof(Header) == NWB_RT_OPTICAL_SCENE_HEADER_BYTES);
static_assert(sizeof(Instance) == NWB_RT_OPTICAL_INSTANCE_BYTES);
static_assert(sizeof(RuntimeInput) == NWB_OPTICAL_BOUNDS_RUNTIME_INPUT_BYTES);
static_assert(offsetof(RuntimeInput, boundsSlot) == NWB_OPTICAL_BOUNDS_RUNTIME_SLOT_OFFSET);
static_assert(offsetof(RuntimeInput, instanceIndex) == NWB_OPTICAL_BOUNDS_RUNTIME_INSTANCE_OFFSET);
static_assert(sizeof(Push) == NWB_OPTICAL_BOUNDS_FINALIZE_PUSH_BYTES);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ConfigureLastInput(const Mode::Enum mode, Bounds& bounds, RuntimeInput& input){
    switch(mode){
    case Mode::Mirrored:
        input.rows[0].x = -1.0f;
        input.rows[2].z = -1.0f;
        break;
    case Mode::Nonuniform:
        input.rows[0] = { -2.0f, 0.0f, 0.0f, 7.0f };
        input.rows[1] = { 0.0f, 0.125f, 0.0f, -5.0f };
        input.rows[2] = { 0.0f, 0.0f, 3.0f, 11.0f };
        break;
    case Mode::RotationShear:
        input.rows[0] = { 0.6f, -0.8f, 0.25f, 13.0f };
        input.rows[1] = { 0.8f, 0.6f, -0.5f, -17.0f };
        input.rows[2] = { 0.125f, 0.0f, -2.0f, 19.0f };
        break;
    case Mode::LargeCancellation:
        input.rows[0] = { 10000000.0f, -10000000.0f, 0.25f, 100000000.0f };
        input.rows[1].w = -100000000.0f;
        break;
    case Mode::SubnormalScale:{
        const f32 tiny = BitCast<f32>(0x007fffffu);
        bounds.minimum = { -tiny, -tiny, -tiny };
        bounds.maximum = { tiny, tiny, tiny };
        input.rows[0].x = input.rows[1].y = input.rows[2].z = 1e30f;
        break;
    }
    case Mode::Collapsed:
        input.rows[0] = { 0.0f, 0.0f, 0.0f, 1.0f };
        input.rows[1] = { 0.0f, 0.0f, 0.0f, -2.0f };
        input.rows[2] = { 0.0f, 0.0f, 0.0f, 3.0f };
        break;
    case Mode::MissingSlot:
        input.boundsSlot = Limit<u32>::s_Max;
        return false;
    case Mode::InvalidLocalFlag:
        bounds.flags = 0u;
        return false;
    case Mode::NonfiniteLocal:
        bounds.maximum.y = Limit<f32>::s_QuietNaN;
        return false;
    case Mode::InvertedLocal:
        bounds.minimum.z = 12.0f;
        return false;
    case Mode::NonfiniteTransform:
        input.rows[0].x = Limit<f32>::s_QuietNaN;
        return false;
    case Mode::InfiniteTranslation:
        input.rows[2].w = Limit<f32>::s_Infinity;
        return false;
    case Mode::OverflowedTransform:
        input.rows[0].x = Limit<f32>::s_Max;
        return false;
    case Mode::InvalidInstanceIndex:
        input.instanceIndex = Limit<u32>::s_Max;
        return false;
    case Mode::OpaqueContributor:
        return false;
    default:
        break;
    }
    return true;
}

void IncludeTransformedCorners(Oracle& oracle, const Bounds& bounds, const RuntimeInput& input){
    // Enumerate all eight corners in FP64; this does not reuse the shader's signed interval-product reduction.
    for(u32 corner = 0u; corner < 8u; ++corner){
        f64 point[3];
        for(u32 axis = 0u; axis < 3u; ++axis)
            point[axis] = (corner & (1u << axis)) != 0u ? bounds.maximum.raw[axis] : bounds.minimum.raw[axis];
        for(u32 row = 0u; row < 3u; ++row){
            f64 transformed = input.rows[row].w;
            for(u32 axis = 0u; axis < 3u; ++axis)
                transformed += static_cast<f64>(input.rows[row].raw[axis]) * point[axis];
            oracle.minimum[row] = Min(oracle.minimum[row], transformed);
            oracle.maximum[row] = Max(oracle.maximum[row], transformed);
        }
    }
    for(u32 row = 0u; row < 3u; ++row){
        f64 magnitude = Abs(static_cast<f64>(input.rows[row].w)) + 1.0;
        for(u32 axis = 0u; axis < 3u; ++axis)
            magnitude += Abs(static_cast<f64>(input.rows[row].raw[axis]))
                * Max(Abs(static_cast<f64>(bounds.minimum.raw[axis])), Abs(static_cast<f64>(bounds.maximum.raw[axis])));
        oracle.tolerance[row] = Max(oracle.tolerance[row], magnitude * 64.0 * 1.1920928955078125e-7);
    }
}

void RunCase(
    GraphicsBackend::Device& device,
    ComputePipeline& pipeline,
    const Case& testCase,
    Alloc::ScratchArena& scratchArena){
    SCOPED_TRACE(testCase.label.data());
    auto& heap = device.getDescriptorHeap();
    const u32 transparentCount = testCase.runtimeCount + testCase.staticCount;
    const u32 instanceCount = transparentCount == 0u ? 0u : transparentCount + s_ExpectedDualCount;
    const usize sourceBytes = sizeof(Header) + instanceCount * sizeof(Instance);
    constexpr u32 guard = 0x6b6b6b6bu;
    Vector<u32, Alloc::ScratchArena> scene(sourceBytes / sizeof(u32), 0u, scratchArena);
    Vector<u32, Alloc::ScratchArena> sentinels(sourceBytes / sizeof(u32) + 8u, guard, scratchArena);
    Vector<RuntimeInput, Alloc::ScratchArena> inputs(Max(testCase.runtimeCount, 1u), scratchArena);
    Vector<Bounds, Alloc::ScratchArena> localBounds(Max(testCase.runtimeCount, 1u), scratchArena);
    Vector<bool, Alloc::ScratchArena> validInputs(testCase.runtimeCount, true, scratchArena);
    Vector<BufferHandle, Alloc::ScratchArena> buffers(3u + testCase.runtimeCount, scratchArena);
    Vector<GpuDescriptorHandle, Alloc::ScratchArena> descriptors(buffers.size(), scratchArena);
    ScopeExit releaseDescriptors([&]()noexcept{
        for(const GpuDescriptorHandle descriptor : descriptors){
            if(descriptor.valid())
                heap.free(descriptor);
        }
        heap.collectRetired();
    });
    for(usize index = 0u; index < buffers.size(); ++index){
        const usize bytes = index == 0u ? sourceBytes : (index == 1u ? inputs.size() * sizeof(RuntimeInput)
            : (index == s_ExpectedDualCount ? sentinels.size() * sizeof(u32) : sizeof(Bounds)));
        BufferDesc desc;
        desc
            .setByteSize(bytes).setCanHaveRawViews(true).setCanHaveUAVs(index == s_ExpectedDualCount)
            .setInitialState(ResourceStates::Common).setKeepInitialState(true)
        ;
        if(index == s_ExpectedDualCount)
            desc.setCpuAccess(CpuAccessMode::Read);
        buffers[index] = device.createBuffer(desc);
        ASSERT_TRUE(buffers[index]);
        descriptors[index] = heap.allocate(GpuDescriptorClass::StorageBuffer);
        ASSERT_TRUE(descriptors[index].valid());
        ASSERT_TRUE(heap.write(descriptors[index], index == s_ExpectedDualCount ? DescriptorWriteItem::RawBuffer_UAV(0u, buffers[index].get())
            : DescriptorWriteItem::RawBuffer_SRV(0u, buffers[index].get())));
    }
    Header header;
    header.transparentCount = testCase.mode == Mode::RuntimeCountMismatch ? testCase.runtimeCount - 1u : transparentCount;
    if(testCase.mode == Mode::NonfiniteStatic)
        header.minimum.y = Limit<f32>::s_QuietNaN;
    NWB_MEMCPY(scene.data(), sourceBytes, &header, sizeof(header));
    for(u32 index = 0u; index < instanceCount; ++index){
        const bool transparent = index < transparentCount;
        const Instance instance{ index + 101u, index * 3u, index % 3u, 0x80u
            | (transparent ? NWB_RT_OPTICAL_INSTANCE_FLAG_TRANSPARENT | NWB_RT_OPTICAL_INSTANCE_FLAG_BOUNDS_VALID : 0u) };
        NWB_MEMCPY(scene.data() + sizeof(Header) / sizeof(u32) + index * sizeof(Instance) / sizeof(u32),
            sizeof(Instance), &instance, sizeof(instance));
    }
    Oracle oracle;
    if(testCase.staticCount != 0u){
        for(u32 axis = 0u; axis < 3u; ++axis){
            oracle.minimum[axis] = header.minimum.raw[axis];
            oracle.maximum[axis] = header.maximum.raw[axis];
        }
    }
    bool complete = testCase.mode != Mode::IncompleteStatic && testCase.mode != Mode::NonfiniteStatic
        && testCase.mode != Mode::RuntimeCountMismatch;
    for(u32 index = 0u; index < testCase.runtimeCount; ++index){
        RuntimeInput& input = inputs[index];
        input.boundsSlot = descriptors[3u + index].slot();
        input.instanceIndex = testCase.runtimeCount - 1u - index;
        input.rows[0].w = static_cast<f32>(index) * 0.25f;
        input.rows[2].w = -static_cast<f32>(index) * 0.5f;
        if(index == testCase.runtimeCount - 1u){
            validInputs[index] = ConfigureLastInput(testCase.mode, localBounds[index], input);
            if(testCase.mode == Mode::OpaqueContributor){
                const usize flagWord = sizeof(Header) / sizeof(u32) + input.instanceIndex * sizeof(Instance) / sizeof(u32) + 3u;
                scene[flagWord] &= ~NWB_RT_OPTICAL_INSTANCE_FLAG_TRANSPARENT;
            }
        }
        complete = complete && validInputs[index];
        if(validInputs[index])
            IncludeTransformedCorners(oracle, localBounds[index], input);
    }
    const CommandListHandle commandList = device.createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
    ASSERT_TRUE(commandList->tryWriteBuffer(*buffers[0], scene.data(), sourceBytes));
    ASSERT_TRUE(commandList->tryWriteBuffer(*buffers[1], inputs.data(), inputs.size() * sizeof(RuntimeInput)));
    ASSERT_TRUE(commandList->tryWriteBuffer(*buffers[2], sentinels.data(), sentinels.size() * sizeof(u32)));
    for(u32 index = 0u; index < testCase.runtimeCount; ++index)
        ASSERT_TRUE(commandList->tryWriteBuffer(*buffers[3u + index], &localBounds[index], sizeof(Bounds)));
    for(usize index = 0u; index < buffers.size(); ++index){
        const ResourceStates::Mask state = index == s_ExpectedDualCount ? ResourceStates::UnorderedAccess : ResourceStates::ShaderResource;
        commandList->setBufferState(buffers[index].get(), state);
    }
    commandList->commitBarriers();
    ComputeState state;
    state.setPipeline(&pipeline);
    commandList->setComputeState(state);
    heap.bindCompute(*commandList, pipeline);
    const Push push{ descriptors[0].slot(), descriptors[1].slot(), descriptors[2].slot(), instanceCount,
        testCase.runtimeCount, testCase.mode == Mode::IncompleteStatic ? 0u : 1u };
    commandList->setPushConstants(&push, sizeof(push));
    commandList->dispatch(1u, 1u, 1u);
    commandList->close();
    ASSERT_FALSE(commandList->commandRecordingFailed());
    CommandList* const submitted[] = { commandList.get() };
    const QueueSubmissionToken token = device.executeCommandLists(
        submitted, LengthOf(submitted), commandList->getDescription().physicalQueue, QueueSubmissionDesc{}
    );
    ASSERT_TRUE(token.valid());
    ASSERT_TRUE(device.waitForIdle());
    const u32* const mapped = static_cast<const u32*>(device.mapBuffer(*buffers[2], CpuAccessMode::Read));
    ASSERT_NE(mapped, nullptr);
    ScopeExit unmap([&]()noexcept{ device.unmapBuffer(*buffers[2]); });
    const Header* const actual = reinterpret_cast<const Header*>(mapped);
    EXPECT_EQ(actual->flags, 0x40u | (complete ? NWB_RT_OPTICAL_SCENE_FLAG_BOUNDS_VALID : 0u));
    EXPECT_EQ(actual->transparentCount, header.transparentCount);
    for(u32 axis = 0u; axis < 3u; ++axis){
        if(!complete || transparentCount == 0u){
            EXPECT_EQ(actual->minimum.raw[axis], 0.0f);
            EXPECT_EQ(actual->maximum.raw[axis], 0.0f);
        }
        else{
            EXPECT_LE(actual->minimum.raw[axis], oracle.minimum[axis]);
            EXPECT_GE(actual->maximum.raw[axis], oracle.maximum[axis]);
            EXPECT_GE(actual->minimum.raw[axis], oracle.minimum[axis] - oracle.tolerance[axis]);
            EXPECT_LE(actual->maximum.raw[axis], oracle.maximum[axis] + oracle.tolerance[axis]);
        }
    }
    for(u32 index = 0u; index < instanceCount; ++index){
        const usize offset = sizeof(Header) / sizeof(u32) + index * sizeof(Instance) / sizeof(u32);
        for(u32 field = 0u; field < 3u; ++field)
            EXPECT_EQ(mapped[offset + field], scene[offset + field]);
        u32 expectedFlags = scene[offset + 3u];
        for(u32 contributor = 0u; contributor < testCase.runtimeCount; ++contributor){
            if(inputs[contributor].instanceIndex == index){
                expectedFlags &= ~NWB_RT_OPTICAL_INSTANCE_FLAG_BOUNDS_VALID;
                if(validInputs[contributor])
                    expectedFlags |= NWB_RT_OPTICAL_INSTANCE_FLAG_BOUNDS_VALID;
            }
        }
        EXPECT_EQ(mapped[offset + 3u], expectedFlags) << "instance " << index;
    }
    for(usize word = sourceBytes / sizeof(u32); word < sentinels.size(); ++word)
        EXPECT_EQ(mapped[word], guard) << "guard word " << word;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(RuntimeBoundsKernelTest, OpticalUnionEnclosesAffineCornerOracleAndRejectsIncompleteContributors){
    using namespace __hidden_optical_union_kernel_tests;
    Alloc::ScratchArena scratchArena(Name("tests/smoke/runtime_bounds_kernel/optical_union"));
    ComputePipelineHandle pipeline;
    ASSERT_TRUE(loadKernel("raytrace", "optical_bounds_finalize_cs", sizeof(Push), scratchArena, pipeline));
    const Case cases[] = {
        { "identity" },
        { "mirrored transform", 1u, 0u, Mode::Mirrored },
        { "nonuniform scale", 1u, 0u, Mode::Nonuniform },
        { "rotated sheared transform", 1u, 0u, Mode::RotationShear },
        { "large translated cancellation", 1u, 0u, Mode::LargeCancellation },
        { "subnormal local positions at large scale", 1u, 0u, Mode::SubnormalScale },
        { "finite collapsed affine transform", 1u, 0u, Mode::Collapsed },
        { "mixed static and runtime bounds", 3u, 1u, Mode::RotationShear },
        { "partial final lane and strided copy", 65u, 1u },
        { "static only", 0u, 1u },
        { "empty complete scene", 0u, 0u },
        { "empty incomplete scene", 0u, 0u, Mode::IncompleteStatic },
        { "missing local selector", s_ExpectedDualCount, 1u, Mode::MissingSlot },
        { "invalid final contributor poisons union", 65u, 0u, Mode::InvalidLocalFlag },
        { "nonfinite local bounds", s_ExpectedDualCount, 0u, Mode::NonfiniteLocal },
        { "inverted local bounds", s_ExpectedDualCount, 0u, Mode::InvertedLocal },
        { "nonfinite transform", s_ExpectedDualCount, 1u, Mode::NonfiniteTransform },
        { "infinite translation", s_ExpectedDualCount, 0u, Mode::InfiniteTranslation },
        { "overflowing finite transform", s_ExpectedDualCount, 0u, Mode::OverflowedTransform },
        { "invalid emitted instance index", s_ExpectedDualCount, 0u, Mode::InvalidInstanceIndex },
        { "opaque runtime contributor", s_ExpectedDualCount, 0u, Mode::OpaqueContributor },
        { "incomplete static subset", s_ExpectedDualCount, 1u, Mode::IncompleteStatic },
        { "nonfinite static subset", s_ExpectedDualCount, 1u, Mode::NonfiniteStatic },
        { "runtime count exceeds transparent count", s_ExpectedDualCount, 0u, Mode::RuntimeCountMismatch },
    };
    for(const Case& testCase : cases)
        RunCase(device(), *pipeline, testCase, scratchArena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

