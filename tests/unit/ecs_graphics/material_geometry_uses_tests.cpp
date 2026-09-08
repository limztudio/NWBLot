// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/graphics_metadata_test_objects.h>
#include <tests/common/test_context.h>

#include <impl/ecs_render/material/task_graph_resource_sets.h>

#include <global/text_utils.h>
#include <global/timer.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_material_geometry_uses_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestArena = ::NWB::Tests::TestArena<struct MaterialGeometryUsesTestsTag>;
using BufferVector = Vector<Core::BufferHandle, Core::Alloc::ScratchArena>;
using ResourceUseVector = Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena>;
inline constexpr usize s_SourceBufferCount = 11u;
inline constexpr Name s_GeometryScratchArena("tests/material_geometry_uses/scratch");


struct GeometryContext{
    TestArena testArena;
    Core::GraphicsAllocator graphicsAllocator{ testArena.arena };
    Core::CpuTaskScheduler cpuScheduler{ 0u };
    Core::GraphicsBackend::VulkanContext context{ graphicsAllocator, cpuScheduler, 1u };
    Core::GraphicsBackend::VulkanAllocator allocator{ context };
    Core::GpuTaskGraph graph{ testArena.arena };
    Core::Alloc::ScratchArena scratchArena{ s_GeometryScratchArena };


    [[nodiscard]] Core::BufferHandle makeBuffer(const Name& identity){
        Core::Buffer* const buffer = NewMetadataOnlyBuffer(
            testArena.arena,
            context,
            allocator,
            Core::BufferDesc{}.setByteSize(256u).setDebugName(identity)
        );
        return Core::BufferHandle(buffer, Core::BufferHandle::deleter_type(&testArena.arena), AdoptRef);
    }
};


[[nodiscard]] static bool CreateBuffers(GeometryContext& context, const usize count, BufferVector& outBuffers){
    outBuffers.reserve(count);
    for(usize bufferIndex = 0u; bufferIndex < count; ++bufferIndex){
        char indexText[32u] = {};
        Core::BufferHandle buffer = context.makeBuffer(
            DeriveName(Name("tests/material_geometry_uses/buffer/"), FormatDecimal(bufferIndex, indexText))
        );
        if(!buffer)
            return false;
        outBuffers.push_back(Move(buffer));
    }
    return true;
}

[[nodiscard]] static Impl::MaterialPassDrawItem MakeDrawItem(const BufferVector& buffers, const usize firstBuffer){
    Impl::MaterialPassDrawItem drawItem;
    Impl::MaterialPassMeshResourceSnapshot& mesh = drawItem.meshResources;
    mesh.sourceBuffers = Impl::RuntimeMeshBuffers{
        .positionBuffer = buffers[firstBuffer],
        .normalBuffer = buffers[firstBuffer + 1u],
        .tangentBuffer = buffers[firstBuffer + 2u],
        .uv0Buffer = buffers[firstBuffer + 3u],
        .colorBuffer = buffers[firstBuffer + 4u],
        .meshletDescBuffer = buffers[firstBuffer + 5u],
        .meshletBoundsBuffer = buffers[firstBuffer + 6u],
        .meshletPositionRefDeltaBuffer = buffers[firstBuffer + 7u],
        .meshletAttributeRefDeltaBuffer = buffers[firstBuffer + 8u],
        .meshletLocalVertexRefBuffer = buffers[firstBuffer + 9u],
        .meshletPrimitiveIndexBuffer = buffers[firstBuffer + 10u],
    };
    for(u32 bindingSlot = 0u; bindingSlot < LengthOf(mesh.geometryHeapHandles); ++bindingSlot)
        mesh.geometryHeapHandles[bindingSlot] = Core::GpuDescriptorHandle::make(Core::GpuDescriptorClass::StorageBuffer, bindingSlot);
    mesh.meshletCount = 1u;
    mesh.meshletPrimitiveIndexCount = 3u;
    return drawItem;
}

