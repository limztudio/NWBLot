// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/material/task_graph_opaque_compute_emulation_plan.h>
#include <core/graphics/vulkan/backend.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>

#include <tests/common/graphics_metadata_test_objects.h>
#include <tests/common/test_context.h>

#include <global/text_utils.h>
#include <global/timer.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_compute_emulation_output_resource_set_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using TestArena = Tests::TestArena<struct ComputeEmulationOutputResourceSetTestsTag>;
using BufferVector = Vector<Core::BufferHandle, Core::Alloc::GlobalArena>;
inline constexpr Name s_SetIdentity("tests/compute_output_set/resources");
inline constexpr AStringView s_SetLabel = "Compute Emulation Outputs";

struct OutputContext{
    TestArena testArena;
    Core::GraphicsAllocator graphicsAllocator{ testArena.arena };
    Core::CpuTaskScheduler cpuScheduler{ 0u };
    Core::GraphicsBackend::VulkanContext context{ graphicsAllocator, cpuScheduler, 1u };
    Core::GraphicsBackend::VulkanAllocator allocator{ context };
    Core::GpuTaskGraph graph{ testArena.arena };
    BufferVector buffers{ testArena.arena };
    ECSRenderDetail::OpaqueRegularComputeEmulationGraphPlan plan{ testArena.arena };

    [[nodiscard]] Core::BufferHandle makeBuffer(const Name& identity){
        Core::Buffer* const buffer = Tests::NewMetadataOnlyBuffer(
            testArena.arena, context, allocator, Core::BufferDesc{}.setByteSize(256u).setDebugName(identity)
        );
        return Core::BufferHandle(buffer, Core::BufferHandle::deleter_type(&testArena.arena), AdoptRef);
    }

    [[nodiscard]] Core::GpuGraphResourceId importExisting(const Core::BufferHandle& buffer, const Name& identity){
        return graph.importBuffer(buffer, RendererTaskGraphDetail::BufferResourceDesc(identity, "Existing Output Buffer"));
    }
};


[[nodiscard]] static Name IndexedName(const Name& prefix, const usize index){
    char indexText[32u] = {};
    return DeriveName(prefix, FormatDecimal(index, indexText));
}

static void ExpectResourceSet(
    const OutputContext& context,
    const Core::GpuGraphResourceSetId resourceSet,
    const BufferVector& expected){
    const Core::GpuTaskGraph::DeclarationReadView view(context.graph);
    ASSERT_TRUE(view.valid());
    ASSERT_TRUE(view.validResourceSet(resourceSet));
    const auto set = view.resourceSetAt(resourceSet.index);
    ASSERT_EQ(set.memberCount, expected.size());
    for(usize index = 0u; index < expected.size(); ++index)
        EXPECT_EQ(view.bufferForResource(set.members[index]), expected[index].get());
}


TEST(ComputeEmulationOutputResourceSet, EmptyAndUncapturedPlansClearTheOutputWithoutImporting){
    OutputContext context;
    Core::Alloc::ScratchArena scratch(Name("tests/compute_output_set/empty"));
    Core::GpuGraphResourceSetId result{ 17u, 1u };
    EXPECT_FALSE(RendererTaskGraphDetail::GatherImportedOutputBufferResourceSet(
        context.graph, context.plan, scratch, s_SetIdentity, s_SetLabel, result
    ));
    EXPECT_FALSE(result.valid());
    context.plan.captured = true;
    EXPECT_FALSE(RendererTaskGraphDetail::GatherImportedOutputBufferResourceSet(
        context.graph, context.plan, scratch, s_SetIdentity, s_SetLabel, result
    ));
    context.plan.outputBuffers.push_back(context.makeBuffer(Name("tests/compute_output_set/uncaptured")));
    context.plan.captured = false;
    EXPECT_FALSE(RendererTaskGraphDetail::GatherImportedOutputBufferResourceSet(
        context.graph, context.plan, scratch, s_SetIdentity, s_SetLabel, result
    ));
    const Core::GpuTaskGraph::DeclarationReadView view(context.graph);
    ASSERT_TRUE(view.valid());
    EXPECT_EQ(view.resourceCount(), 0u);
    EXPECT_EQ(view.resourceSetCount(), 0u);
}

