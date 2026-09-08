// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_mesh/skinning/live_state_buffers.h>

#include <tests/common/graphics_metadata_test_objects.h>

#include <global/text_utils.h>
#include <global/timer.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_skinning_live_state_buffers_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using BufferVector = Vector<Core::BufferHandle, Core::Alloc::GlobalArena>;

inline constexpr Core::BufferHandle MeshSkinningRuntimeInstance::* s_InstanceBuffers[] = {
    &MeshSkinningRuntimeInstance::restPositionBuffer,
    &MeshSkinningRuntimeInstance::restNormalBuffer,
    &MeshSkinningRuntimeInstance::restTangentBuffer,
    &MeshSkinningRuntimeInstance::skinnedPositionBuffer,
    &MeshSkinningRuntimeInstance::skinnedNormalBuffer,
    &MeshSkinningRuntimeInstance::skinnedTangentBuffer,
    &MeshSkinningRuntimeInstance::uv0Buffer,
    &MeshSkinningRuntimeInstance::colorBuffer,
    &MeshSkinningRuntimeInstance::meshletDescBuffer,
    &MeshSkinningRuntimeInstance::meshletBoundsBuffer,
    &MeshSkinningRuntimeInstance::meshletPositionRefDeltaBuffer,
    &MeshSkinningRuntimeInstance::meshletAttributeRefDeltaBuffer,
    &MeshSkinningRuntimeInstance::meshletLocalVertexRefBuffer,
    &MeshSkinningRuntimeInstance::meshletPrimitiveIndexBuffer,
    &MeshSkinningRuntimeInstance::attributeSkinBuffer,
    &MeshSkinningRuntimeInstance::triangleIndexBuffer,
    &MeshSkinningRuntimeInstance::attributeBuffer,
};
inline constexpr usize s_BuffersPerInstance = LengthOf(s_InstanceBuffers) + 3u;

struct LiveStateContext{
    Core::Alloc::GlobalArena arena{ Name("tests/skinning_live_state/inputs") };
    Core::GraphicsAllocator graphicsAllocator{ arena };
    Core::Alloc::CpuTaskScheduler cpuScheduler{ 0u };
    Core::GraphicsBackend::VulkanContext context{ graphicsAllocator, cpuScheduler, 1u };
    Core::GraphicsBackend::VulkanAllocator allocator{ context };
    BufferVector buffers{ arena };
    Vector<MeshSkinningRuntimeInstance, Core::Alloc::GlobalArena> instances{ arena };
    bool sharedBuffers = false;

    void initialize(const usize instanceCount, const bool shared = false){
        sharedBuffers = shared;
        const usize bufferCount = instanceCount == 0u ? 0u : (shared ? 1u : instanceCount) * s_BuffersPerInstance;
        buffers.reserve(bufferCount);
        for(usize index = 0u; index < bufferCount; ++index){
            Core::Buffer* const buffer = Tests::NewMetadataOnlyBuffer(
                arena,
                context,
                allocator,
                Core::BufferDesc{}.setByteSize(256u)
            );
            buffers.emplace_back(buffer, Core::BufferHandle::deleter_type(&arena), AdoptRef);
        }
        instances.reserve(instanceCount);
        for(usize index = 0u; index < instanceCount; ++index){
            MeshSkinningRuntimeInstance& instance = instances.emplace_back(arena);
            instance.handle.value = (1ull << 40u) + index + 1u;
            instance.entity = Core::ECS::EntityID(static_cast<u32>(index));
            instance.sourceName = Name("tests/skinning_live_state/source");
            instance.localBounds.minBounds.w = s_RuntimeMeshBoundsValidFlag | s_RuntimeMeshBoundsFiniteFlag;
            instance.restPositions.resize(1u);
            instance.restNormals.resize(1u);
            instance.restTangents.resize(1u);
            instance.uv0.resize(1u);
            instance.colors.resize(1u);
            instance.meshlets.resize(1u);
            instance.meshletBounds.resize(1u);
            instance.meshletPositionRefDeltas.resize(1u);
            instance.meshletAttributeRefDeltas.resize(1u);
            instance.meshletLocalVertexRefs.resize(1u);
            instance.meshletPrimitiveIndices.resize(1u);
            instance.attributeSkins.resize(1u);
            instance.skin.resize(1u);
            instance.meshletPositionRefCount = 1u;
            instance.meshletAttributeRefCount = 1u;
            instance.editRevision = 7u;
            instance.dirtyFlags = RuntimeMeshDirtyFlag::None;
            const usize firstBuffer = shared ? 0u : index * s_BuffersPerInstance;
            for(usize slot = 0u; slot < LengthOf(s_InstanceBuffers); ++slot)
                instance.*s_InstanceBuffers[slot] = buffers[firstBuffer + slot];
        }
    }