static void ExpectUses(
    const Core::GpuTaskGraph& graph,
    const ResourceUseVector& uses,
    const BufferVector& expectedBuffers
){
    const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
    ASSERT_TRUE(declarations.valid());
    ASSERT_EQ(uses.size(), expectedBuffers.size());
    for(usize resourceIndex = 0u; resourceIndex < uses.size(); ++resourceIndex){
        const Core::GpuTaskResourceUse& use = uses[resourceIndex];
        EXPECT_TRUE(use.resource.valid());
        EXPECT_EQ(declarations.bufferForResource(use.resource), expectedBuffers[resourceIndex].get());
        EXPECT_EQ(use.access, Core::GpuTaskResourceAccess::Read);
        EXPECT_EQ(use.requiredState, Core::ResourceStates::ShaderResource);
        EXPECT_EQ(use.range.bufferRange.byteOffset, Core::s_EntireBuffer.byteOffset);
        EXPECT_EQ(use.range.bufferRange.byteSize, Core::s_EntireBuffer.byteSize);
        EXPECT_FALSE(use.hasIndependentStateSource);
    }
}

static void MeasureGatherWorkload(const usize meshCount, const usize repetitions, const usize warmGatherCount){
    GeometryContext context;
    BufferVector buffers{ context.scratchArena };
    ASSERT_TRUE(CreateBuffers(context, meshCount * s_SourceBufferCount, buffers));
    Impl::MaterialPassDrawItems drawItems(context.scratchArena);
    drawItems.meshDrawItems.reserve(meshCount * repetitions);
    for(usize repetition = 0u; repetition < repetitions; ++repetition){
        for(usize meshIndex = 0u; meshIndex < meshCount; ++meshIndex)
            drawItems.meshDrawItems.push_back(MakeDrawItem(buffers, meshIndex * s_SourceBufferCount));
    }
    const Impl::MaterialPassDrawItems* const drawItemSets[] = { &drawItems };
    Core::Alloc::ScratchArena gatherScratchArena(Name("tests/material_geometry_uses/gather_scratch"));
    ResourceUseVector uses{ gatherScratchArena };

    // Measure the complete production helper, including first-import work. Buffer creation and draw preparation
    // are outside the measured region; the second region reuses those imports as later frame-graph passes do.
    const Timer coldBegin = TimerNow();
    const bool coldGathered = Impl::RendererTaskGraphDetail::GatherPreparedMaterialGeometryUses(
        context.graph, drawItemSets, LengthOf(drawItemSets), gatherScratchArena, uses
    );
    const u64 coldNanoseconds = DurationInNS<u64>(TimerNow(), coldBegin);
    ASSERT_TRUE(coldGathered);
    ASSERT_NO_FATAL_FAILURE(ExpectUses(context.graph, uses, buffers));

    bool warmGathered = true;
    const Timer warmBegin = TimerNow();
    for(usize gatherIndex = 0u; gatherIndex < warmGatherCount; ++gatherIndex){
        if(!Impl::RendererTaskGraphDetail::GatherPreparedMaterialGeometryUses(
            context.graph, drawItemSets, LengthOf(drawItemSets), gatherScratchArena, uses
        )){
            warmGathered = false;
            break;
        }
    }
    const u64 warmNanoseconds = DurationInNS<u64>(TimerNow(), warmBegin);
    ASSERT_TRUE(warmGathered);
    ASSERT_NO_FATAL_FAILURE(ExpectUses(context.graph, uses, buffers));

    const ArenaMemoryStats gatherMemory = gatherScratchArena.memoryStats();
    char reservedText[32u] = {};
    char peakText[32u] = {};
    testing::Test::RecordProperty("geometry_scratch_reserved_bytes", FormatDecimal(gatherMemory.reservedBytes, reservedText).data());
    testing::Test::RecordProperty("geometry_scratch_peak_bytes", FormatDecimal(gatherMemory.peakUsedBytes, peakText).data());
    char coldDurationText[32u] = {};
    char warmDurationText[32u] = {};
    char gatherCountText[32u] = {};
    char bufferCountText[32u] = {};
    char drawCountText[32u] = {};
    testing::Test::RecordProperty("geometry_cold_gather_ns", FormatDecimal(coldNanoseconds, coldDurationText).data());
    testing::Test::RecordProperty("geometry_warm_gather_total_ns", FormatDecimal(warmNanoseconds, warmDurationText).data());
    testing::Test::RecordProperty("geometry_warm_gather_count", FormatDecimal(warmGatherCount, gatherCountText).data());
    testing::Test::RecordProperty("geometry_unique_buffer_count", FormatDecimal(buffers.size(), bufferCountText).data());
    testing::Test::RecordProperty("geometry_draw_count", FormatDecimal(drawItems.meshDrawItems.size(), drawCountText).data());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(MaterialGeometryUses, PreservesFirstOccurrenceAcrossDrawKindsSetsAndExistingImports){
    GeometryContext context;
    BufferVector buffers{ context.scratchArena };
    ASSERT_TRUE(CreateBuffers(context, 3u * s_SourceBufferCount, buffers));
    Impl::MaterialPassDrawItems firstSet(context.scratchArena);
    Impl::MaterialPassDrawItems secondSet(context.scratchArena);
    firstSet.meshDrawItems.reserve(1u);
    firstSet.computeDrawItems.reserve(1u);
    secondSet.meshDrawItems.reserve(1u);
    secondSet.computeDrawItems.reserve(1u);
    firstSet.meshDrawItems.push_back(MakeDrawItem(buffers, 11u));
    firstSet.meshDrawItems.back().meshResources.sourceBuffers.normalBuffer = buffers[11u];
    firstSet.computeDrawItems.push_back(MakeDrawItem(buffers, 0u));
    secondSet.meshDrawItems.push_back(MakeDrawItem(buffers, 22u));
    secondSet.computeDrawItems.push_back(MakeDrawItem(buffers, 11u));
    const Impl::MaterialPassDrawItems* const drawItemSets[] = { &firstSet, &secondSet, &firstSet };
    BufferVector expected{ context.scratchArena };
    expected.reserve(buffers.size());
    expected.push_back(buffers[11u]);
    for(usize bufferIndex = 13u; bufferIndex < 22u; ++bufferIndex)
        expected.push_back(buffers[bufferIndex]);
    for(usize bufferIndex = 0u; bufferIndex < 11u; ++bufferIndex)
        expected.push_back(buffers[bufferIndex]);
    for(usize bufferIndex = 22u; bufferIndex < 33u; ++bufferIndex)
        expected.push_back(buffers[bufferIndex]);
    expected.push_back(buffers[12u]);

    const Core::GpuGraphResourceId lastPreimport = context.graph.importBuffer(
        buffers[32u], Impl::RendererTaskGraphDetail::BufferResourceDesc(Name("tests/material_geometry_uses/last"), "Last")
    );
    const Core::GpuGraphResourceId middlePreimport = context.graph.importBuffer(
        buffers[7u], Impl::RendererTaskGraphDetail::BufferResourceDesc(Name("tests/material_geometry_uses/middle"), "Middle")
    );
    ASSERT_TRUE(lastPreimport.valid());
    ASSERT_TRUE(middlePreimport.valid());
    ResourceUseVector uses{ context.scratchArena };
    ASSERT_TRUE(Impl::RendererTaskGraphDetail::GatherPreparedMaterialGeometryUses(
        context.graph, drawItemSets, LengthOf(drawItemSets), context.scratchArena, uses
    ));
    ASSERT_NO_FATAL_FAILURE(ExpectUses(context.graph, uses, expected));
    EXPECT_EQ(uses[17u].resource, middlePreimport);
    EXPECT_EQ(uses[31u].resource, lastPreimport);

    Core::GpuGraphResourceSetId resourceSet;
    ASSERT_TRUE(Impl::RendererTaskGraphDetail::GatherPreparedMaterialGeometryResourceSet(
        context.graph, drawItemSets, LengthOf(drawItemSets), context.scratchArena,
        Name("tests/material_geometry_uses/set"), "Geometry Set", resourceSet
    ));
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(context.graph);
        ASSERT_TRUE(declarations.valid());
        EXPECT_EQ(declarations.resourceCount(), buffers.size());
        const Core::GpuTaskGraphResourceSetView set = declarations.resourceSetAt(resourceSet.index);
        ASSERT_EQ(set.memberCount, uses.size());
        for(usize resourceIndex = 0u; resourceIndex < uses.size(); ++resourceIndex)
            EXPECT_EQ(set.members[resourceIndex], uses[resourceIndex].resource);
    }

    const u64 previousGeneration = uses.front().resource.generation;
    context.graph.reset();
    ASSERT_TRUE(Impl::RendererTaskGraphDetail::GatherPreparedMaterialGeometryUses(
        context.graph, drawItemSets, LengthOf(drawItemSets), context.scratchArena, uses
    ));
    ASSERT_NO_FATAL_FAILURE(ExpectUses(context.graph, uses, expected));
    EXPECT_NE(uses.front().resource.generation, previousGeneration);
}