TEST(ComputeEmulationOutputResourceSet, PreservesOrderAndReusesAnExistingUnnamedAliasWithoutMetadataValidation){
    OutputContext context;
    const auto first = context.makeBuffer(Name("tests/compute_output_set/first"));
    const auto unnamed = context.makeBuffer(NAME_NONE);
    const auto last = context.makeBuffer(Name("tests/compute_output_set/last"));
    const auto existing = context.importExisting(unnamed, Name("tests/compute_output_set/existing_alias"));
    ASSERT_TRUE(existing.valid());
    Core::BufferDesc& description = const_cast<Core::BufferDesc&>(unnamed->getDescription());
    ++description.byteSize;
    ASSERT_FALSE(unnamed->descriptionMatchesCreation());
    context.plan.outputBuffers = { first, unnamed, last };
    context.plan.captured = true;
    Core::Alloc::ScratchArena scratch(Name("tests/compute_output_set/order"));
    Core::GpuGraphResourceSetId result;
    ASSERT_TRUE(RendererTaskGraphDetail::GatherImportedOutputBufferResourceSet(
        context.graph, context.plan, scratch, s_SetIdentity, s_SetLabel, result
    ));
    ASSERT_NO_FATAL_FAILURE(ExpectResourceSet(context, result, context.plan.outputBuffers));
    const Core::GpuTaskGraph::DeclarationReadView view(context.graph);
    ASSERT_TRUE(view.valid());
    ASSERT_EQ(view.resourceCount(), 3u);
    EXPECT_EQ(view.resourceSetAt(result.index).members[1u], existing);
    EXPECT_EQ(view.resourceAt(existing.index).identity, Name("tests/compute_output_set/existing_alias"));
    EXPECT_EQ(view.resourceAt(existing.index).markerLabel, "Existing Output Buffer");
    EXPECT_EQ(view.resourceAt(1u).identity, first->getCreationDescription().debugName);
    EXPECT_EQ(view.resourceAt(2u).identity, last->getCreationDescription().debugName);
    description = unnamed->getCreationDescription();
}

TEST(ComputeEmulationOutputResourceSet, DuplicateMembersRejectTheSetAfterEveryBufferHasBeenImported){
    OutputContext context;
    const auto first = context.makeBuffer(Name("tests/compute_output_set/duplicate_first"));
    const auto second = context.makeBuffer(Name("tests/compute_output_set/duplicate_second"));
    const auto later = context.makeBuffer(Name("tests/compute_output_set/duplicate_later"));
    context.plan.outputBuffers = { first, second, first, later };
    context.plan.captured = true;
    Core::Alloc::ScratchArena scratch(Name("tests/compute_output_set/duplicates"));
    Core::GpuGraphResourceSetId result{ 17u, 1u };
    EXPECT_FALSE(RendererTaskGraphDetail::GatherImportedOutputBufferResourceSet(
        context.graph, context.plan, scratch, s_SetIdentity, s_SetLabel, result
    ));
    EXPECT_FALSE(result.valid());
    const Core::GpuTaskGraph::DeclarationReadView view(context.graph);
    ASSERT_TRUE(view.valid());
    EXPECT_EQ(view.resourceCount(), 3u);
    EXPECT_EQ(view.resourceSetCount(), 0u);
    EXPECT_TRUE(view.findImportedBuffer(first).valid());
    EXPECT_TRUE(view.findImportedBuffer(second).valid());
    EXPECT_TRUE(view.findImportedBuffer(later).valid());
}

