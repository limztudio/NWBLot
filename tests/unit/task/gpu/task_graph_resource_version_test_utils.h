// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <tests/common/test_context.h>

#include "task_graph_test_utils.h"

#include <gtest/gtest.h>

#include <core/task/gpu/compiler_internal.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TaskGraphResourceVersionTestUtils{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestArena = ::NWB::Tests::TestArena<struct TaskGraphResourceVersionTestsTag>;
using TaskGraphTestUtils::Assign;

inline constexpr Name s_ResourceVersionScratchArena("tests/task/gpu/resource_version_scratch");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Core::GpuTaskResourceRange BufferRange(const u64 byteOffset, const u64 byteSize)noexcept;

[[nodiscard]] inline Expected<Core::GpuGraphResourceId> AddBuffer(Core::GpuTaskGraph& graph, const Name& identity){
    return graph.importResource(Core::GpuGraphResourceDesc{}.setIdentity(identity).setMarkerLabel("Resource Version Buffer").setType(Core::GpuGraphResourceType::Buffer).setInitialState(Core::ResourceStates::Common));
}

[[nodiscard]] inline Expected<Core::GpuGraphResourceVersionId> AddVersion(
    Core::GpuTaskGraph& graph,
    const Core::GpuGraphResourceId resource,
    const Core::GpuGraphResourceVersionOrigin::Enum origin,
    const Core::GpuTaskResourceRange& range = BufferRange(0u, 64u)
){
    return graph.declareResourceVersion(Core::GpuGraphResourceVersionDesc{}.setResource(resource).setRange(range).setOrigin(origin));
}

[[nodiscard]] inline Core::GpuTaskResourceUse ResourceUse(const Core::GpuGraphResourceId resource, const Core::GpuTaskResourceRange& range, const Core::ResourceStates::Mask state, const Core::GpuTaskResourceAccess::Enum access)noexcept{
    return Core::GpuTaskResourceUse{ .resource = resource, .range = range, .requiredState = state, .access = access, };
}

[[nodiscard]] inline Core::GpuTaskResourceVersionUse VersionUse(const Core::GpuGraphResourceVersionId version, const Core::GpuTaskResourceVersionRole::Enum role)noexcept{
    return Core::GpuTaskResourceVersionUse{ .version = version, .role = role, };
}

struct VersionProducerUses{
    Core::GpuTaskResourceUse resourceUse;
    Core::GpuTaskResourceVersionUse versionUse;
};

struct VersionConsumerUses{
    Core::GpuTaskResourceUse resourceUse;
    Core::GpuTaskResourceVersionUse versionUse;
};

[[nodiscard]] inline VersionProducerUses ProducerUses(const Core::GpuGraphResourceId resource, const Core::GpuGraphResourceVersionId version, const Core::GpuTaskResourceRange& range = BufferRange(0u, 64u))noexcept{
    return VersionProducerUses{ .resourceUse = ResourceUse(resource, range, Core::ResourceStates::UnorderedAccess, Core::GpuTaskResourceAccess::Write), .versionUse = VersionUse(version, Core::GpuTaskResourceVersionRole::Produce), };
}

[[nodiscard]] inline VersionConsumerUses ConsumerUses(const Core::GpuGraphResourceId resource, const Core::GpuGraphResourceVersionId version, const Core::GpuTaskResourceRange& range = BufferRange(0u, 64u))noexcept{
    return VersionConsumerUses{ .resourceUse = ResourceUse(resource, range, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read), .versionUse = VersionUse(version, Core::GpuTaskResourceVersionRole::Consume), };
}

[[nodiscard]] inline Expected<Core::GpuTaskId> AddTask(
    const Name& identity,
    const Span<const Core::GpuTaskResourceUse> resourceUses,
    const Span<const Core::GpuTaskResourceVersionUse> versionUses,
    const Span<const Core::GpuTaskId> dependencies,
    const Span<const Core::GpuExternalCompletionId> externalDependencies,
    Core::GpuTaskGraph& graph
){
    return TaskGraphTestUtils::AddTask(identity, "Resource Version Task", dependencies, resourceUses, {}, versionUses, externalDependencies, graph);
}

[[nodiscard]] b8 Compile(Core::GpuTaskGraphAnalysis& analysis, Core::GpuTaskGraphQueueAssignments& assignments, Core::GpuCompiledGraph& compiledGraph, Core::Alloc::ScratchArena& scratchArena, const Core::GpuTaskGraph& graph);

void ExpectClosedCycle(const Core::GpuTaskGraphAnalysis& analysis);

[[nodiscard]] b8 HasResourceVersionEdge(
    const Core::GpuTaskGraphAnalysis& analysis,
    const Core::GpuTaskId producer,
    const Core::GpuTaskId consumer,
    const Core::GpuGraphResourceId resource,
    const Core::GpuGraphResourceVersionId version,
    const Core::GpuTaskHazardType::Enum hazard
)noexcept;

void ExpectDiagnosticMatchesVersionCycleEdge(const Core::GpuTaskGraphAnalysis& analysis);

void ExpectInvalidBoundUse(
    const Name& identity,
    const Core::GpuGraphResourceVersionOrigin::Enum origin,
    const Core::GpuTaskResourceRange& versionRange,
    const Core::GpuTaskResourceRange& physicalRange,
    const Core::GpuTaskResourceAccess::Enum access,
    const Core::GpuTaskResourceVersionRole::Enum role
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