TEST(MaterialGeometryUses, RejectsInvalidInputsBeforeImportAndClearsOutput){
    GeometryContext context;
    BufferVector buffers{ context.scratchArena };
    ASSERT_TRUE(CreateBuffers(context, s_SourceBufferCount, buffers));
    Impl::MaterialPassDrawItems drawItems(context.scratchArena);
    drawItems.meshDrawItems.reserve(2u);
    drawItems.meshDrawItems.push_back(MakeDrawItem(buffers, 0u));
    drawItems.meshDrawItems.push_back(drawItems.meshDrawItems.front());
    const Impl::MaterialPassDrawItems* const drawItemSets[] = { &drawItems };
    ResourceUseVector uses{ context.scratchArena };
    uses.reserve(1u);

    uses.emplace_back();
    EXPECT_FALSE(Impl::RendererTaskGraphDetail::GatherPreparedMaterialGeometryUses(
        context.graph, nullptr, 1u, context.scratchArena, uses
    ));
    EXPECT_TRUE(uses.empty());
    const Impl::MaterialPassDrawItems* const nullSet[] = { &drawItems, nullptr };
    uses.emplace_back();
    EXPECT_FALSE(Impl::RendererTaskGraphDetail::GatherPreparedMaterialGeometryUses(
        context.graph, nullSet, LengthOf(nullSet), context.scratchArena, uses
    ));
    EXPECT_TRUE(uses.empty());

    for(u32 invalidCase = 0u; invalidCase < 5u; ++invalidCase){
        drawItems.meshDrawItems.back() = drawItems.meshDrawItems.front();
        Impl::MaterialPassMeshResourceSnapshot& mesh = drawItems.meshDrawItems.back().meshResources;
        switch(invalidCase){
        case 0u: mesh.sourceBuffers.normalBuffer = nullptr; break;
        case 1u: mesh.geometryHeapHandles[NWB_MESH_BINDING_NORMAL] = Core::GpuDescriptorHandle::invalid(); break;
        case 2u:
            mesh.geometryHeapHandles[NWB_MESH_BINDING_NORMAL] = Core::GpuDescriptorHandle::make(Core::GpuDescriptorClass::SampledImage, 1u);
            break;
        case 3u: mesh.meshletCount = 0u; break;
        case 4u: mesh.meshletPrimitiveIndexCount = 0u; break;
        }
        uses.emplace_back();
        EXPECT_FALSE(Impl::RendererTaskGraphDetail::GatherPreparedMaterialGeometryUses(
            context.graph, drawItemSets, LengthOf(drawItemSets), context.scratchArena, uses
        ));
        EXPECT_TRUE(uses.empty());
        const Core::GpuTaskGraph::DeclarationReadView declarations(context.graph);
        ASSERT_TRUE(declarations.valid());
        EXPECT_EQ(declarations.resourceCount(), 0u);
    }

    uses.emplace_back();
    EXPECT_TRUE(Impl::RendererTaskGraphDetail::GatherPreparedMaterialGeometryUses(
        context.graph, nullptr, 0u, context.scratchArena, uses
    ));
    EXPECT_TRUE(uses.empty());
}

