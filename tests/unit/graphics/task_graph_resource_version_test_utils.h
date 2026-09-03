// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <tests/common/graphics_metadata_test_objects.h>
#include <tests/common/test_context.h>

#include <gtest/gtest.h>

#include <core/graphics/task_graph/compiler.h>
#include <core/graphics/vulkan/backend.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TaskGraphResourceVersionTestUtils{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestArena = ::NWB::Tests::TestArena<struct TaskGraphResourceVersionTestsTag>;
namespace Graphics = Core;

inline constexpr Name s_ResourceVersionScratchArena("tests/graphics/task_graph_resource_version_scratch");


[[nodiscard]] inline Graphics::GpuPhysicalQueueInfo GraphicsQueue(){
    return Graphics::GpuPhysicalQueueInfo{
        .id = Graphics::GpuPhysicalQueueId{ 0u, 1u },
        .queueClass = Graphics::CommandQueue::Graphics,
        .capabilities = static_cast<Graphics::GpuQueueCapability::Mask>(
            static_cast<u8>(Graphics::GpuQueueCapability::Graphics)
            | static_cast<u8>(Graphics::GpuQueueCapability::Compute)
            | static_cast<u8>(Graphics::GpuQueueCapability::Transfer)
        ),
        .familyIndex = 0u,
        .queueIndex = 0u,
        .dedicated = false,
    };
}

[[nodiscard]] inline Graphics::GpuPhysicalQueueInfo DedicatedComputeQueue(){
    return Graphics::GpuPhysicalQueueInfo{
        .id = Graphics::GpuPhysicalQueueId{ 1u, 1u },
        .queueClass = Graphics::CommandQueue::Compute,
        .capabilities = static_cast<Graphics::GpuQueueCapability::Mask>(
            static_cast<u8>(Graphics::GpuQueueCapability::Compute)
            | static_cast<u8>(Graphics::GpuQueueCapability::Transfer)
        ),
        .familyIndex = 1u,
        .queueIndex = 0u,
        .dedicated = true,
    };
}

[[nodiscard]] inline Graphics::GpuTaskResourceRange BufferRange(const u64 byteOffset, const u64 byteSize){
    Graphics::GpuTaskResourceRange range;
    range.bufferRange = Graphics::BufferRange(byteOffset, byteSize);
    return range;
}

[[nodiscard]] inline Graphics::GpuTaskResourceRange TextureRange(
    const Graphics::MipLevel baseMipLevel,
    const Graphics::MipLevel numMipLevels,
    const Graphics::ArraySlice baseArraySlice,
    const Graphics::ArraySlice numArraySlices
){
    Graphics::GpuTaskResourceRange range;
    range.textureSubresources = Graphics::TextureSubresourceSet(
        baseMipLevel,
        numMipLevels,
        baseArraySlice,
        numArraySlices
    );
    return range;
}

[[nodiscard]] inline Graphics::GpuGraphResourceId AddBuffer(Graphics::GpuTaskGraph& graph, const Name& identity){
    return graph.importResource(
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(identity)
            .setMarkerLabel("Resource Version Buffer")
            .setType(Graphics::GpuGraphResourceType::Buffer)
            .setInitialState(Graphics::ResourceStates::Common)
    );
}

[[nodiscard]] inline Graphics::GpuGraphResourceVersionId AddVersion(
    Graphics::GpuTaskGraph& graph,
    const Graphics::GpuGraphResourceId resource,
    const Graphics::GpuGraphResourceVersionOrigin::Enum origin,
    const Graphics::GpuTaskResourceRange& range = BufferRange(0u, 64u)
){
    return graph.declareResourceVersion(
        Graphics::GpuGraphResourceVersionDesc{}
            .setResource(resource)
            .setRange(range)
            .setOrigin(origin)
    );
}

[[nodiscard]] inline Graphics::GpuTaskResourceUse ResourceUse(
    const Graphics::GpuGraphResourceId resource,
    const Graphics::GpuTaskResourceRange& range,
    const Graphics::ResourceStates::Mask state,
    const Graphics::GpuTaskResourceAccess::Enum access
){
    return Graphics::GpuTaskResourceUse{
        .resource = resource,
        .range = range,
        .requiredState = state,
        .access = access,
    };
}

[[nodiscard]] inline Graphics::GpuTaskResourceVersionUse VersionUse(
    const Graphics::GpuGraphResourceVersionId version,
    const Graphics::GpuTaskResourceVersionRole::Enum role
){
    return Graphics::GpuTaskResourceVersionUse{
        .version = version,
        .role = role,
    };
}

[[nodiscard]] inline Graphics::GpuTaskId AddTask(
    Graphics::GpuTaskGraph& graph,
    const Name& identity,
    const Graphics::GpuTaskResourceUse* const resourceUses,
    const usize resourceUseCount,
    const Graphics::GpuTaskResourceVersionUse* const versionUses,
    const usize versionUseCount,
    const Graphics::GpuTaskId* const dependencies = nullptr,
    const usize dependencyCount = 0u,
    const Graphics::GpuExternalCompletionId* const externalDependencies = nullptr,
    const usize externalDependencyCount = 0u
){
    Graphics::GpuTaskDesc desc;
    desc
        .setIdentity(identity)
        .setMarkerLabel("Resource Version Task")
        .setDependencies(dependencies, dependencyCount)
        .setExternalDependencies(externalDependencies, externalDependencyCount)
        .setResourceUses(resourceUses, resourceUseCount)
        .setResourceVersionUses(versionUses, versionUseCount)
    ;
    return graph.addTask(desc);
}

