// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "gpu_task_graph_read_views.h"

#include <tests/common/arena_owner_fixture.h>
#include <tests/common/capturing_logger.h>
#include <tests/common/test_context.h>
#include <tests/unit/task/gpu/case_types.adhd.h>

#include <core/alloc/scratch.h>
#include <core/task/gpu/capture/command_ir.h>
#include <core/task/gpu/capture/command_ir_internal.h>
#include <core/task/gpu/compiler_internal.h>
#include <core/task/gpu/packet_runtime.h>
#include <core/task/gpu/queue_assignment_telemetry.h>
#include <core/telemetry/frame_graph_contributor.h>

#include <global/text_utils.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TaskGraphTestUtils{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestArena = ::NWB::Tests::TestArena<struct TaskGraphTestsTag>;
namespace Telemetry = Core::Telemetry;
using NWB::Tests::ExpectMemoryStatsEqual;

inline constexpr Name s_TaskGraphScratchArena = NWB::Tests::TaskGraphTestUtils::s_TaskGpuScratch;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<Name> IndexedName(const Name& prefix, const usize index);

[[nodiscard]] Expected<Core::GpuGraphResourceId> AddHazardDomain(Core::GpuTaskGraph& graph, const Name& identity, const AStringView label);

[[nodiscard]] Expected<Core::GpuGraphResourceId> AddTextureMetadata(
    Core::GpuTaskGraph& graph,
    const Name& identity,
    const AStringView label,
    const Core::ResourceStates::Mask initialState = Core::ResourceStates::Common,
    const Core::ResourceQueueSharing::Mask queueSharing = Core::ResourceQueueSharing::Exclusive
);

[[nodiscard]] Expected<Core::GpuGraphResourceId> AddBufferMetadata(
    Core::GpuTaskGraph& graph,
    const Name& identity,
    const AStringView label,
    const Core::ResourceStates::Mask initialState = Core::ResourceStates::Common,
    const Core::ResourceQueueSharing::Mask queueSharing = Core::ResourceQueueSharing::Exclusive
);

[[nodiscard]] Expected<Core::GpuGraphResourceId> AddAccelStructMetadata(
    Core::GpuTaskGraph& graph,
    const Name& identity,
    const AStringView label,
    const Core::ResourceStates::Mask initialState = Core::ResourceStates::Common,
    const Core::ResourceQueueSharing::Mask queueSharing = Core::ResourceQueueSharing::Exclusive
);

[[nodiscard]] Expected<Core::GpuGraphPipelineId> AddPipelineMetadata(Core::GpuTaskGraph& graph, const Name& identity, const AStringView label, const Core::GpuGraphPipelineType::Enum type);

[[nodiscard]] Expected<Core::GpuTaskId> AddTask(
    const Name& identity,
    const AStringView label,
    const Span<const Core::GpuTaskId> dependencies,
    const Span<const Core::GpuTaskResourceUse> resourceUses,
    const Span<const Core::GpuTaskResourceSetUse> resourceSetUses,
    const Span<const Core::GpuTaskResourceVersionUse> resourceVersionUses,
    const Span<const Core::GpuExternalCompletionId> externalDependencies,
    Core::GpuTaskGraph& graph
);

[[nodiscard]] Expected<Core::GpuTaskId> AddTaskWithCommands(
    const Name& identity,
    const AStringView label,
    const Core::GpuTaskCommandRequirements& commands,
    const Core::GpuTaskSchedulingHint& scheduling,
    const Core::GpuTaskTimingMetadata& timing,
    const Span<const Core::GpuTaskId> dependencies,
    Core::GpuTaskGraph& graph
);

[[nodiscard]] b8 Analyze(const Core::GpuTaskGraph& graph, Core::Alloc::ScratchArena& scratchArena, Core::GpuTaskGraphAnalysis& analysis);

[[nodiscard]] b8 Assign(
    Core::GpuTaskGraphQueueAssignments& assignments,
    Core::Alloc::ScratchArena& scratchArena,
    const Core::GpuTaskGraph& graph,
    const Core::GpuTaskGraphAnalysis& analysis,
    const Core::GpuPhysicalQueueTopology& topology,
    const Core::GpuTaskGraphQueueAssignmentOptions& options = {}
);

[[nodiscard]] b8 Compile(
    Core::GpuTaskGraphAnalysis& analysis,
    Core::GpuTaskGraphQueueAssignments& assignments,
    Core::GpuCompiledGraph& compiledGraph,
    Core::Alloc::ScratchArena& scratchArena,
    const Core::GpuTaskGraph& graph,
    const Core::GpuPhysicalQueueTopology& topology,
    const Core::GpuTaskGraphCompileOptions& options = {}
);

// Compile on a topology where Compute and Transfer capabilities stay on their dedicated queue classes.
[[nodiscard]] b8 CompileWithSeparatedCommandQueues(
    Core::GpuTaskGraphAnalysis& analysis,
    Core::GpuTaskGraphQueueAssignments& assignments,
    Core::GpuCompiledGraph& compiledGraph,
    Core::Alloc::ScratchArena& scratchArena,
    const Core::GpuTaskGraph& graph,
    const Core::GpuPhysicalQueueTopology& topology,
    const Core::GpuTaskGraphCompileOptions& options = {}
);