TEST(MaterialGeometryUses, PreservesPartialImportFailureAndReusesUnnamedExistingBuffer){
    GeometryContext context;
    BufferVector buffers{ context.scratchArena };
    ASSERT_TRUE(CreateBuffers(context, s_SourceBufferCount, buffers));
    const Core::BufferHandle unnamedBuffer = context.makeBuffer(NAME_NONE);
    ASSERT_TRUE(unnamedBuffer);
    buffers.back() = unnamedBuffer;
    Impl::MaterialPassDrawItems drawItems(context.scratchArena);
    drawItems.meshDrawItems.reserve(1u);
    drawItems.meshDrawItems.push_back(MakeDrawItem(buffers, 0u));
    const Impl::MaterialPassDrawItems* const drawItemSets[] = { &drawItems };
    ResourceUseVector uses{ context.scratchArena };
    EXPECT_FALSE(Impl::RendererTaskGraphDetail::GatherPreparedMaterialGeometryUses(
        context.graph, drawItemSets, LengthOf(drawItemSets), context.scratchArena, uses
    ));
    EXPECT_TRUE(uses.empty());
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(context.graph);
        ASSERT_TRUE(declarations.valid());
        EXPECT_EQ(declarations.resourceCount(), s_SourceBufferCount - 1u);
    }

    // Different objects with the same import identity remain an error; pointer deduplication must not merge them.
    buffers.back() = context.makeBuffer(buffers.front()->getCreationDescription().debugName);
    ASSERT_TRUE(buffers.back());
    drawItems.meshDrawItems.front() = MakeDrawItem(buffers, 0u);
    EXPECT_FALSE(Impl::RendererTaskGraphDetail::GatherPreparedMaterialGeometryUses(
        context.graph, drawItemSets, LengthOf(drawItemSets), context.scratchArena, uses
    ));
    EXPECT_TRUE(uses.empty());
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(context.graph);
        ASSERT_TRUE(declarations.valid());
        EXPECT_EQ(declarations.resourceCount(), s_SourceBufferCount - 1u);
    }

    context.graph.reset();
    buffers.back() = unnamedBuffer;
    drawItems.meshDrawItems.front() = MakeDrawItem(buffers, 0u);
    const Core::GpuGraphResourceId unnamedImport = context.graph.importBuffer(
        unnamedBuffer,
        Impl::RendererTaskGraphDetail::BufferResourceDesc(Name("tests/material_geometry_uses/external"), "External")
    );
    ASSERT_TRUE(unnamedImport.valid());
    ASSERT_TRUE(Impl::RendererTaskGraphDetail::GatherPreparedMaterialGeometryUses(
        context.graph, drawItemSets, LengthOf(drawItemSets), context.scratchArena, uses
    ));
    ASSERT_NO_FATAL_FAILURE(ExpectUses(context.graph, uses, buffers));
    EXPECT_EQ(uses.back().resource, unnamedImport);
}

