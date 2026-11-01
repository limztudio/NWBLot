// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_dependency_inference_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, ExpandsImmutableResourceSetsIntoConcreteHazards){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto textureAResult = AddTextureMetadata(graph, Name("tests/task_graph/resource_set_texture_a"), "Resource Set Texture A");
    ASSERT_TRUE(textureAResult);
    const Core::GpuGraphResourceId textureA = *textureAResult;
    const auto textureBResult = AddTextureMetadata(graph, Name("tests/task_graph/resource_set_texture_b"), "Resource Set Texture B");
    ASSERT_TRUE(textureBResult);
    const Core::GpuGraphResourceId textureB = *textureBResult;
    const auto bufferResult = AddBufferMetadata(graph, Name("tests/task_graph/resource_set_buffer"), "Resource Set Buffer");
    ASSERT_TRUE(bufferResult);
    const Core::GpuGraphResourceId buffer = *bufferResult;
    ASSERT_TRUE(textureA.valid());
    ASSERT_TRUE(textureB.valid());
    ASSERT_TRUE(buffer.valid());

    const Array<Core::GpuGraphResourceId, 3> members = { textureA, textureB, buffer };
    const Core::GpuGraphResourceSetDesc resourceSetDesc = Core::GpuGraphResourceSetDesc{}.setIdentity(Name("tests/task_graph/visible_resources")).setMarkerLabel("Visible Resources").setMembers(members);
    const auto resourceSetResult = graph.importResourceSet(resourceSetDesc);
    ASSERT_TRUE(resourceSetResult);
    const Core::GpuGraphResourceSetId resourceSet = *resourceSetResult;
    ASSERT_TRUE(resourceSet.valid());
    {
        const auto gpuTaskAdmission = graph.importResourceSet(resourceSetDesc);
        EXPECT_TRUE(gpuTaskAdmission);
        if(gpuTaskAdmission)
            EXPECT_EQ(*gpuTaskAdmission, resourceSet);
    }

    const Array<Core::GpuGraphResourceId, 2> duplicateMembers = { textureA, textureA };
    EXPECT_FALSE(graph.importResourceSet(Core::GpuGraphResourceSetDesc{}.setIdentity(Name("tests/task_graph/duplicate_resources")).setMarkerLabel("Duplicate Resources").setMembers(duplicateMembers)).has_value());

    const Core::GpuTaskResourceSetUse writeSetUse{ .resourceSet = resourceSet, .range = {}, .requiredState = Core::ResourceStates::UnorderedAccess, .access = Core::GpuTaskResourceAccess::Write, };
    const Core::GpuTaskResourceSetUse readSetUse{ .resourceSet = resourceSet, .range = {}, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read, };
    const auto writerResult = AddTask(Name("tests/task_graph/resource_set_writer"), "Resource Set Writer", {}, {}, Span(&writeSetUse, 1u), {}, {}, graph);
    ASSERT_TRUE(writerResult);
    const Core::GpuTaskId writer = *writerResult;
    const auto readerResult = AddTask(Name("tests/task_graph/resource_set_reader"), "Resource Set Reader", {}, {}, Span(&readSetUse, 1u), {}, {}, graph);
    ASSERT_TRUE(readerResult);
    const Core::GpuTaskId reader = *readerResult;
    ASSERT_TRUE(writer.valid());
    ASSERT_TRUE(reader.valid());

    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, operationScratchArena, analysis));
    ASSERT_EQ(analysis.edges().size(), 1u);
    const Core::GpuTaskDependencyEdge* const edge = FindEdge(analysis, writer, reader);
    ASSERT_NE(edge, nullptr);
    EXPECT_EQ(edge->hazard, Core::GpuTaskHazardType::ReadAfterWrite);
    ASSERT_EQ(analysis.inferredEdges().size(), LengthOf(members));
    for(const Core::GpuGraphResourceId member : members){
        EXPECT_TRUE(HasInferredHazard(analysis, writer, reader, member, Core::GpuTaskHazardType::ReadAfterWrite));
    }

    graph.reset();
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);

        EXPECT_FALSE(declarations.validResourceSet(resourceSet));
    }
    const Core::GpuTaskResourceSetUse staleSetUse{ .resourceSet = resourceSet, .range = {}, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read, };
    EXPECT_FALSE(AddTask(Name("tests/task_graph/stale_resource_set"), "Stale Resource Set", {}, {}, Span(&staleSetUse, 1u), {}, {}, graph).has_value());
}