TEST(ComputeEmulationOutputResourceSet, LateInvalidInputsKeepOnlyTheImportedPrefixAndPublishNoSet){
    constexpr usize s_Count = 40u;
    constexpr usize s_FailureIndex = 35u;
    for(u32 failureKind = 0u; failureKind < 4u; ++failureKind){
        OutputContext context;
        context.plan.outputBuffers.reserve(s_Count);
        for(usize index = 0u; index < s_Count; ++index){
            context.plan.outputBuffers.push_back(context.makeBuffer(
                IndexedName(Name("tests/compute_output_set/late_failure"), index)
            ));
        }
        Core::BufferHandle conflict;
        if(failureKind == 0u)
            context.plan.outputBuffers[s_FailureIndex] = nullptr;
        else if(failureKind == 1u)
            context.plan.outputBuffers[s_FailureIndex] = context.makeBuffer(NAME_NONE);
        else if(failureKind == 2u){
            const Name identity = context.plan.outputBuffers[s_FailureIndex]->getCreationDescription().debugName;
            conflict = context.makeBuffer(identity);
            ASSERT_TRUE(context.importExisting(conflict, identity).valid());
        }
        else{
            Core::BufferDesc& description = const_cast<Core::BufferDesc&>(
                context.plan.outputBuffers[s_FailureIndex]->getDescription()
            );
            ++description.byteSize;
            ASSERT_FALSE(context.plan.outputBuffers[s_FailureIndex]->descriptionMatchesCreation());
        }
        context.plan.captured = true;
        Core::Alloc::ScratchArena scratch(Name("tests/compute_output_set/late_failure_scratch"));
        Core::GpuGraphResourceSetId result{ 17u, 1u };
        EXPECT_FALSE(RendererTaskGraphDetail::GatherImportedOutputBufferResourceSet(
            context.graph, context.plan, scratch, s_SetIdentity, s_SetLabel, result
        ));
        EXPECT_FALSE(result.valid());
        const Core::GpuTaskGraph::DeclarationReadView view(context.graph);
        ASSERT_TRUE(view.valid());
        EXPECT_EQ(view.resourceCount(), s_FailureIndex + (conflict ? 1u : 0u));
        EXPECT_EQ(view.resourceSetCount(), 0u);
        for(usize index = 0u; index < s_Count; ++index){
            const auto resource = view.findImportedBuffer(context.plan.outputBuffers[index]);
            EXPECT_EQ(resource.valid(), index < s_FailureIndex);
        }
        if(failureKind == 3u){
            const auto& buffer = context.plan.outputBuffers[s_FailureIndex];
            Core::BufferDesc& description = const_cast<Core::BufferDesc&>(buffer->getDescription());
            description = buffer->getCreationDescription();
        }
    }
}

TEST(ComputeEmulationOutputResourceSet, LargeMixedImportsPreserveAliasesOrderAndOwningReferences){
    OutputContext context;
    constexpr usize s_Count = 40u;
    context.buffers.reserve(s_Count);
    context.plan.outputBuffers.reserve(s_Count);
    Array<Core::GpuGraphResourceId, s_Count> existing;
    for(usize index = 0u; index < s_Count; ++index){
        context.buffers.push_back(context.makeBuffer(IndexedName(Name("tests/compute_output_set/large"), index)));
    }
    for(usize remaining = s_Count; remaining != 0u; --remaining){
        const usize index = remaining - 1u;
        if(index % 2u == 0u){
            existing[index] = context.importExisting(
                context.buffers[index], IndexedName(Name("tests/compute_output_set/large_alias"), index)
            );
            ASSERT_TRUE(existing[index].valid());
        }
    }
    for(usize index = 0u; index < s_Count; ++index)
        context.plan.outputBuffers.push_back(context.buffers[(index * 17u) % s_Count]);
    Array<u32, s_Count> referencesBefore;
    for(usize index = 0u; index < s_Count; ++index)
        referencesBefore[index] = context.buffers[index]->getReferenceCount();
    context.plan.captured = true;
    Core::Alloc::ScratchArena scratch(Name("tests/compute_output_set/large_scratch"));
    Core::GpuGraphResourceSetId result;
    ASSERT_TRUE(RendererTaskGraphDetail::GatherImportedOutputBufferResourceSet(
        context.graph, context.plan, scratch, s_SetIdentity, s_SetLabel, result
    ));
    ASSERT_NO_FATAL_FAILURE(ExpectResourceSet(context, result, context.plan.outputBuffers));
    {
        const Core::GpuTaskGraph::DeclarationReadView view(context.graph);
        ASSERT_TRUE(view.valid());
        EXPECT_EQ(view.resourceCount(), s_Count);
        const auto set = view.resourceSetAt(result.index);
        for(usize index = 0u; index < s_Count; ++index){
            const usize bufferIndex = (index * 17u) % s_Count;
            if(existing[bufferIndex].valid())
                EXPECT_EQ(set.members[index], existing[bufferIndex]);
            EXPECT_EQ(
                context.buffers[bufferIndex]->getReferenceCount(),
                referencesBefore[bufferIndex] + (existing[bufferIndex].valid() ? 0u : 1u)
            );
        }
    }
    context.plan.reset();
    for(const auto& buffer : context.buffers)
        EXPECT_EQ(buffer->getReferenceCount(), 2u);
    context.graph.reset();
    for(const auto& buffer : context.buffers)
        EXPECT_EQ(buffer->getReferenceCount(), 1u);
}