TEST(MaterialGeometryUses, ResolvesImportsAcrossUnrelatedResourcesAndMetadataOnlyDeclarations){
    GeometryContext context;
    BufferVector buffers{ context.scratchArena };
    ASSERT_TRUE(CreateBuffers(context, 2u * s_SourceBufferCount, buffers));
    BufferVector expected{ context.scratchArena };
    expected.reserve(s_SourceBufferCount);
    for(usize bufferIndex = 0u; bufferIndex < s_SourceBufferCount; ++bufferIndex)
        expected.push_back(buffers[bufferIndex]);
    Impl::MaterialPassDrawItems drawItems(context.scratchArena);
    drawItems.meshDrawItems.reserve(1u);
    drawItems.meshDrawItems.push_back(MakeDrawItem(buffers, 0u));
    const Impl::MaterialPassDrawItems* const drawItemSets[] = { &drawItems };

    const Core::GpuGraphResourceId metadataResource = context.graph.importResource(
        Impl::RendererTaskGraphDetail::BufferResourceDesc(Name("tests/material_geometry_uses/metadata"), "Metadata")
    );
    ASSERT_TRUE(metadataResource.valid());
    for(usize bufferIndex = 0u; bufferIndex < s_SourceBufferCount; ++bufferIndex){
        const Core::BufferHandle& unrelated = buffers[s_SourceBufferCount + bufferIndex];
        const Core::GpuGraphResourceId unrelatedImport = context.graph.importBuffer(
            unrelated, Impl::RendererTaskGraphDetail::BufferResourceDesc(unrelated->getCreationDescription().debugName, "Unrelated")
        );
        ASSERT_TRUE(unrelatedImport.valid());
        // Leave the final requested buffer absent so the first gather must resolve the other inputs and then import it.
        if(bufferIndex + 1u < s_SourceBufferCount){
            const Core::BufferHandle& requested = buffers[bufferIndex];
            const Core::GpuGraphResourceId requestedImport = context.graph.importBuffer(
                requested, Impl::RendererTaskGraphDetail::BufferResourceDesc(requested->getCreationDescription().debugName, "Requested")
            );
            ASSERT_TRUE(requestedImport.valid());
        }
    }
    ResourceUseVector uses{ context.scratchArena };
    ASSERT_TRUE(Impl::RendererTaskGraphDetail::GatherPreparedMaterialGeometryUses(
        context.graph, drawItemSets, LengthOf(drawItemSets), context.scratchArena, uses
    ));
    ASSERT_NO_FATAL_FAILURE(ExpectUses(context.graph, uses, expected));

    const Core::GpuGraphResourceId trailingResource = context.graph.importResource(
        Impl::RendererTaskGraphDetail::HazardDomainDesc(Name("tests/material_geometry_uses/tail"), "Tail")
    );
    ASSERT_TRUE(trailingResource.valid());
    ASSERT_TRUE(Impl::RendererTaskGraphDetail::GatherPreparedMaterialGeometryUses(
        context.graph, drawItemSets, LengthOf(drawItemSets), context.scratchArena, uses
    ));
    ASSERT_NO_FATAL_FAILURE(ExpectUses(context.graph, uses, expected));
    const Core::GpuTaskGraph::DeclarationReadView declarations(context.graph);
    ASSERT_TRUE(declarations.valid());
    EXPECT_EQ(declarations.resourceCount(), buffers.size() + 2u);
}

