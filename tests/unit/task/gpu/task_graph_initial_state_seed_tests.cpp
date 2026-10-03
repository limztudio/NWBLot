// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/unit/task/gpu/task_graph_test_utils.h>
#include <tests/common/vulkan_test_sync.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuPacketInitialStateSeedTestAccess final{
public:
    struct Source{
        GpuTaskId m_task;
        const CommandListResourceStateHandoff& m_states;
    };


public:
    template<usize sourceCount>
    [[nodiscard]] static bool build(
        GpuRecordedGraph& recorded,
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiled,
        const GpuTaskId consumer,
        const Source (&sources)[sourceCount],
        CommandListResourceStateHandoff& result){
        result.reset();
        const GpuTaskGraph::DeclarationReadView declarations(graph);
        const GpuCompiledGraph::ReadView plan(compiled);
        GpuRecordedGraph::ArtifactOperation operation(recorded, GpuRecordedGraph::ArtifactOperationMode::Exclusive);
        if(!declarations.valid() || !plan.validFor(declarations) || !operation.valid())
            return false;
        for(const Source& source : sources){
            const GpuSubmissionPacketId packet = plan.packetForTask(source.m_task);
            CommandListResourceStateHandoff* const stored = recorded.packetStateSeed(packet, operation);
            if(!stored || !stored->copyFrom(source.m_states))
                return false;
        }
        GpuRecordedGraph::PacketRecordingScratch* const scratch = recorded.serialRecordingScratch(operation);
        if(!scratch || !scratch->stateFanInScratchArena)
            return false;
        const CommandListResourceStateHandoff* initial = nullptr;
        const bool built = recorded.buildPacketInitialStateSeed(
            *scratch,
            *scratch->stateFanInScratchArena,
            graph,
            declarations,
            compiled,
            plan,
            operation,
            plan.packetForTask(consumer),
            initial
        );
        for(const Source& source : sources){
            const GpuSubmissionPacketId packet = plan.packetForTask(source.m_task);
            const CommandListResourceStateHandoff* const stored = recorded.packetStateSeed(packet, operation);
            EXPECT_NE(stored, nullptr);
            if(stored)
                EXPECT_TRUE(stored->equivalentTo(source.m_states));
        }
        if(!built){
            EXPECT_EQ(initial, nullptr);
            return false;
        }
        return initial && result.copyFrom(*initial);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_packet_initial_state_seed_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace Tests::TaskGraphTestUtils;
using Access = Core::GraphicsBackend::BackendTestDispatchAccess;
using SeedAccess = Core::GpuPacketInitialStateSeedTestAccess;
using Handoff = Core::CommandListResourceStateHandoff;

inline constexpr u16 s_DeviceGeneration = 1u;
inline constexpr u64 s_BufferBytes = 16u;
inline constexpr u64 s_HalfBytes = s_BufferBytes / 2u;
inline constexpr usize s_TwoRanges = 2u;
inline constexpr usize s_NineRanges = 9u;
inline constexpr usize s_SwitchRanges = 6u;
inline constexpr usize s_NumberBytes = 32u;
inline constexpr Core::GpuPhysicalQueueId s_Owner{ .index = 0u, .deviceGeneration = s_DeviceGeneration };
inline constexpr Core::GpuPhysicalQueueId s_ExternalOwner{ .index = 1u, .deviceGeneration = s_DeviceGeneration };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SeedContext{
    TestArena m_testArena;
    Core::GraphicsAllocator m_graphicsAllocator{ m_testArena.arena };
    Core::CpuTaskScheduler m_cpuScheduler{ 0u };
    Core::GraphicsBackend::VulkanContext m_context{ m_graphicsAllocator, m_cpuScheduler, s_DeviceGeneration };
    Core::GraphicsBackend::VulkanAllocator m_allocator{ m_context };
    Core::GraphicsVector<Core::BufferHandle> m_buffers{ m_testArena.arena };
    Core::GraphicsVector<Core::GpuGraphResourceId> m_resources{ m_testArena.arena };
    Core::GpuTaskGraph m_graph{ m_testArena.arena };
    Core::GpuTaskGraphAnalysis m_analysis{ m_testArena.arena };
    Core::GpuTaskGraphQueueAssignments m_assignments{ m_testArena.arena };
    Core::GpuCompiledGraph m_compiled{ m_testArena.arena };
    Core::GpuRecordedGraph m_recorded{ m_testArena.arena };
    Core::Alloc::ScratchArena m_referenceScratch{ Name("tests/task/gpu/initial_seed_reference") };

    [[nodiscard]] bool initialize(const usize count, const u64 bytes = s_BufferBytes){
        m_buffers.reserve(count);
        m_resources.reserve(count);
        for(usize index = 0u; index < count; ++index){
            Core::Buffer* const buffer = NewMetadataOnlyBuffer(
                m_testArena.arena,
                m_context,
                m_allocator,
                Core::BufferDesc{}.setByteSize(bytes).setInitialState(Core::ResourceStates::Common)
            );
            if(!buffer)
                return false;
            m_buffers.emplace_back(buffer, Core::BufferHandle::deleter_type(&m_testArena.arena), AdoptRef);
            char suffix[s_NumberBytes] = {};
            m_resources.push_back(m_graph.importBuffer(
                m_buffers.back(),
                Core::GpuGraphResourceDesc{}
                    .setIdentity(DeriveName(Name("tests/task/gpu/initial_seed_buffer/"), FormatDecimal(index, suffix)))
                    .setMarkerLabel("Initial seed buffer")
                    .setType(Core::GpuGraphResourceType::Buffer)
                    .setInitialState(Core::ResourceStates::Common)
            ));
            if(!m_resources.back().valid())
                return false;
        }
        return true;
    }

    [[nodiscard]] Core::GpuTaskId addTask(
        const Name& identity,
        const Core::GpuTaskResourceUse* uses,
        const usize useCount,
        const Core::GpuTaskId* dependencies = nullptr,
        const usize dependencyCount = 0u,
        const Core::GpuTaskSchedulingHint& scheduling = {},
        const Handoff* external = nullptr){
        const Core::GpuTaskExternalStateSource stateSource{ .states = external };
        Core::GpuTaskDesc desc;
        desc
            .setIdentity(identity)
            .setMarkerLabel("Initial seed task")
            .setScheduling(scheduling)
            .setDependencies(dependencies, dependencyCount)
            .setResourceUses(uses, useCount)
            .setExternalStateSources(external ? &stateSource : nullptr, external ? 1u : 0u)
        ;
        return m_graph.addTask(desc, GraphicsCommands());
    }

    [[nodiscard]] bool compile(const bool includeExternalSourceQueue = false){
        const Core::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(), DedicatedComputeQueue(s_ExternalOwner.index) };
        const Core::GpuPhysicalQueueTopology topology{
            .queues = queues,
            .queueCount = includeExternalSourceQueue ? LengthOf(queues) : 1u,
        };
        return Compile(m_graph, m_analysis, topology, m_assignments, m_compiled) && m_recorded.tryReset(m_compiled);
    }

    void addState(
        Handoff& states,
        const usize buffer,
        const Core::BufferRange range,
        const Core::ResourceStates::Mask state,
        const Core::GpuPhysicalQueueId destination = {},
        const Core::GpuPhysicalQueueId owner = s_Owner){
        Access::stateHandoffBuffers(states).push_back({
            .buffer = m_buffers[buffer].get(),
            .state = state,
            .ownerQueue = owner,
            .releaseDestinationQueue = destination,
            .range = range,
        });
        Access::validateStateHandoff(states, s_DeviceGeneration);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Core::GpuTaskResourceUse BufferUse(
    const Core::GpuGraphResourceId resource,
    const Core::BufferRange range,
    const Core::ResourceStates::Mask state,
    const Core::GpuTaskResourceAccess::Enum access){
    return Core::GpuTaskResourceUse{
        .resource = resource,
        .range = Core::GpuTaskResourceRange{ .bufferRange = range },
        .requiredState = state,
        .access = access,
    };
}

template<usize sourceCount>
[[nodiscard]] bool SequentialFold(
    SeedContext& context,
    const Core::GpuTaskId consumer,
    const SeedAccess::Source (&sources)[sourceCount],
    Handoff& result){
    result.reset();
    const Core::GpuTaskGraph::DeclarationReadView declarations(context.m_graph);
    const Core::GpuCompiledGraph::ReadView plan(context.m_compiled);
    const Core::GpuCompiledTaskView task = plan.findTask(consumer);
    if(!task.valid())
        return false;
    Handoff subset(context.m_testArena.arena);
    Handoff merged(context.m_testArena.arena);
    for(usize index = 0u; index < task.plan->prologueStateSeedCount; ++index){
        const Core::GpuPacketStateSeed& seed = task.prologueStateSeeds[index];
        const Handoff* sourceStates = nullptr;
        for(const auto& source : sources){
            if(plan.packetForTask(source.m_task) == seed.sourcePacket)
                sourceStates = &source.m_states;
        }
        Core::Buffer* const buffer = declarations.bufferForResource(seed.resource);
        subset.reset();
        if(
            !sourceStates || !buffer
            || !subset.buildBufferRangeSubset(*sourceStates, buffer, seed.range.bufferRange)
            || subset.empty()
        )
            return false;
        if(!result.valid()){
            if(!result.copyFrom(subset))
                return false;
            continue;
        }
        const Handoff* const branches[] = { &subset };
        if(!merged.buildFanIn(result, branches, LengthOf(branches), context.m_referenceScratch) || !result.copyFrom(merged))
            return false;
    }
    return result.valid();
}

void DeclareRangeConsumer(
    SeedContext& context,
    Core::GpuTaskId& producer,
    Core::GpuTaskId& consumer,
    const usize rangeCount){
    const u64 bytes = context.m_buffers[0u]->getCreationDescription().byteSize;
    const Core::GpuTaskResourceUse writer[] = {
        BufferUse(context.m_resources[0u], { 0u, bytes }, Core::ResourceStates::CopyDest, Core::GpuTaskResourceAccess::Write),
    };
    producer = context.addTask(Name("tests/task/gpu/initial_seed_producer"), writer, LengthOf(writer));
    ASSERT_TRUE(producer.valid());
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> uses(context.m_referenceScratch);
    uses.reserve(rangeCount);
    for(usize index = 0u; index < rangeCount; ++index){
        const usize rangeIndex = rangeCount - index - 1u;
        uses.push_back(BufferUse(
            context.m_resources[0u],
            { rangeIndex * s_HalfBytes, s_HalfBytes },
            Core::ResourceStates::CopySource,
            Core::GpuTaskResourceAccess::Read
        ));
    }
    consumer = context.addTask(Name("tests/task/gpu/initial_seed_consumer"), uses.data(), uses.size(), &producer, 1u);
    ASSERT_TRUE(consumer.valid());
}


TEST(GpuPacketInitialStateSeed, ReleasedAccelStorageRejectsPartialPrefixBeforeSameSourceHalvesCanHideIt){
    SeedContext context;
    ASSERT_TRUE(context.initialize(1u));
    Handoff source(context.m_testArena.arena);
    Handoff released(context.m_testArena.arena);
    context.addState(source, 0u, { 0u, s_BufferBytes }, Core::ResourceStates::CopyDest);
    context.addState(
        released,
        0u,
        { 0u, s_BufferBytes },
        Core::ResourceStates::CopyDest,
        s_Owner,
        s_ExternalOwner
    );
    Handoff sourceBefore(context.m_testArena.arena);
    Handoff releasedBefore(context.m_testArena.arena);
    ASSERT_TRUE(sourceBefore.copyFrom(source));
    ASSERT_TRUE(releasedBefore.copyFrom(released));
    const Core::GpuTaskResourceUse writer[] = {
        BufferUse(
            context.m_resources[0u],
            { 0u, s_BufferBytes },
            Core::ResourceStates::CopyDest,
            Core::GpuTaskResourceAccess::Write
        ),
    };
    const auto producer = context.addTask(Name("tests/task/gpu/seed_release_producer"), writer, LengthOf(writer));
    ASSERT_TRUE(producer.valid());
    Core::RayTracingAccelStruct* const accelStructObject = NewArenaObject<Core::RayTracingAccelStruct>(
        context.m_testArena.arena,
        context.m_context
    );
    ASSERT_NE(accelStructObject, nullptr);
    Core::RayTracingAccelStructHandle accelStruct(
        accelStructObject,
        Core::RayTracingAccelStructHandle::deleter_type(&context.m_testArena.arena),
        AdoptRef
    );
    Core::BufferHandle& backing = const_cast<Core::BufferHandle&>(accelStruct->getBackingBufferHandle());
    backing = context.m_buffers[0u];
    ASSERT_EQ(accelStruct->getBackingBuffer(), context.m_buffers[0u].get());
    const Core::GpuGraphResourceId accelResource = context.m_graph.importAccelStruct(
        accelStruct,
        Core::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task/gpu/seed_release_accel_storage"))
            .setMarkerLabel("Released acceleration storage")
            .setType(Core::GpuGraphResourceType::AccelStruct)
            .setInitialState(Core::ResourceStates::Common)
    );
    ASSERT_TRUE(accelResource.valid());
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(context.m_graph);
        ASSERT_EQ(declarations.accelStructForResource(accelResource)->getBackingBuffer(), context.m_buffers[0u].get());
    }
    // Acceleration-structure seeds select the whole backing allocation without inventing a buffer-history use.
    const Core::GpuTaskResourceUse stageUses[] = {
        Core::GpuTaskResourceUse{
            .resource = accelResource,
            .range = {},
            .requiredState = Core::ResourceStates::AccelStructRead,
            .access = Core::GpuTaskResourceAccess::Read,
        },
    };
    const auto stage = context.addTask(
        Name("tests/task/gpu/seed_release_stage"), stageUses, LengthOf(stageUses), &producer, 1u, {}, &released
    );
    ASSERT_TRUE(stage.valid());
    const Core::GpuTaskResourceUse halves[] = {
        BufferUse(
            context.m_resources[0u],
            { 0u, s_HalfBytes },
            Core::ResourceStates::CopySource,
            Core::GpuTaskResourceAccess::Read
        ),
        BufferUse(
            context.m_resources[0u],
            { s_HalfBytes, s_HalfBytes },
            Core::ResourceStates::CopySource,
            Core::GpuTaskResourceAccess::Read
        ),
    };
    Core::GpuTaskSchedulingHint mergedScheduling;
    mergedScheduling.mergeWithPrevious = true;
    const auto consumer = context.addTask(
        Name("tests/task/gpu/seed_release_consumer"), halves, LengthOf(halves), &stage, 1u, mergedScheduling
    );
    ASSERT_TRUE(consumer.valid());
    ASSERT_TRUE(context.compile(true)) << static_cast<u32>(context.m_analysis.diagnostic().status);
    {
        const Core::GpuCompiledGraph::ReadView plan(context.m_compiled);
        ASSERT_EQ(plan.packetForTask(stage), plan.packetForTask(consumer));
        ASSERT_NE(plan.packetForTask(producer), plan.packetForTask(consumer));
        ASSERT_EQ(plan.findTask(stage).plan->prologueStateSeedCount, 0u);
        const auto task = plan.findTask(consumer);
        ASSERT_EQ(task.plan->prologueStateSeedCount, s_TwoRanges);
        for(usize index = 0u; index < s_TwoRanges; ++index){
            ASSERT_EQ(task.prologueStateSeeds[index].sourcePacket, plan.packetForTask(producer));
            ASSERT_EQ(task.prologueStateSeeds[index].range.bufferRange, Core::BufferRange(index * s_HalfBytes, s_HalfBytes));
        }
    }
    Handoff first(context.m_testArena.arena);
    Handoff second(context.m_testArena.arena);
    Handoff naive(context.m_testArena.arena);
    ASSERT_TRUE(first.buildBufferRangeSubset(source, context.m_buffers[0u].get(), { 0u, s_HalfBytes }));
    ASSERT_TRUE(second.buildBufferRangeSubset(source, context.m_buffers[0u].get(), { s_HalfBytes, s_HalfBytes }));
    const Handoff* const prefix[] = { &first };
    EXPECT_FALSE(naive.buildFanIn(released, prefix, LengthOf(prefix), context.m_referenceScratch));
    const Handoff* const complete[] = { &first, &second };
    ASSERT_TRUE(naive.buildFanIn(released, complete, LengthOf(complete), context.m_referenceScratch));
    Handoff result(context.m_testArena.arena);
    ASSERT_TRUE(result.copyFrom(naive));
    const SeedAccess::Source sources[] = { { producer, source } };
    EXPECT_FALSE(SeedAccess::build(context.m_recorded, context.m_graph, context.m_compiled, consumer, sources, result));
    EXPECT_FALSE(result.valid());
    EXPECT_TRUE(result.empty());
    EXPECT_TRUE(source.equivalentTo(sourceBefore));
    EXPECT_TRUE(released.equivalentTo(releasedBefore));
    const Core::GpuTaskGraph::DeclarationReadView declarations(context.m_graph);
    const auto stageDeclaration = declarations.taskAt(stage.index);
    ASSERT_EQ(stageDeclaration.externalStateSourceCount, 1u);
    ASSERT_NE(stageDeclaration.externalStateSources[0u].states, nullptr);
    EXPECT_TRUE(stageDeclaration.externalStateSources[0u].states->equivalentTo(releasedBefore));
}

TEST(GpuPacketInitialStateSeed, ReversedRangesAcrossBatchBoundaryMatchSequentialFold){
    SeedContext context;
    ASSERT_TRUE(context.initialize(1u, s_NineRanges * s_HalfBytes));
    Core::GpuTaskId producer;
    Core::GpuTaskId consumer;
    ASSERT_NO_FATAL_FAILURE(DeclareRangeConsumer(context, producer, consumer, s_NineRanges));
    ASSERT_TRUE(context.compile());
    Handoff source(context.m_testArena.arena);
    for(usize index = 0u; index < s_NineRanges; ++index){
        context.addState(
            source, 0u, { index * s_HalfBytes, s_HalfBytes },
            index % s_TwoRanges == 0u ? Core::ResourceStates::CopyDest : Core::ResourceStates::CopySource
        );
    }
    {
        const Core::GpuCompiledGraph::ReadView plan(context.m_compiled);
        const auto task = plan.findTask(consumer);
        ASSERT_TRUE(task.valid());
        ASSERT_EQ(task.plan->prologueStateSeedCount, s_NineRanges);
        for(usize index = 0u; index < s_NineRanges; ++index){
            ASSERT_EQ(task.prologueStateSeeds[index].sourcePacket, plan.packetForTask(producer));
            const Core::BufferRange expectedRange((s_NineRanges - index - 1u) * s_HalfBytes, s_HalfBytes);
            ASSERT_EQ(task.prologueStateSeeds[index].range.bufferRange, expectedRange);
        }
    }
    const SeedAccess::Source sources[] = { { producer, source } };
    Handoff expected(context.m_testArena.arena);
    Handoff result(context.m_testArena.arena);
    Handoff sourceBefore(context.m_testArena.arena);
    ASSERT_TRUE(sourceBefore.copyFrom(source));
    ASSERT_TRUE(SequentialFold(context, consumer, sources, expected));
    ASSERT_TRUE(SeedAccess::build(context.m_recorded, context.m_graph, context.m_compiled, consumer, sources, result));
    EXPECT_TRUE(result.equivalentTo(expected));
    EXPECT_TRUE(source.equivalentTo(sourceBefore));
}

TEST(GpuPacketInitialStateSeed, MissingRangeReturnsNoPartialSeedAndRetryReusesTheScratchSafely){
    SeedContext context;
    ASSERT_TRUE(context.initialize(1u, s_NineRanges * s_HalfBytes));
    Core::GpuTaskId producer;
    Core::GpuTaskId consumer;
    ASSERT_NO_FATAL_FAILURE(DeclareRangeConsumer(context, producer, consumer, s_NineRanges));
    ASSERT_TRUE(context.compile());
    Handoff source(context.m_testArena.arena);
    {
        const Core::GpuCompiledGraph::ReadView plan(context.m_compiled);
        const auto task = plan.findTask(consumer);
        ASSERT_TRUE(task.valid());
        ASSERT_EQ(task.plan->prologueStateSeedCount, s_NineRanges);
        for(usize index = 0u; index < s_NineRanges; ++index){
            ASSERT_EQ(task.prologueStateSeeds[index].sourcePacket, plan.packetForTask(producer));
            const Core::BufferRange expectedRange((s_NineRanges - index - 1u) * s_HalfBytes, s_HalfBytes);
            ASSERT_EQ(task.prologueStateSeeds[index].range.bufferRange, expectedRange);
        }
    }
    // Reverse declaration leaves the missing first byte range as the ninth seed, after one complete batch.
    context.addState(source, 0u, { s_HalfBytes, (s_NineRanges - 1u) * s_HalfBytes }, Core::ResourceStates::CopyDest);
    Handoff sourceBefore(context.m_testArena.arena);
    ASSERT_TRUE(sourceBefore.copyFrom(source));
    const SeedAccess::Source sources[] = { { producer, source } };
    Handoff result(context.m_testArena.arena);
    ASSERT_TRUE(result.copyFrom(source));
    EXPECT_FALSE(SeedAccess::build(context.m_recorded, context.m_graph, context.m_compiled, consumer, sources, result));
    EXPECT_FALSE(result.valid());
    EXPECT_TRUE(result.empty());
    EXPECT_TRUE(source.equivalentTo(sourceBefore));
    context.addState(source, 0u, { 0u, s_HalfBytes }, Core::ResourceStates::CopySource);
    Handoff expected(context.m_testArena.arena);
    ASSERT_TRUE(SequentialFold(context, consumer, sources, expected));
    ASSERT_TRUE(SeedAccess::build(context.m_recorded, context.m_graph, context.m_compiled, consumer, sources, result));
    EXPECT_TRUE(result.equivalentTo(expected));
}

TEST(GpuPacketInitialStateSeed, SameSourceRunsDoNotCrossInterleavedProducerBoundaries){
    SeedContext context;
    ASSERT_TRUE(context.initialize(s_TwoRanges));
    const Core::GpuTaskResourceUse firstWrites[] = {
        BufferUse(
            context.m_resources[0u],
            { 0u, s_BufferBytes },
            Core::ResourceStates::CopyDest,
            Core::GpuTaskResourceAccess::Write
        ),
    };
    const Core::GpuTaskResourceUse secondWrites[] = {
        BufferUse(
            context.m_resources[1u],
            { 0u, s_BufferBytes },
            Core::ResourceStates::CopyDest,
            Core::GpuTaskResourceAccess::Write
        ),
    };
    const auto first = context.addTask(Name("tests/task/gpu/seed_switch_first"), firstWrites, LengthOf(firstWrites));
    const auto second = context.addTask(Name("tests/task/gpu/seed_switch_second"), secondWrites, LengthOf(secondWrites));
    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());
    constexpr usize s_BufferOrder[s_SwitchRanges] = { 0u, 0u, 1u, 1u, 0u, 0u };
    constexpr u64 s_QuarterBytes = s_BufferBytes / 4u;
    constexpr u64 s_Offsets[s_SwitchRanges] = {
        0u, s_QuarterBytes,
        0u, s_QuarterBytes,
        s_HalfBytes, s_HalfBytes + s_QuarterBytes,
    };
    Core::GpuTaskResourceUse uses[s_SwitchRanges] = {};
    for(usize index = 0u; index < s_SwitchRanges; ++index){
        uses[index] = BufferUse(
            context.m_resources[s_BufferOrder[index]], { s_Offsets[index], s_QuarterBytes },
            Core::ResourceStates::CopySource, Core::GpuTaskResourceAccess::Read
        );
    }
    const Core::GpuTaskId producers[] = { first, second };
    const auto consumer = context.addTask(
        Name("tests/task/gpu/seed_switch_consumer"), uses, LengthOf(uses), producers, LengthOf(producers)
    );
    ASSERT_TRUE(consumer.valid());
    ASSERT_TRUE(context.compile());
    {
        const Core::GpuCompiledGraph::ReadView plan(context.m_compiled);
        const auto task = plan.findTask(consumer);
        ASSERT_EQ(task.plan->prologueStateSeedCount, s_SwitchRanges);
        for(usize index = 0u; index < s_SwitchRanges; ++index){
            const auto expectedSource = plan.packetForTask(s_BufferOrder[index] == 0u ? first : second);
            ASSERT_EQ(task.prologueStateSeeds[index].sourcePacket, expectedSource);
        }
    }
    Handoff firstSource(context.m_testArena.arena);
    Handoff secondSource(context.m_testArena.arena);
    context.addState(firstSource, 0u, { 0u, s_BufferBytes }, Core::ResourceStates::CopyDest);
    context.addState(firstSource, 1u, { 0u, s_BufferBytes }, Core::ResourceStates::Common);
    context.addState(secondSource, 0u, { 0u, s_BufferBytes }, Core::ResourceStates::Common);
    context.addState(secondSource, 1u, { 0u, s_BufferBytes }, Core::ResourceStates::CopySource);
    const SeedAccess::Source sources[] = { { first, firstSource }, { second, secondSource } };
    Handoff firstBefore(context.m_testArena.arena);
    Handoff secondBefore(context.m_testArena.arena);
    ASSERT_TRUE(firstBefore.copyFrom(firstSource));
    ASSERT_TRUE(secondBefore.copyFrom(secondSource));
    Handoff expected(context.m_testArena.arena);
    Handoff result(context.m_testArena.arena);
    ASSERT_TRUE(SequentialFold(context, consumer, sources, expected));
    ASSERT_TRUE(SeedAccess::build(context.m_recorded, context.m_graph, context.m_compiled, consumer, sources, result));
    EXPECT_TRUE(result.equivalentTo(expected));
    EXPECT_TRUE(firstSource.equivalentTo(firstBefore));
    EXPECT_TRUE(secondSource.equivalentTo(secondBefore));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