TEST(ComputeEmulationOutputResourceSet, NewCallsObserveCurrentHandlesGenerationAndImportValidation){
    OutputContext context;
    const Name identity("tests/compute_output_set/replacement");
    context.plan.outputBuffers.push_back(context.makeBuffer(identity));
    context.plan.captured = true;
    Core::Alloc::ScratchArena scratch(Name("tests/compute_output_set/replacement_scratch"));
    Core::GpuGraphResourceSetId result;
    ASSERT_TRUE(RendererTaskGraphDetail::GatherImportedOutputBufferResourceSet(
        context.graph, context.plan, scratch, s_SetIdentity, s_SetLabel, result
    ));
    const auto previousSet = result;
    context.graph.reset();
    context.plan.outputBuffers[0u] = context.makeBuffer(identity);
    Core::BufferDesc& description = const_cast<Core::BufferDesc&>(context.plan.outputBuffers[0u]->getDescription());
    ++description.byteSize;
    EXPECT_FALSE(RendererTaskGraphDetail::GatherImportedOutputBufferResourceSet(
        context.graph, context.plan, scratch, s_SetIdentity, s_SetLabel, result
    ));
    EXPECT_FALSE(result.valid());
    description = context.plan.outputBuffers[0u]->getCreationDescription();
    ASSERT_TRUE(RendererTaskGraphDetail::GatherImportedOutputBufferResourceSet(
        context.graph, context.plan, scratch, s_SetIdentity, s_SetLabel, result
    ));
    EXPECT_NE(result.generation, previousSet.generation);
    ASSERT_NO_FATAL_FAILURE(ExpectResourceSet(context, result, context.plan.outputBuffers));
    const Core::GpuTaskGraph::DeclarationReadView view(context.graph);
    ASSERT_TRUE(view.valid());
    EXPECT_FALSE(view.validResourceSet(previousSet));
    EXPECT_EQ(view.resourceCount(), 1u);
    EXPECT_EQ(view.resourceSetCount(), 1u);
}

TEST(ComputeEmulationOutputResourceSet, ConflictingSetIdentityStillImportsNewBuffersBeforeRejectingTheSet){
    OutputContext context;
    context.plan.outputBuffers.push_back(context.makeBuffer(Name("tests/compute_output_set/set_first")));
    context.plan.captured = true;
    Core::Alloc::ScratchArena scratch(Name("tests/compute_output_set/set_conflict"));
    Core::GpuGraphResourceSetId result;
    ASSERT_TRUE(RendererTaskGraphDetail::GatherImportedOutputBufferResourceSet(
        context.graph, context.plan, scratch, s_SetIdentity, s_SetLabel, result
    ));
    const auto originalSet = result;
    const auto replacement = context.makeBuffer(Name("tests/compute_output_set/set_replacement"));
    context.plan.outputBuffers[0u] = replacement;
    EXPECT_FALSE(RendererTaskGraphDetail::GatherImportedOutputBufferResourceSet(
        context.graph, context.plan, scratch, s_SetIdentity, s_SetLabel, result
    ));
    EXPECT_FALSE(result.valid());
    const Core::GpuTaskGraph::DeclarationReadView view(context.graph);
    ASSERT_TRUE(view.valid());
    EXPECT_TRUE(view.validResourceSet(originalSet));
    EXPECT_TRUE(view.findImportedBuffer(replacement).valid());
    EXPECT_EQ(view.resourceCount(), 2u);
    EXPECT_EQ(view.resourceSetCount(), 1u);
}