    [[nodiscard]] MeshSkinningStateBufferResources resources(const usize index)const{
        const usize firstBuffer = sharedBuffers ? 0u : index * s_BuffersPerInstance;
        return {
            .editRevision = 7u,
            .skinBuffer = &buffers[firstBuffer + 17u],
            .jointPaletteBuffer = &buffers[firstBuffer + 18u],
            .bindlessResourceSlotsBuffer = &buffers[firstBuffer + 19u],
        };
    }

    void collect(Core::Alloc::ScratchArena& scratch, BufferVector& outBuffers)const{
        MeshSkinningStateBufferCollector collector(scratch, outBuffers);
        for(usize index = 0u; index < instances.size(); ++index)
            collector.collect(&instances[index], resources(index));
    }
};

TEST(SkinningLiveStateBuffers, PreservesAllTwentyRolesFirstOccurrenceAndOwningHandles){
    LiveStateContext context;
    context.initialize(2u);
    Core::Alloc::GlobalArena outputArena(Name("tests/skinning_live_state/output"));
    Core::Alloc::ScratchArena scratch(Name("tests/skinning_live_state/scratch"));
    BufferVector output(outputArena);
    ASSERT_TRUE(context.instances[0u].valid());
    ASSERT_TRUE(context.instances[1u].valid());
    const auto initialReferences = context.buffers[0u]->getReferenceCount();
    {
        MeshSkinningStateBufferCollector collector(scratch, output);
        collector.collect(&context.instances[1u], context.resources(1u));
        collector.collect(&context.instances[0u], context.resources(0u));
        collector.collect(&context.instances[1u], context.resources(1u));
    }
    ASSERT_EQ(output.size(), 40u);
    for(usize index = 0u; index < s_BuffersPerInstance; ++index){
        EXPECT_EQ(output[index].get(), context.buffers[index + s_BuffersPerInstance].get());
        EXPECT_EQ(output[index + s_BuffersPerInstance].get(), context.buffers[index].get());
    }
    EXPECT_EQ(context.buffers[0u]->getReferenceCount(), initialReferences + 1u);
    EXPECT_EQ(output.get_allocator().arenaPtr(), &outputArena);
    context.instances.clear();
    context.buffers.clear();
    for(const Core::BufferHandle& buffer : output){
        EXPECT_EQ(buffer->getReferenceCount(), 1u);
        EXPECT_EQ(buffer->getCreationDescription().byteSize, 256u);
    }
    output.clear();
}

TEST(SkinningLiveStateBuffers, FiltersMissingInstancesInvalidHandlesAndIncompleteRuntimePayloads){
    LiveStateContext context;
    context.initialize(1u);
    Core::Alloc::ScratchArena scratch(Name("tests/skinning_live_state/invalid_scratch"));
    BufferVector output(context.arena);
    MeshSkinningRuntimeInstance& instance = context.instances[0u];
    for(u32 failure = 0u; failure < 6u; ++failure){
        const RuntimeMeshHandle handle = instance.handle;
        const Core::ECS::EntityID entity = instance.entity;
        const Name sourceName = instance.sourceName;
        const Core::BufferHandle restPosition = instance.restPositionBuffer;
        const i32 boundsFlags = instance.localBounds.minBounds.w;
        switch(failure){
        case 0u: instance.handle.reset(); break;
        case 1u: instance.entity = Core::ECS::ENTITY_ID_INVALID; break;
        case 2u: instance.sourceName = NAME_NONE; break;
        case 3u: instance.dirtyFlags = RuntimeMeshDirtyFlag::GpuUploadDirty; break;
        case 4u: instance.restPositionBuffer = nullptr; break;
        case 5u: instance.localBounds.minBounds.w = 0; break;
        }
        {
            MeshSkinningStateBufferCollector collector(scratch, output);
            collector.collect(nullptr, context.resources(0u));
            collector.collect(&instance, context.resources(0u));
        }
        EXPECT_TRUE(output.empty());
        instance.handle = handle;
        instance.entity = entity;
        instance.sourceName = sourceName;
        instance.restPositionBuffer = restPosition;
        instance.localBounds.minBounds.w = boundsFlags;
        instance.dirtyFlags = RuntimeMeshDirtyFlag::None;
        ASSERT_TRUE(instance.valid());
    }
    instance.restTangents.clear();
    context.collect(scratch, output);
    EXPECT_TRUE(output.empty());
    instance.restTangents.resize(1u);
    context.collect(scratch, output);
    EXPECT_EQ(output.size(), 20u);
}

