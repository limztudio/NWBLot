// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <tests/common/capturing_logger.h>
#include <tests/common/gpu_task_graph_read_views.h>
#include <tests/common/graphics_metadata_test_objects.h>
#include <tests/common/test_context.h>

#include <cstddef>

#include <gtest/gtest.h>

#include <core/task/gpu/capture/command_ir.h>
#include <core/task/gpu/capture/command_ir_internal.h>
#include <core/task/gpu/compiler.h>
#include <core/task/gpu/compiler_internal.h>
#include <core/task/gpu/packet_runtime.h>
#include <core/task/gpu/queue_assignment_telemetry.h>
#include <core/graphics/vulkan/backend.h>
#include <core/graphics/vulkan/command_validation.h>
#include <core/graphics/vulkan/device_detail.h>
#include <core/graphics/vulkan/state_tracking_detail.h>
#include <core/telemetry/frame_graph_contributor.h>
#include <global/filesystem/operations.h>
#include <global/filesystem/path.h>
#include <global/text_utils.h>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TaskGraphTestUtils{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestArena = ::NWB::Tests::TestArena<struct TaskGraphTestsTag>;
namespace Graphics = Core;
namespace Telemetry = Core::Telemetry;
using TestPath = ::Path<Graphics::Alloc::GlobalArena>;

inline constexpr Name s_TaskGraphScratchArena("tests/task/gpu/scratch");


void ExpectMemoryStatsEqual(const ArenaMemoryStats& expected, const ArenaMemoryStats& actual);


using ::NWB::Tests::NewMetadataOnlyBuffer;
using ::NWB::Tests::NewMetadataOnlyTexture;


struct ImportedTexturePair{
    Graphics::GpuGraphResourceId source;
    Graphics::GpuGraphResourceId destination;
};


[[nodiscard]] ImportedTexturePair ImportTexturePair(
    TestArena& testArena,
    Graphics::GraphicsBackend::VulkanContext& context,
    Graphics::GraphicsBackend::VulkanAllocator& allocator,
    Graphics::GpuTaskGraph& graph,
    const Graphics::TextureDesc& sourceDescription,
    const Graphics::TextureDesc& destinationDescription
);


[[nodiscard]] Graphics::GpuGraphResourceId AddHazardDomain(
    Graphics::GpuTaskGraph& graph,
    const Name& identity,
    const AStringView label
);

[[nodiscard]] Graphics::GpuGraphResourceId AddTextureMetadata(
    Graphics::GpuTaskGraph& graph,
    const Name& identity,
    const AStringView label,
    const Graphics::ResourceStates::Mask initialState = Graphics::ResourceStates::Common,
    const Graphics::ResourceQueueSharing::Mask queueSharing = Graphics::ResourceQueueSharing::Exclusive
);

[[nodiscard]] Graphics::GpuGraphResourceId AddPresentationTexture(
    TestArena& testArena,
    Graphics::GraphicsBackend::VulkanContext& context,
    Graphics::GraphicsBackend::VulkanAllocator& allocator,
    Graphics::GpuTaskGraph& graph,
    const Name& identity,
    const AStringView label,
    const Graphics::ResourceStates::Mask initialState,
    const Graphics::ResourceStates::Mask externalFinalState = Graphics::ResourceStates::Present,
    const Graphics::GpuPhysicalQueueId externalFinalReleaseDestinationQueue = {}
);

[[nodiscard]] Graphics::GpuGraphResourceId AddBufferMetadata(
    Graphics::GpuTaskGraph& graph,
    const Name& identity,
    const AStringView label,
    const Graphics::ResourceStates::Mask initialState = Graphics::ResourceStates::Common,
    const Graphics::ResourceQueueSharing::Mask queueSharing = Graphics::ResourceQueueSharing::Exclusive
);

[[nodiscard]] Graphics::GpuGraphResourceId AddAccelStructMetadata(
    Graphics::GpuTaskGraph& graph,
    const Name& identity,
    const AStringView label,
    const Graphics::ResourceStates::Mask initialState = Graphics::ResourceStates::Common,
    const Graphics::ResourceQueueSharing::Mask queueSharing = Graphics::ResourceQueueSharing::Exclusive
);

[[nodiscard]] Graphics::GpuGraphPipelineId AddPipelineMetadata(
    Graphics::GpuTaskGraph& graph,
    const Name& identity,
    const AStringView label,
    const Graphics::GpuGraphPipelineType::Enum type
);

[[nodiscard]] Graphics::GpuTaskId AddTask(
    Graphics::GpuTaskGraph& graph,
    const Name& identity,
    const AStringView label,
    const Graphics::GpuTaskId* const dependencies = nullptr,
    const usize dependencyCount = 0u,
    const Graphics::GpuTaskResourceUse* const resourceUses = nullptr,
    const usize resourceUseCount = 0u,
    const Graphics::GpuTaskResourceSetUse* const resourceSetUses = nullptr,
    const usize resourceSetUseCount = 0u
);