TEST(ComputeEmulationOutputResourceSet, LargeCallsUseCurrentGraphGenerationAndDeclarationOrderAfterReset){
    OutputContext context;
    constexpr usize s_Count = 40u;
    context.plan.outputBuffers.reserve(s_Count);
    for(usize index = 0u; index < s_Count; ++index){
        const Name identity = IndexedName(Name("tests/compute_output_set/promoted_reset"), index);
        context.plan.outputBuffers.push_back(context.makeBuffer(identity));
        ASSERT_TRUE(context.importExisting(context.plan.outputBuffers.back(), identity).valid());
    }
    context.plan.captured = true;
    Core::Alloc::ScratchArena scratch(Name("tests/compute_output_set/promoted_reset_scratch"));
    Core::GpuGraphResourceSetId result;
    ASSERT_TRUE(RendererTaskGraphDetail::GatherImportedOutputBufferResourceSet(
        context.graph, context.plan, scratch, s_SetIdentity, s_SetLabel, result
    ));
    const auto previousSet = result;
    context.graph.reset();
    for(usize remaining = s_Count; remaining != 0u; --remaining){
        const auto& buffer = context.plan.outputBuffers[remaining - 1u];
        ASSERT_TRUE(context.importExisting(buffer, buffer->getCreationDescription().debugName).valid());
    }
    ASSERT_TRUE(RendererTaskGraphDetail::GatherImportedOutputBufferResourceSet(
        context.graph, context.plan, scratch, s_SetIdentity, s_SetLabel, result
    ));
    EXPECT_NE(result.generation, previousSet.generation);
    ASSERT_NO_FATAL_FAILURE(ExpectResourceSet(context, result, context.plan.outputBuffers));
    const Core::GpuTaskGraph::DeclarationReadView view(context.graph);
    ASSERT_TRUE(view.valid());
    EXPECT_FALSE(view.validResourceSet(previousSet));
    EXPECT_EQ(view.resourceSetAt(result.index).members[0u].index, s_Count - 1u);
}