TEST(SkinningLiveStateBuffers, RechecksResourceRevisionOptionalRolesAndBufferReplacementEveryCollection){
    LiveStateContext context;
    context.initialize(2u);
    Core::Alloc::ScratchArena scratch(Name("tests/skinning_live_state/revision_scratch"));
    BufferVector output(context.arena);
    MeshSkinningRuntimeInstance& instance = context.instances[0u];
    MeshSkinningStateBufferResources resources = context.resources(0u);
    ++resources.editRevision;
    {
        MeshSkinningStateBufferCollector collector(scratch, output);
        collector.collect(&instance, resources);
    }
    EXPECT_EQ(output.size(), 17u);
    resources = context.resources(0u);
    instance.triangleIndexBuffer = nullptr;
    instance.attributeBuffer = nullptr;
    {
        MeshSkinningStateBufferCollector collector(scratch, output);
        collector.collect(&instance, resources);
    }
    ASSERT_EQ(output.size(), 18u);
    EXPECT_EQ(output[15u].get(), resources.skinBuffer->get());
    instance.restPositionBuffer = context.buffers[20u];
    {
        MeshSkinningStateBufferCollector collector(scratch, output);
        collector.collect(&instance, {});
    }
    ASSERT_EQ(output.size(), 15u);
    EXPECT_EQ(output[0u].get(), context.buffers[20u].get());
    for(const Core::BufferHandle& buffer : output)
        EXPECT_NE(buffer.get(), context.buffers[0u].get());
    {
        MeshSkinningStateBufferCollector collector(scratch, output);
    }
    EXPECT_TRUE(output.empty());
}

TEST(SkinningLiveStateBuffers, DeduplicatesSharedResourcesAcrossManyLiveInstancesAndChangedLanes){
    LiveStateContext context;
    context.initialize(64u, true);
    Core::Alloc::ScratchArena scratch(Name("tests/skinning_live_state/shared_scratch"));
    BufferVector output(context.arena);
    context.collect(scratch, output);
    ASSERT_EQ(output.size(), 20u);
    for(usize index = 0u; index < 20u; ++index)
        EXPECT_EQ(output[index].get(), context.buffers[index].get());
    Swap(context.instances[32u].restNormalBuffer, context.instances[32u].restTangentBuffer);
    context.collect(scratch, output);
    ASSERT_EQ(output.size(), 20u);
    EXPECT_EQ(output[1u].get(), context.buffers[1u].get());
    EXPECT_EQ(output[2u].get(), context.buffers[2u].get());
}

TEST(SkinningLiveStateBuffers, SharedCollectionsDoNotAllocateScratchStorage){
    LiveStateContext context;
    context.initialize(64u, true);
    Core::Alloc::GlobalArena outputArena(Name("tests/skinning_live_state/shared_output"));
    Core::Alloc::ScratchArena scratch(Name("tests/skinning_live_state/shared_no_allocations"));
    BufferVector output(outputArena);
    const ArenaMemoryStats initial = scratch.memoryStats();
    for(usize repeat = 0u; repeat < 3u; ++repeat){
        context.collect(scratch, output);
        ASSERT_EQ(output.size(), s_BuffersPerInstance);
        const ArenaMemoryStats after = scratch.memoryStats();
        EXPECT_EQ(after.allocationCount, initial.allocationCount);
        EXPECT_EQ(after.usedBytes, initial.usedBytes);
        EXPECT_EQ(after.reservedBytes, initial.reservedBytes);
        EXPECT_EQ(after.peakUsedBytes, initial.peakUsedBytes);
    }
}

