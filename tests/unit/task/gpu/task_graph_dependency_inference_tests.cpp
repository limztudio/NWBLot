// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_dependency_inference_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, ExpandsImmutableResourceSetsIntoConcreteHazards){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const auto textureAResult = AddTextureMetadata(graph, Name("tests/task_graph/resource_set_texture_a"), "Resource Set Texture A");
    ASSERT_TRUE(textureAResult);
    const Graphics::GpuGraphResourceId textureA = *textureAResult;
    const auto textureBResult = AddTextureMetadata(graph, Name("tests/task_graph/resource_set_texture_b"), "Resource Set Texture B");
    ASSERT_TRUE(textureBResult);
    const Graphics::GpuGraphResourceId textureB = *textureBResult;
    const auto bufferResult = AddBufferMetadata(graph, Name("tests/task_graph/resource_set_buffer"), "Resource Set Buffer");
    ASSERT_TRUE(bufferResult);
    const Graphics::GpuGraphResourceId buffer = *bufferResult;
    ASSERT_TRUE(textureA.valid());
    ASSERT_TRUE(textureB.valid());
    ASSERT_TRUE(buffer.valid());

    const Graphics::GpuGraphResourceId members[] = { textureA, textureB, buffer };
    const Graphics::GpuGraphResourceSetDesc resourceSetDesc = Graphics::GpuGraphResourceSetDesc{}.setIdentity(Name("tests/task_graph/visible_resources")).setMarkerLabel("Visible Resources").setMembers(members, LengthOf(members));
    const auto resourceSetResult = graph.importResourceSet(resourceSetDesc);
    ASSERT_TRUE(resourceSetResult);
    const Graphics::GpuGraphResourceSetId resourceSet = *resourceSetResult;
    ASSERT_TRUE(resourceSet.valid());
    {
        const auto gpuTaskAdmission = graph.importResourceSet(resourceSetDesc);
        EXPECT_TRUE(gpuTaskAdmission);
        if(gpuTaskAdmission)
            EXPECT_EQ(*gpuTaskAdmission, resourceSet);
    }

    const Graphics::GpuGraphResourceId duplicateMembers[] = { textureA, textureA };
    EXPECT_FALSE(graph.importResourceSet(Graphics::GpuGraphResourceSetDesc{}.setIdentity(Name("tests/task_graph/duplicate_resources")).setMarkerLabel("Duplicate Resources").setMembers(duplicateMembers, LengthOf(duplicateMembers))).has_value());

    const Graphics::GpuTaskResourceSetUse writeSetUse{ .resourceSet = resourceSet, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::Write, };
    const Graphics::GpuTaskResourceSetUse readSetUse{ .resourceSet = resourceSet, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read, };
    const auto writerResult = AddTask(graph, Name("tests/task_graph/resource_set_writer"), "Resource Set Writer", nullptr, 0u, nullptr, 0u, &writeSetUse, 1u);
    ASSERT_TRUE(writerResult);
    const Graphics::GpuTaskId writer = *writerResult;
    const auto readerResult = AddTask(graph, Name("tests/task_graph/resource_set_reader"), "Resource Set Reader", nullptr, 0u, nullptr, 0u, &readSetUse, 1u);
    ASSERT_TRUE(readerResult);
    const Graphics::GpuTaskId reader = *readerResult;
    ASSERT_TRUE(writer.valid());
    ASSERT_TRUE(reader.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(operationScratchArena, graph, analysis));
    ASSERT_EQ(analysis.edges().size(), 1u);
    const Graphics::GpuTaskDependencyEdge* const edge = FindEdge(analysis, writer, reader);
    ASSERT_NE(edge, nullptr);
    EXPECT_EQ(edge->hazard, Graphics::GpuTaskHazardType::ReadAfterWrite);
    ASSERT_EQ(analysis.inferredEdges().size(), LengthOf(members));
    for(const Graphics::GpuGraphResourceId member : members){
        EXPECT_TRUE(HasInferredHazard(analysis, writer, reader, member, Graphics::GpuTaskHazardType::ReadAfterWrite));
    }

    graph.reset();
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);

        EXPECT_FALSE(declarations.validResourceSet(resourceSet));
    }
    const Graphics::GpuTaskResourceSetUse staleSetUse{ .resourceSet = resourceSet, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read, };
    EXPECT_FALSE(AddTask(graph, Name("tests/task_graph/stale_resource_set"), "Stale Resource Set", nullptr, 0u, nullptr, 0u, &staleSetUse, 1u).has_value());
}