TEST(ComputeEmulationOutputResourceSet, RepeatedRequestsRetainOnlyGraphOwnedHandlesAndReleaseTemporaryMembers){
    struct Workload{
        usize uniqueCount;
        usize requestCount;
        usize unrelatedCount;
    };
    constexpr Workload s_Workloads[]{
        { 1u, 1u, 0u },
        { 1u, 4096u, 128u },
        { 32u, 32u, 128u },
        { 32u, 4096u, 128u },
        { 40u, 40u, 0u },
        { 40u, 4096u, 0u },
        { 40u, 40u, 512u },
    };
    u64 peaks[LengthOf(s_Workloads)] = {};
    for(usize workloadIndex = 0u; workloadIndex < LengthOf(s_Workloads); ++workloadIndex){
        const Workload& workload = s_Workloads[workloadIndex];
        OutputContext context;
        context.buffers.reserve(workload.uniqueCount + workload.unrelatedCount);
        for(usize index = 0u; index < workload.uniqueCount + workload.unrelatedCount; ++index){
            const Name identity = IndexedName(Name("tests/compute_output_set/lookup_storage"), index);
            context.buffers.push_back(context.makeBuffer(identity));
            ASSERT_TRUE(context.importExisting(context.buffers.back(), identity).valid());
        }
        context.plan.outputBuffers.reserve(workload.requestCount);
        for(usize index = 0u; index < workload.requestCount; ++index)
            context.plan.outputBuffers.push_back(context.buffers[workload.unrelatedCount + index % workload.uniqueCount]);
        context.plan.captured = true;
        Core::Alloc::ScratchArena scratch(Name("tests/compute_output_set/lookup_storage_scratch"));
        Core::GpuGraphResourceSetId result;
        const bool gathered = RendererTaskGraphDetail::GatherImportedOutputBufferResourceSet(
            context.graph, context.plan, scratch, s_SetIdentity, s_SetLabel, result
        );
        const bool uniqueMembers = workload.uniqueCount == workload.requestCount;
        EXPECT_EQ(gathered, uniqueMembers);
        EXPECT_EQ(result.valid(), uniqueMembers);
        if(uniqueMembers)
            ASSERT_NO_FATAL_FAILURE(ExpectResourceSet(context, result, context.plan.outputBuffers));
        context.plan.outputBuffers.clear();
        const Core::GpuTaskGraph::DeclarationReadView view(context.graph);
        ASSERT_TRUE(view.valid());
        EXPECT_EQ(view.resourceCount(), workload.uniqueCount + workload.unrelatedCount);
        EXPECT_EQ(view.resourceSetCount(), uniqueMembers ? 1u : 0u);
        for(const Core::BufferHandle& buffer : context.buffers){
            const auto resource = view.findImportedBuffer(buffer);
            ASSERT_TRUE(resource.valid());
            EXPECT_EQ(view.bufferForResource(resource), buffer.get());
            EXPECT_EQ(buffer->getReferenceCount(), 2u);
        }
        EXPECT_FALSE(view.findImportedBuffer({}).valid());
        const ArenaMemoryStats memory = scratch.memoryStats();
        peaks[workloadIndex] = memory.peakUsedBytes;
        EXPECT_EQ(memory.usedBytes, 0u);
    }
    EXPECT_EQ(peaks[1u], peaks[3u]);
    EXPECT_EQ(peaks[3u], peaks[5u]);
    EXPECT_EQ(peaks[4u], peaks[6u]);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void RecordUnsignedProperty(const NotNull<const char*> key, const u64 value){
    char text[32u] = {};
    const AStringView formatted = FormatDecimal(value, text);
    text[formatted.size()] = '\0';
    testing::Test::RecordProperty(key.get(), text);
}

static void BenchmarkOutputResourceSets(
    const usize uniqueCount,
    const usize requestCount,
    const usize unrelatedCount,
    const usize iterations,
    const bool preimportRequested = true){
    OutputContext context;
    context.buffers.reserve(uniqueCount + unrelatedCount);
    context.plan.outputBuffers.reserve(requestCount);
    for(usize index = 0u; index < uniqueCount + unrelatedCount; ++index){
        const Name identity = IndexedName(Name("tests/compute_output_set/benchmark"), index);
        context.buffers.push_back(context.makeBuffer(identity));
        if(index < unrelatedCount || preimportRequested)
            ASSERT_TRUE(context.importExisting(context.buffers.back(), identity).valid());
    }
    // The production helper consumes the captured flag and ordered owning outputs. Capture itself has independent
    // tests and benchmarks; this setup reserves and fills its real plan storage before either measured region.
    for(usize index = 0u; index < requestCount; ++index)
        context.plan.outputBuffers.push_back(context.buffers[unrelatedCount + index % uniqueCount]);
    context.plan.captured = true;
    const bool expectedSuccess = requestCount == uniqueCount;
    u64 firstNanoseconds = 0u;
    u64 repeatNanoseconds = 0u;
    u64 peak = 0u;
    u64 reserved = 0u;
    usize matchingOutcomes = 0u;
    Core::GpuGraphResourceSetId lastSet;
    for(usize iteration = 0u; iteration <= iterations; ++iteration){
        if(!preimportRequested && iteration != 0u){
            context.graph.reset();
            for(usize index = 0u; index < unrelatedCount; ++index){
                const auto& buffer = context.buffers[index];
                ASSERT_TRUE(context.importExisting(buffer, buffer->getCreationDescription().debugName).valid());
            }
        }
        Core::Alloc::ScratchArena scratch(Name("tests/compute_output_set/benchmark_scratch"));
        const Timer begin = TimerNow();
        const bool success = RendererTaskGraphDetail::GatherImportedOutputBufferResourceSet(
            context.graph, context.plan, scratch, s_SetIdentity, s_SetLabel, lastSet
        );
        const u64 elapsed = DurationInNS<u64>(TimerNow(), begin);
        if(iteration == 0u)
            firstNanoseconds = elapsed;
        else
            repeatNanoseconds += elapsed;
        matchingOutcomes += success == expectedSuccess && lastSet.valid() == expectedSuccess ? 1u : 0u;
        peak = Max(peak, scratch.memoryStats().peakUsedBytes);
        reserved = Max(reserved, scratch.memoryStats().reservedBytes);
    }
    EXPECT_EQ(matchingOutcomes, iterations + 1u);
    if(expectedSuccess)
        ASSERT_NO_FATAL_FAILURE(ExpectResourceSet(context, lastSet, context.plan.outputBuffers));
    const Core::GpuTaskGraph::DeclarationReadView view(context.graph);
    ASSERT_TRUE(view.valid());
    EXPECT_EQ(view.resourceCount(), unrelatedCount + uniqueCount);
    EXPECT_EQ(view.resourceSetCount(), expectedSuccess ? 1u : 0u);
    RecordUnsignedProperty(MakeNotNull("compute_output_set_first_ns"), firstNanoseconds);
    RecordUnsignedProperty(MakeNotNull("compute_output_set_repeat_ns"), repeatNanoseconds);
    RecordUnsignedProperty(MakeNotNull("compute_output_set_iterations"), iterations);
    RecordUnsignedProperty(MakeNotNull("compute_output_set_requests"), requestCount);
    RecordUnsignedProperty(MakeNotNull("compute_output_set_unique"), uniqueCount);
    RecordUnsignedProperty(MakeNotNull("compute_output_set_unrelated"), unrelatedCount);
    RecordUnsignedProperty(MakeNotNull("compute_output_set_initially_missing"), preimportRequested ? 0u : uniqueCount);
    RecordUnsignedProperty(MakeNotNull("compute_output_set_scratch_peak_bytes"), peak);
    RecordUnsignedProperty(MakeNotNull("compute_output_set_scratch_reserved_bytes"), reserved);
}

TEST(ComputeEmulationOutputResourceSetBenchmark, DISABLED_Unique1){
    BenchmarkOutputResourceSets(1u, 1u, 0u, 2048u);
}

TEST(ComputeEmulationOutputResourceSetBenchmark, DISABLED_Unique8){
    BenchmarkOutputResourceSets(8u, 8u, 0u, 256u);
}

TEST(ComputeEmulationOutputResourceSetBenchmark, DISABLED_Unique32){
    BenchmarkOutputResourceSets(32u, 32u, 0u, 64u);
}

TEST(ComputeEmulationOutputResourceSetBenchmark, DISABLED_Unique1024WithUnrelated1024){
    BenchmarkOutputResourceSets(1024u, 1024u, 1024u, 3u);
}

TEST(ComputeEmulationOutputResourceSetBenchmark, DISABLED_Duplicate1In4096){
    BenchmarkOutputResourceSets(1u, 4096u, 128u, 3u);
}

TEST(ComputeEmulationOutputResourceSetBenchmark, DISABLED_Missing256){
    BenchmarkOutputResourceSets(256u, 256u, 256u, 3u, false);
}

TEST(ComputeEmulationOutputResourceSetBenchmark, DISABLED_Sparse1In4096){
    BenchmarkOutputResourceSets(1u, 1u, 4096u, 32u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