struct SingleQueueCompile{
    Core::Alloc::ScratchArena scratchArena;
    Core::GpuPhysicalQueueInfo singleQueue;
    Core::GpuPhysicalQueueTopology topology;
    Core::GpuTaskGraphAnalysis analysis;
    Core::GpuTaskGraphQueueAssignments assignments;
    Core::GpuCompiledGraph compiledGraph;


public:
    explicit SingleQueueCompile(TestArena& testArena);


public:
    [[nodiscard]] b8 compile(const Core::GpuTaskGraph& graph, const Core::GpuTaskGraphCompileOptions& options = {});
};

struct TwoQueueCompile{
    Core::Alloc::ScratchArena scratchArena;
    Core::GpuPhysicalQueueInfo graphicsQueue;
    Core::GpuPhysicalQueueInfo computeQueue;
    Array<Core::GpuPhysicalQueueInfo, 2> queueStorage{};
    Core::GpuPhysicalQueueTopology topology;
    Core::GpuTaskGraphAnalysis analysis;
    Core::GpuTaskGraphQueueAssignments assignments;
    Core::GpuCompiledGraph compiledGraph;


public:
    explicit TwoQueueCompile(TestArena& testArena);


public:
    [[nodiscard]] b8 compile(const Core::GpuTaskGraph& graph, const Core::GpuTaskGraphCompileOptions& options = {});
};

struct ThreeQueueCompile{
    Core::Alloc::ScratchArena scratchArena;
    Core::GpuPhysicalQueueInfo graphicsQueue;
    Core::GpuPhysicalQueueInfo computeQueue;
    Core::GpuPhysicalQueueInfo transferQueue;
    Array<Core::GpuPhysicalQueueInfo, 3> queueStorage{};
    Core::GpuPhysicalQueueTopology topology;
    Core::GpuTaskGraphAnalysis analysis;
    Core::GpuTaskGraphQueueAssignments assignments;
    Core::GpuCompiledGraph compiledGraph;


public:
    explicit ThreeQueueCompile(TestArena& testArena);


public:
    [[nodiscard]] b8 compile(const Core::GpuTaskGraph& graph, const Core::GpuTaskGraphCompileOptions& options = {});
};

[[nodiscard]] constexpr Core::GpuQueueCapability::Mask QueueCapabilities(
    const Core::GpuQueueCapability::Mask first,
    const Core::GpuQueueCapability::Mask second = Core::GpuQueueCapability::None,
    const Core::GpuQueueCapability::Mask third = Core::GpuQueueCapability::None
)noexcept{
    return static_cast<Core::GpuQueueCapability::Mask>(static_cast<u8>(first) | static_cast<u8>(second) | static_cast<u8>(third));
}

[[nodiscard]] Core::GpuPhysicalQueueInfo GraphicsQueue(
    const u16 index = 0u,
    const Core::GpuQueueCapability::Mask capabilities = QueueCapabilities(Core::GpuQueueCapability::Graphics, Core::GpuQueueCapability::Compute, Core::GpuQueueCapability::Transfer)
)noexcept;

[[nodiscard]] Core::GpuPhysicalQueueInfo DedicatedComputeQueue(const u16 index = 1u)noexcept;

[[nodiscard]] Core::GpuPhysicalQueueInfo DedicatedTransferQueue(const u16 index = 2u)noexcept;

[[nodiscard]] inline Core::GpuTaskCommandRequirements GraphicsCommands()noexcept{
    return Core::GpuTaskCommandRequirements{ Core::GpuQueueCapability::Graphics };
}

[[nodiscard]] inline Core::GpuTaskCommandRequirements ComputeCommands()noexcept{
    return Core::GpuTaskCommandRequirements{ Core::GpuQueueCapability::Compute };
}

struct TransferOwnershipPair{
    Core::GpuGraphResourceId texture;
    Core::GpuTaskId producer;
    Core::GpuTaskId consumer;
};

[[nodiscard]] Expected<TransferOwnershipPair> AddTransferOwnershipPair(Core::GpuTaskGraph& graph, const Core::ResourceQueueSharing::Mask queueSharing);

[[nodiscard]] const Core::GpuTaskDependencyEdge* FindEdge(const Core::GpuTaskGraphAnalysis& analysis, const Core::GpuTaskId producer, const Core::GpuTaskId consumer)noexcept;

struct ExternalFinalReadPair{
    Core::GpuGraphResourceId resource;
    Core::GpuTaskId earlierReader;
    Core::GpuTaskId finalizingReader;
};

[[nodiscard]] Expected<ExternalFinalReadPair> AddExternalFinalReadPair(
    Core::GpuTaskGraph& graph,
    const Core::GpuGraphResourceType::Enum resourceType,
    const Core::GpuTaskResourceRange& earlierRange,
    const Core::GpuTaskResourceRange& finalizingRange,
    const Core::ResourceStates::Mask readState,
    const Core::ResourceStates::Mask externalFinalState,
    const Core::ResourceQueueSharing::Mask queueSharing,
    const b8 samePacket,
    const b8 finalizerDependsOnEarlier,
    const Core::GpuPhysicalQueueId externalFinalReleaseDestinationQueue
);

[[nodiscard]] b8 HasInferredHazard(const Core::GpuTaskGraphAnalysis& analysis, const Core::GpuTaskId producer, const Core::GpuTaskId consumer, const Core::GpuGraphResourceId resource, const Core::GpuTaskHazardType::Enum hazard)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