[[nodiscard]] Graphics::GpuTaskId AddTaskWithQueue(
    Graphics::GpuTaskGraph& graph,
    const Name& identity,
    const AStringView label,
    const Graphics::GpuQueueRequest& queue,
    const Graphics::GpuTaskSchedulingHint& scheduling = {},
    const Graphics::GpuTaskTimingMetadata& timing = {},
    const Graphics::GpuTaskId* const dependencies = nullptr,
    const usize dependencyCount = 0u
);

[[nodiscard]] bool Analyze(
    const Graphics::GpuTaskGraph& graph,
    Graphics::GpuTaskGraphAnalysis& analysis
);

[[nodiscard]] bool Assign(
    const Graphics::GpuTaskGraph& graph,
    const Graphics::GpuTaskGraphAnalysis& analysis,
    const Graphics::GpuTaskGraphQueueTopology& topology,
    Graphics::GpuTaskGraphQueueAssignments& assignments,
    const Graphics::GpuTaskGraphQueueAssignmentOptions& options = {}
);

[[nodiscard]] bool Compile(
    const Graphics::GpuTaskGraph& graph,
    Graphics::GpuTaskGraphAnalysis& analysis,
    const Graphics::GpuTaskGraphQueueTopology& topology,
    Graphics::GpuTaskGraphQueueAssignments& assignments,
    Graphics::GpuCompiledGraph& compiledGraph,
    const Graphics::GpuTaskGraphCompileOptions& options = {}
);

[[nodiscard]] constexpr Graphics::GpuQueueCapability::Mask QueueCapabilities(
    const Graphics::GpuQueueCapability::Mask first,
    const Graphics::GpuQueueCapability::Mask second = Graphics::GpuQueueCapability::None,
    const Graphics::GpuQueueCapability::Mask third = Graphics::GpuQueueCapability::None
){
    return static_cast<Graphics::GpuQueueCapability::Mask>(
        static_cast<u8>(first)
        | static_cast<u8>(second)
        | static_cast<u8>(third)
    );
}

[[nodiscard]] Graphics::GpuPhysicalQueueInfo GraphicsQueue(
    const u16 index = 0u,
    const Graphics::GpuQueueCapability::Mask capabilities = QueueCapabilities(
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueueCapability::Compute,
        Graphics::GpuQueueCapability::Transfer
    )
);

[[nodiscard]] Graphics::GpuPhysicalQueueInfo DedicatedComputeQueue(const u16 index = 1u);

[[nodiscard]] Graphics::GpuPhysicalQueueInfo DedicatedTransferQueue(const u16 index = 2u);

[[nodiscard]] Telemetry::FrameGraphQueueAssignmentModifier::Mask ExpectedTelemetryModifiers(
    const Graphics::GpuTaskQueueAssignmentModifier::Mask modifiers
);

void ExpectPlannedQueueAssignmentTelemetry(
    const Graphics::GpuTaskQueueAssignment& source,
    const Telemetry::FrameGraphQueueAssignment& telemetry,
    const Telemetry::FrameGraphQueueClass::Enum queueClass,
    const Telemetry::FrameGraphQueueAssignmentReason::Enum reason
);

struct TransferOwnershipPair{
    Graphics::GpuGraphResourceId texture;
    Graphics::GpuTaskId producer;
    Graphics::GpuTaskId consumer;
};

[[nodiscard]] TransferOwnershipPair AddTransferOwnershipPair(
    Graphics::GpuTaskGraph& graph,
    const Graphics::ResourceQueueSharing::Mask queueSharing,
    const bool allowFallback = true
);

[[nodiscard]] const Graphics::GpuTaskDependencyEdge* FindEdge(
    const Graphics::GpuTaskGraphAnalysis& analysis,
    const Graphics::GpuTaskId producer,
    const Graphics::GpuTaskId consumer
);

struct ExternalFinalReadPair{
    Graphics::GpuGraphResourceId resource;
    Graphics::GpuTaskId earlierReader;
    Graphics::GpuTaskId finalizingReader;
};


[[nodiscard]] ExternalFinalReadPair AddExternalFinalReadPair(
    Graphics::GpuTaskGraph& graph,
    const Graphics::GpuGraphResourceType::Enum resourceType,
    const Graphics::GpuTaskResourceRange& earlierRange,
    const Graphics::GpuTaskResourceRange& finalizingRange,
    const Graphics::ResourceStates::Mask readState,
    const Graphics::ResourceStates::Mask externalFinalState,
    const Graphics::ResourceQueueSharing::Mask queueSharing,
    const bool samePacket,
    const bool finalizerDependsOnEarlier,
    const Graphics::GpuPhysicalQueueId externalFinalReleaseDestinationQueue
);


[[nodiscard]] bool HasInferredHazard(
    const Graphics::GpuTaskGraphAnalysis& analysis,
    const Graphics::GpuTaskId producer,
    const Graphics::GpuTaskId consumer,
    const Graphics::GpuGraphResourceId resource,
    const Graphics::GpuTaskHazardType::Enum hazard
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