TEST(MaterialGeometryUses, ReservedOutputReleasesTupleAndBufferMembershipStorage){
    constexpr usize s_DrawCounts[]{ 1u, 64u };
    for(const usize drawCount : s_DrawCounts){
        GeometryContext context;
        BufferVector buffers{ context.scratchArena };
        ASSERT_TRUE(CreateBuffers(context, drawCount * s_SourceBufferCount, buffers));
        Impl::MaterialPassDrawItems drawItems(context.scratchArena);
        drawItems.meshDrawItems.reserve(drawCount);
        for(usize index = 0u; index < drawCount; ++index)
            drawItems.meshDrawItems.push_back(MakeDrawItem(buffers, index * s_SourceBufferCount));
        const Impl::MaterialPassDrawItems* const drawItemSets[] = { &drawItems };
        Core::Alloc::ScratchArena gatherScratch(Name("tests/material_geometry_uses/membership_storage"));
        ResourceUseVector uses{ gatherScratch };
        uses.reserve(drawCount * NWB_MESH_INSTANCE_GEOMETRY_SLOT_COUNT);
        const ArenaMemoryStats before = gatherScratch.memoryStats();
        for(usize pass = 0u; pass < 2u; ++pass){
            ASSERT_TRUE(Impl::RendererTaskGraphDetail::GatherPreparedMaterialGeometryUses(
                context.graph, drawItemSets, LengthOf(drawItemSets), gatherScratch, uses
            ));
            EXPECT_EQ(uses.size(), buffers.size());
            EXPECT_EQ(gatherScratch.memoryStats().usedBytes, before.usedBytes);
            ASSERT_NO_FATAL_FAILURE(ExpectUses(context.graph, uses, buffers));
        }
    }
}