TEST(GpuTaskGraph, RejectsMalformedBufferRangesAndDetectsTextureOverlap){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuGraphResourceDesc bufferDesc;
    bufferDesc.setIdentity(Name("tests/task_graph/range_buffer")).setMarkerLabel("Range Buffer").setType(Graphics::GpuGraphResourceType::Buffer);
    const auto bufferResult = graph.importResource(bufferDesc);
    ASSERT_TRUE(bufferResult);
    const Graphics::GpuGraphResourceId buffer = *bufferResult;
    ASSERT_TRUE(buffer.valid());

    Graphics::GpuTaskResourceRange zeroBufferRange;
    zeroBufferRange.bufferRange = Graphics::BufferRange(0u, 0u);
    const Graphics::GpuTaskResourceUse zeroBufferUse{ .resource = buffer, .range = zeroBufferRange, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read, };
    {
        const auto gpuTaskAdmission = AddTask(graph, Name("tests/task_graph/zero_buffer_range"), "Zero Buffer Range", nullptr, 0u, &zeroBufferUse, 1u);
        ASSERT_TRUE(gpuTaskAdmission);
        ASSERT_TRUE(gpuTaskAdmission->valid());
    }
    Graphics::GpuTaskGraphAnalysis zeroRangeAnalysis(testArena.arena);
    EXPECT_FALSE(Analyze(operationScratchArena, graph, zeroRangeAnalysis));
    EXPECT_EQ(zeroRangeAnalysis.diagnostic().status, Graphics::GpuTaskGraphAnalysisStatus::InvalidResourceUse);

    graph.reset();
    const auto overflowBufferResult = graph.importResource(bufferDesc);
    ASSERT_TRUE(overflowBufferResult);
    const Graphics::GpuGraphResourceId overflowBuffer = *overflowBufferResult;
    ASSERT_TRUE(overflowBuffer.valid());
    Graphics::GpuTaskResourceRange overflowBufferRange;
    overflowBufferRange.bufferRange = Graphics::BufferRange(Limit<u64>::s_Max - 3u, 4u);
    const Graphics::GpuTaskResourceUse overflowBufferUse{ .resource = overflowBuffer, .range = overflowBufferRange, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read, };
    {
        const auto secondGpuTaskAdmission = AddTask(graph, Name("tests/task_graph/overflow_buffer_range"), "Overflow Buffer Range", nullptr, 0u, &overflowBufferUse, 1u);
        ASSERT_TRUE(secondGpuTaskAdmission);
        ASSERT_TRUE(secondGpuTaskAdmission->valid());
    }
    Graphics::GpuTaskGraphAnalysis overflowRangeAnalysis(testArena.arena);
    EXPECT_FALSE(Analyze(operationScratchArena, graph, overflowRangeAnalysis));
    EXPECT_EQ(overflowRangeAnalysis.diagnostic().status, Graphics::GpuTaskGraphAnalysisStatus::InvalidResourceUse);

    graph.reset();
    Graphics::GpuGraphResourceDesc textureDesc;
    textureDesc.setIdentity(Name("tests/task_graph/overlap_texture")).setMarkerLabel("Overlap Texture").setType(Graphics::GpuGraphResourceType::Texture);
    const auto textureResult = graph.importResource(textureDesc);
    ASSERT_TRUE(textureResult);
    const Graphics::GpuGraphResourceId texture = *textureResult;
    ASSERT_TRUE(texture.valid());
    Graphics::GpuTaskResourceRange firstTextureRange;
    firstTextureRange.textureSubresources = Graphics::TextureSubresourceSet(0u, s_ExpectedDualCount, 0u, 1u);
    Graphics::GpuTaskResourceRange secondTextureRange;
    secondTextureRange.textureSubresources = Graphics::TextureSubresourceSet(1u, 1u, 0u, 1u);
    const Graphics::GpuTaskResourceUse firstTextureUse{ .resource = texture, .range = firstTextureRange, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::Write, };
    const Graphics::GpuTaskResourceUse secondTextureUse{ .resource = texture, .range = secondTextureRange, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read, };
    const auto firstResult = AddTask(graph, Name("tests/task_graph/overlap_texture_writer"), "Overlap Texture Writer", nullptr, 0u, &firstTextureUse, 1u);
    ASSERT_TRUE(firstResult);
    const Graphics::GpuTaskId first = *firstResult;
    const auto secondResult = AddTask(graph, Name("tests/task_graph/overlap_texture_reader"), "Overlap Texture Reader", nullptr, 0u, &secondTextureUse, 1u);
    ASSERT_TRUE(secondResult);
    const Graphics::GpuTaskId second = *secondResult;
    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());
    Graphics::GpuTaskGraphAnalysis overlapAnalysis(testArena.arena);
    ASSERT_TRUE(Analyze(operationScratchArena, graph, overlapAnalysis));
    EXPECT_NE(FindEdge(overlapAnalysis, first, second), nullptr);
}

TEST(GpuTaskGraph, TracksOnlyTheNearestWholeResourceWriters){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const auto resourceResult = AddHazardDomain(graph, Name("tests/task_graph/nearest_writer_resource"), "Nearest Writer Resource");
    ASSERT_TRUE(resourceResult);
    const Graphics::GpuGraphResourceId resource = *resourceResult;
    ASSERT_TRUE(resource.valid());
    const Graphics::GpuTaskResourceUse writerUse{ .resource = resource, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::Write, };
    const auto firstResult = AddTask(graph, Name("tests/task_graph/nearest_writer_first"), "Nearest Writer First", nullptr, 0u, &writerUse, 1u);
    ASSERT_TRUE(firstResult);
    const Graphics::GpuTaskId first = *firstResult;
    const auto secondResult = AddTask(graph, Name("tests/task_graph/nearest_writer_second"), "Nearest Writer Second", nullptr, 0u, &writerUse, 1u);
    ASSERT_TRUE(secondResult);
    const Graphics::GpuTaskId second = *secondResult;
    const auto thirdResult = AddTask(graph, Name("tests/task_graph/nearest_writer_third"), "Nearest Writer Third", nullptr, 0u, &writerUse, 1u);
    ASSERT_TRUE(thirdResult);
    const Graphics::GpuTaskId third = *thirdResult;
    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());
    ASSERT_TRUE(third.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(operationScratchArena, graph, analysis));
    EXPECT_NE(FindEdge(analysis, first, second), nullptr);
    EXPECT_NE(FindEdge(analysis, second, third), nullptr);
    EXPECT_EQ(FindEdge(analysis, first, third), nullptr);
    ASSERT_EQ(analysis.inferredEdges().size(), s_ExpectedDualCount);
    EXPECT_TRUE(HasInferredHazard(analysis, first, second, resource, Graphics::GpuTaskHazardType::WriteAfterWrite));
    EXPECT_TRUE(HasInferredHazard(analysis, second, third, resource, Graphics::GpuTaskHazardType::WriteAfterWrite));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