TEST(GpuTaskGraph, RejectsMalformedBufferRangesAndDetectsTextureOverlap){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    Core::GpuGraphResourceDesc bufferDesc;
    bufferDesc.setIdentity(Name("tests/task_graph/range_buffer")).setMarkerLabel("Range Buffer").setType(Core::GpuGraphResourceType::Buffer);
    const auto bufferResult = graph.importResource(bufferDesc);
    ASSERT_TRUE(bufferResult);
    const Core::GpuGraphResourceId buffer = *bufferResult;
    ASSERT_TRUE(buffer.valid());

    Core::GpuTaskResourceRange zeroBufferRange;
    zeroBufferRange.bufferRange = Core::BufferRange(0u, 0u);
    const Core::GpuTaskResourceUse zeroBufferUse{ .resource = buffer, .range = zeroBufferRange, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read, };
    {
        const auto gpuTaskAdmission = AddTask(Name("tests/task_graph/zero_buffer_range"), "Zero Buffer Range", {}, Span(&zeroBufferUse, 1u), {}, {}, {}, graph);
        ASSERT_TRUE(gpuTaskAdmission);
        ASSERT_TRUE(gpuTaskAdmission->valid());
    }
    Core::GpuTaskGraphAnalysis zeroRangeAnalysis(testArena.arena);
    EXPECT_FALSE(Analyze(graph, operationScratchArena, zeroRangeAnalysis));
    EXPECT_EQ(zeroRangeAnalysis.diagnostic().status, Core::GpuTaskGraphAnalysisStatus::InvalidResourceUse);

    graph.reset();
    const auto overflowBufferResult = graph.importResource(bufferDesc);
    ASSERT_TRUE(overflowBufferResult);
    const Core::GpuGraphResourceId overflowBuffer = *overflowBufferResult;
    ASSERT_TRUE(overflowBuffer.valid());
    Core::GpuTaskResourceRange overflowBufferRange;
    overflowBufferRange.bufferRange = Core::BufferRange(Limit<u64>::s_Max - 3u, 4u);
    const Core::GpuTaskResourceUse overflowBufferUse{ .resource = overflowBuffer, .range = overflowBufferRange, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read, };
    {
        const auto secondGpuTaskAdmission = AddTask(Name("tests/task_graph/overflow_buffer_range"), "Overflow Buffer Range", {}, Span(&overflowBufferUse, 1u), {}, {}, {}, graph);
        ASSERT_TRUE(secondGpuTaskAdmission);
        ASSERT_TRUE(secondGpuTaskAdmission->valid());
    }
    Core::GpuTaskGraphAnalysis overflowRangeAnalysis(testArena.arena);
    EXPECT_FALSE(Analyze(graph, operationScratchArena, overflowRangeAnalysis));
    EXPECT_EQ(overflowRangeAnalysis.diagnostic().status, Core::GpuTaskGraphAnalysisStatus::InvalidResourceUse);

    graph.reset();
    Core::GpuGraphResourceDesc textureDesc;
    textureDesc.setIdentity(Name("tests/task_graph/overlap_texture")).setMarkerLabel("Overlap Texture").setType(Core::GpuGraphResourceType::Texture);
    const auto textureResult = graph.importResource(textureDesc);
    ASSERT_TRUE(textureResult);
    const Core::GpuGraphResourceId texture = *textureResult;
    ASSERT_TRUE(texture.valid());
    Core::GpuTaskResourceRange firstTextureRange;
    firstTextureRange.textureSubresources = Core::TextureSubresourceSet(0u, 2u, 0u, 1u);
    Core::GpuTaskResourceRange secondTextureRange;
    secondTextureRange.textureSubresources = Core::TextureSubresourceSet(1u, 1u, 0u, 1u);
    const Core::GpuTaskResourceUse firstTextureUse{ .resource = texture, .range = firstTextureRange, .requiredState = Core::ResourceStates::UnorderedAccess, .access = Core::GpuTaskResourceAccess::Write, };
    const Core::GpuTaskResourceUse secondTextureUse{ .resource = texture, .range = secondTextureRange, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read, };
    const auto firstResult = AddTask(Name("tests/task_graph/overlap_texture_writer"), "Overlap Texture Writer", {}, Span(&firstTextureUse, 1u), {}, {}, {}, graph);
    ASSERT_TRUE(firstResult);
    const Core::GpuTaskId first = *firstResult;
    const auto secondResult = AddTask(Name("tests/task_graph/overlap_texture_reader"), "Overlap Texture Reader", {}, Span(&secondTextureUse, 1u), {}, {}, {}, graph);
    ASSERT_TRUE(secondResult);
    const Core::GpuTaskId second = *secondResult;
    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());
    Core::GpuTaskGraphAnalysis overlapAnalysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, operationScratchArena, overlapAnalysis));
    EXPECT_NE(FindEdge(overlapAnalysis, first, second), nullptr);
}

TEST(GpuTaskGraph, TracksOnlyTheNearestWholeResourceWriters){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto resourceResult = AddHazardDomain(graph, Name("tests/task_graph/nearest_writer_resource"), "Nearest Writer Resource");
    ASSERT_TRUE(resourceResult);
    const Core::GpuGraphResourceId resource = *resourceResult;
    ASSERT_TRUE(resource.valid());
    const Core::GpuTaskResourceUse writerUse{ .resource = resource, .range = {}, .requiredState = Core::ResourceStates::UnorderedAccess, .access = Core::GpuTaskResourceAccess::Write, };
    const auto firstResult = AddTask(Name("tests/task_graph/nearest_writer_first"), "Nearest Writer First", {}, Span(&writerUse, 1u), {}, {}, {}, graph);
    ASSERT_TRUE(firstResult);
    const Core::GpuTaskId first = *firstResult;
    const auto secondResult = AddTask(Name("tests/task_graph/nearest_writer_second"), "Nearest Writer Second", {}, Span(&writerUse, 1u), {}, {}, {}, graph);
    ASSERT_TRUE(secondResult);
    const Core::GpuTaskId second = *secondResult;
    const auto thirdResult = AddTask(Name("tests/task_graph/nearest_writer_third"), "Nearest Writer Third", {}, Span(&writerUse, 1u), {}, {}, {}, graph);
    ASSERT_TRUE(thirdResult);
    const Core::GpuTaskId third = *thirdResult;
    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());
    ASSERT_TRUE(third.valid());

    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, operationScratchArena, analysis));
    EXPECT_NE(FindEdge(analysis, first, second), nullptr);
    EXPECT_NE(FindEdge(analysis, second, third), nullptr);
    EXPECT_EQ(FindEdge(analysis, first, third), nullptr);
    ASSERT_EQ(analysis.inferredEdges().size(), 2u);
    EXPECT_TRUE(HasInferredHazard(analysis, first, second, resource, Core::GpuTaskHazardType::WriteAfterWrite));
    EXPECT_TRUE(HasInferredHazard(analysis, second, third, resource, Core::GpuTaskHazardType::WriteAfterWrite));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