TEST(SkinningLiveStateBuffers, PromotedCollectionsPreserveOrderAndRebuildAfterRuntimeMutation){
    LiveStateContext context;
    context.initialize(32u);
    Core::Alloc::GlobalArena outputArena(Name("tests/skinning_live_state/promoted_output"));
    Core::Alloc::ScratchArena scratch(Name("tests/skinning_live_state/promoted_scratch"));
    BufferVector output(outputArena);
    {
        MeshSkinningStateBufferCollector collector(scratch, output);
        for(usize index = 0u; index < context.instances.size(); ++index){
            const usize instanceIndex = (index * 13u) % context.instances.size();
            collector.collect(&context.instances[instanceIndex], context.resources(instanceIndex));
        }
        ASSERT_EQ(output.size(), context.buffers.size());
        for(usize index = 0u; index < context.instances.size(); ++index){
            const usize instanceIndex = (index * 13u) % context.instances.size();
            for(usize role = 0u; role < s_BuffersPerInstance; ++role)
                EXPECT_EQ(output[index * s_BuffersPerInstance + role].get(), context.buffers[instanceIndex * s_BuffersPerInstance + role].get());
        }
        const ArenaMemoryStats collected = scratch.memoryStats();
        for(usize index = 0u; index < context.instances.size(); ++index)
            collector.collect(&context.instances[index], context.resources(index));
        EXPECT_EQ(output.size(), context.buffers.size());
        EXPECT_EQ(scratch.memoryStats().allocationCount, collected.allocationCount);
        EXPECT_EQ(scratch.memoryStats().usedBytes, collected.usedBytes);
        EXPECT_EQ(scratch.memoryStats().reservedBytes, collected.reservedBytes);
    }

    context.instances[0u].restPositionBuffer = context.buffers[s_BuffersPerInstance];
    const ArenaMemoryStats beforeSmallCollection = scratch.memoryStats();
    {
        MeshSkinningStateBufferCollector collector(scratch, output);
        collector.collect(&context.instances[0u], context.resources(0u));
    }
    ASSERT_EQ(output.size(), s_BuffersPerInstance);
    EXPECT_EQ(output[0u].get(), context.buffers[s_BuffersPerInstance].get());
    EXPECT_EQ(context.buffers[0u]->getReferenceCount(), 1u);
    EXPECT_EQ(scratch.memoryStats().allocationCount, beforeSmallCollection.allocationCount);

    context.collect(scratch, output);
    ASSERT_EQ(output.size(), context.buffers.size() - 1u);
    EXPECT_EQ(output[0u].get(), context.buffers[s_BuffersPerInstance].get());
    usize outputIndex = 1u;
    for(usize index = 1u; index < context.buffers.size(); ++index){
        if(index == s_BuffersPerInstance)
            continue;
        EXPECT_EQ(output[outputIndex].get(), context.buffers[index].get());
        ++outputIndex;
    }
    EXPECT_EQ(outputIndex, output.size());
}

static void BenchmarkCollection(const usize instanceCount, const usize repeatCount, const bool shared){
    LiveStateContext context;
    context.initialize(instanceCount, shared);
    Core::Alloc::GlobalArena outputArena(Name("tests/skinning_live_state/benchmark_output"));
    u64 elapsed = 0u;
    u64 outputCount = 0u;
    u64 scratchPeak = 0u;
    u64 scratchReserved = 0u;
    for(usize repeat = 0u; repeat < repeatCount; ++repeat){
        const Timer begin = TimerNow();
        {
            Core::Alloc::ScratchArena scratch(Name("tests/skinning_live_state/benchmark_scratch"));
            BufferVector output(outputArena);
            context.collect(scratch, output);
            outputCount += output.size();
            scratchPeak = Max(scratchPeak, scratch.memoryStats().peakUsedBytes);
            scratchReserved = Max(scratchReserved, scratch.memoryStats().reservedBytes);
        }
        elapsed += DurationInNS<u64>(TimerNow(), begin);
    }
    EXPECT_EQ(outputCount, repeatCount * (shared ? 1u : instanceCount) * s_BuffersPerInstance);
    EXPECT_EQ(outputArena.memoryStats().usedBytes, 0u);
    char elapsedText[32u] = {};
    char repeatText[32u] = {};
    char outputText[32u] = {};
    char scratchPeakText[32u] = {};
    char scratchReservedText[32u] = {};
    testing::Test::RecordProperty("live_buffers_ns", FormatDecimal(elapsed, elapsedText).data());
    testing::Test::RecordProperty("live_buffers_repeat_count", FormatDecimal(repeatCount, repeatText).data());
    testing::Test::RecordProperty("live_buffers_output_count", FormatDecimal(outputCount, outputText).data());
    testing::Test::RecordProperty("live_buffers_scratch_peak_bytes", FormatDecimal(scratchPeak, scratchPeakText).data());
    testing::Test::RecordProperty("live_buffers_scratch_reserved_bytes", FormatDecimal(scratchReserved, scratchReservedText).data());
}

TEST(SkinningLiveStateBuffers, DISABLED_BenchmarkSingleInstance){
    BenchmarkCollection(1u, 1024u, false);
}

TEST(SkinningLiveStateBuffers, DISABLED_BenchmarkUniqueInstances){
    BenchmarkCollection(1024u, 1u, false);
}

TEST(SkinningLiveStateBuffers, DISABLED_BenchmarkSharedInstances){
    BenchmarkCollection(1024u, 8u, true);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