[[nodiscard]] inline bool Analyze(const Graphics::GpuTaskGraph& graph, Graphics::GpuTaskGraphAnalysis& analysis){
    Core::Alloc::ScratchArena scratchArena(s_ResourceVersionScratchArena);
    const Graphics::GpuTaskGraphCompiler compiler;
    return compiler.analyze(graph, analysis, scratchArena);
}

[[nodiscard]] inline bool Assign(
    const Graphics::GpuTaskGraph& graph,
    const Graphics::GpuTaskGraphAnalysis& analysis,
    const Graphics::GpuTaskGraphQueueTopology& topology,
    Graphics::GpuTaskGraphQueueAssignments& assignments
){
    Core::Alloc::ScratchArena scratchArena(s_ResourceVersionScratchArena);
    const Graphics::GpuTaskGraphCompiler compiler;
    return compiler.assignQueues(graph, analysis, topology, assignments, scratchArena);
}

[[nodiscard]] inline bool Compile(
    const Graphics::GpuTaskGraph& graph,
    Graphics::GpuTaskGraphAnalysis& analysis,
    Graphics::GpuTaskGraphQueueAssignments& assignments,
    Graphics::GpuCompiledGraph& compiledGraph
){
    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    Graphics::GpuTaskGraphCompileOptions options;
    options.allowMetadataOnlyTasks = true;
    Core::Alloc::ScratchArena scratchArena(s_ResourceVersionScratchArena);
    const Graphics::GpuTaskGraphCompiler compiler;
    return compiler.compile(graph, analysis, topology, assignments, compiledGraph, scratchArena, options);
}

inline void ExpectClosedCycle(const Graphics::GpuTaskGraphAnalysis& analysis){
    ASSERT_GE(analysis.cyclePath().size(), 3u);
    EXPECT_EQ(analysis.cyclePath().front(), analysis.cyclePath().back());
    ASSERT_EQ(analysis.cycleEdges().size(), analysis.cyclePath().size() - 1u);
    for(usize edgeIndex = 0u; edgeIndex < analysis.cycleEdges().size(); ++edgeIndex){
        EXPECT_EQ(analysis.cycleEdges()[edgeIndex].producer, analysis.cyclePath()[edgeIndex]);
        EXPECT_EQ(analysis.cycleEdges()[edgeIndex].consumer, analysis.cyclePath()[edgeIndex + 1u]);
    }
}

[[nodiscard]] inline bool HasResourceVersionEdge(
    const Graphics::GpuTaskGraphAnalysis& analysis,
    const Graphics::GpuTaskId producer,
    const Graphics::GpuTaskId consumer,
    const Graphics::GpuGraphResourceId resource,
    const Graphics::GpuGraphResourceVersionId version,
    const Graphics::GpuTaskHazardType::Enum hazard
){
    for(const Graphics::GpuTaskDependencyEdge& edge : analysis.inferredEdges()){
        if(
            edge.producer == producer
            && edge.consumer == consumer
            && edge.resource == resource
            && edge.resourceVersion == version
            && edge.hazard == hazard
        )
            return true;
    }
    return false;
}

inline void ExpectDiagnosticMatchesVersionCycleEdge(const Graphics::GpuTaskGraphAnalysis& analysis){
    const Graphics::GpuTaskGraphAnalysisDiagnostic& diagnostic = analysis.diagnostic();
    ASSERT_TRUE(diagnostic.resource.valid());
    ASSERT_TRUE(diagnostic.resourceVersion.valid());
    bool matched = false;
    for(const Graphics::GpuTaskDependencyEdge& edge : analysis.cycleEdges()){
        if(
            edge.producer == diagnostic.relatedTask
            && edge.consumer == diagnostic.task
            && edge.resource == diagnostic.resource
            && edge.resourceVersion == diagnostic.resourceVersion
            && (
                edge.hazard == Graphics::GpuTaskHazardType::VersionDependency
                || edge.hazard == Graphics::GpuTaskHazardType::VersionLifetime
            )
        ){
            matched = true;
            break;
        }
    }
    EXPECT_TRUE(matched);
}

inline void ExpectInvalidBoundUse(
    const Name& identity,
    const Graphics::GpuGraphResourceVersionOrigin::Enum origin,
    const Graphics::GpuTaskResourceRange& versionRange,
    const Graphics::GpuTaskResourceRange& physicalRange,
    const Graphics::GpuTaskResourceAccess::Enum access,
    const Graphics::GpuTaskResourceVersionRole::Enum role
){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId resource = AddBuffer(graph, identity);
    const Graphics::GpuGraphResourceVersionId version = AddVersion(graph, resource, origin, versionRange);
    const Graphics::ResourceStates::Mask state = access == Graphics::GpuTaskResourceAccess::Read
        ? Graphics::ResourceStates::ShaderResource
        : Graphics::ResourceStates::UnorderedAccess
    ;
    const Graphics::GpuTaskResourceUse resourceUse = ResourceUse(resource, physicalRange, state, access);
    const Graphics::GpuTaskResourceVersionUse versionUse = VersionUse(version, role);
    const Graphics::GpuTaskId task = AddTask(graph, identity, &resourceUse, 1u, &versionUse, 1u);
    ASSERT_TRUE(resource.valid());
    ASSERT_TRUE(version.valid());
    ASSERT_TRUE(task.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    EXPECT_FALSE(Analyze(graph, analysis));
    EXPECT_EQ(analysis.diagnostic().status, Graphics::GpuTaskGraphAnalysisStatus::InvalidResourceVersionUse);
    EXPECT_EQ(analysis.diagnostic().task, task);
    EXPECT_EQ(analysis.diagnostic().resource, resource);
    EXPECT_EQ(analysis.diagnostic().resourceVersion, version);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