TEST(MaterialGeometryUses, TracksEveryChangedSourceInRepeatedMeshTuples){
    GeometryContext context;
    BufferVector buffers{ context.scratchArena };
    ASSERT_TRUE(CreateBuffers(context, 2u * s_SourceBufferCount, buffers));
    using BufferMember = Core::BufferHandle Impl::RuntimeMeshBuffers::*;
    constexpr BufferMember s_SourceMembers[] = {
        &Impl::RuntimeMeshBuffers::positionBuffer,
        &Impl::RuntimeMeshBuffers::normalBuffer,
        &Impl::RuntimeMeshBuffers::tangentBuffer,
        &Impl::RuntimeMeshBuffers::uv0Buffer,
        &Impl::RuntimeMeshBuffers::colorBuffer,
        &Impl::RuntimeMeshBuffers::meshletDescBuffer,
        &Impl::RuntimeMeshBuffers::meshletBoundsBuffer,
        &Impl::RuntimeMeshBuffers::meshletPositionRefDeltaBuffer,
        &Impl::RuntimeMeshBuffers::meshletAttributeRefDeltaBuffer,
        &Impl::RuntimeMeshBuffers::meshletLocalVertexRefBuffer,
        &Impl::RuntimeMeshBuffers::meshletPrimitiveIndexBuffer,
    };
    Impl::MaterialPassDrawItems drawItems(context.scratchArena);
    drawItems.meshDrawItems.reserve(3u);
    drawItems.computeDrawItems.reserve(1u);
    for(usize drawIndex = 0u; drawIndex < 3u; ++drawIndex)
        drawItems.meshDrawItems.push_back(MakeDrawItem(buffers, 0u));
    drawItems.computeDrawItems.push_back(drawItems.meshDrawItems.front());
    const Impl::MaterialPassDrawItems* const drawItemSets[] = { &drawItems, &drawItems };
    BufferVector expected{ context.scratchArena };
    expected.reserve(s_SourceBufferCount + 1u);
    for(usize bufferIndex = 0u; bufferIndex < s_SourceBufferCount; ++bufferIndex)
        expected.push_back(buffers[bufferIndex]);
    expected.push_back({});
    Core::Alloc::ScratchArena gatherScratchArena(Name("tests/material_geometry_uses/changed_tuple_scratch"));
    ResourceUseVector uses{ gatherScratchArena };

    // Every lane participates in tuple identity, even when all other buffers and the position buffer are shared.
    // Keep earlier imports in the graph and mutate the frozen snapshot between calls to detect stale lookup state.
    for(usize sourceIndex = 0u; sourceIndex < LengthOf(s_SourceMembers); ++sourceIndex){
        Impl::MaterialPassDrawItem& changed = drawItems.meshDrawItems[1u];
        changed = drawItems.meshDrawItems.front();
        const Core::BufferHandle& replacement = buffers[s_SourceBufferCount + sourceIndex];
        changed.meshResources.sourceBuffers.*s_SourceMembers[sourceIndex] = replacement;
        drawItems.computeDrawItems.front() = changed;
        expected.back() = replacement;
        ASSERT_TRUE(Impl::RendererTaskGraphDetail::GatherPreparedMaterialGeometryUses(
            context.graph, drawItemSets, LengthOf(drawItemSets), gatherScratchArena, uses
        ));
        ASSERT_NO_FATAL_FAILURE(ExpectUses(context.graph, uses, expected));
    }
    drawItems.meshDrawItems[1u] = drawItems.meshDrawItems.front();
    drawItems.computeDrawItems.front() = drawItems.meshDrawItems.front();
    expected.pop_back();
    ASSERT_TRUE(Impl::RendererTaskGraphDetail::GatherPreparedMaterialGeometryUses(
        context.graph, drawItemSets, LengthOf(drawItemSets), gatherScratchArena, uses
    ));
    ASSERT_NO_FATAL_FAILURE(ExpectUses(context.graph, uses, expected));
}

TEST(MaterialGeometryUses, GathersRepeatedMeshes){
    MeasureGatherWorkload(128u, 16u, 1u);
}

TEST(MaterialGeometryUses, GathersMostlyUniqueMeshes){
    MeasureGatherWorkload(128u, 1u, 1u);
}

TEST(MaterialGeometryUses, GathersOneMesh){
    MeasureGatherWorkload(1u, 1u, 64u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

