// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "runtime.h"

#include <core/task/cpu/scheduler.h>
#include <core/task/gpu/task_desc.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GraphicsModuleDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_SetupUploadLargeMinimumBytes = 1024u * 1024u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SetupUploadSameClassRouting{
    GpuPhysicalQueueId primaryQueue;
    b8 enabled = false;
    b8 crossesQueueFamily = false;
};


using GraphTaskDeclaration = GraphicsRuntime::StandaloneTaskGraphDeclaration;


[[nodiscard]] SetupUploadSameClassRouting ResolveSetupUploadSameClassRouting(GraphicsBackend::Device& device, CommandQueue::Enum consumerQueue, usize uploadBytes)noexcept;
[[nodiscard]] ResourceQueueSharing::Mask ResolveSetupUploadConsumerSharing(ResourceQueueSharing::Mask requestedSharing, CommandQueue::Enum consumerQueue, b8 crossFamilySameClassRouting = false)noexcept;
[[nodiscard]] CommandQueue::Enum ResolveSetupUploadConsumerQueue(GraphicsBackend::Device& device, CommandQueue::Enum requestedConsumerQueue, usize uploadBytes, b8 hasKnownFinalState)noexcept;
[[nodiscard]] GpuTaskSchedulingHint SetupUploadGraphScheduling(usize byteCount, b8 sameClassRouting = false, b8 crossFamilySameClassRouting = false)noexcept;
[[nodiscard]] ResourceStates::Mask SetupUploadGraphFinalState(ResourceStates::Mask declaredInitialState)noexcept;

[[nodiscard]] Expected<QueueSubmissionToken> SubmitGraphOwnedStandaloneTask(
    const GraphicsRuntime& graphics,
    NotNull<GraphTaskDeclaration> declareTask,
    void* userData,
    StandaloneGraphWorkspacePool& workspaces,
    GpuPhysicalQueueId requiredTerminalQueue = {},
    CpuTaskScheduler* readyFrontierScheduler = nullptr,
    GpuTimingRecorder* timingRecorder = nullptr,
    GpuTimingFrameTransaction* frameTimingTransaction = nullptr
);
[[nodiscard]] b8 SubmitGraphOwnedSetupUpload(
    const GraphicsRuntime& graphics,
    ResourceQueueSharing::Mask queueSharing,
    CommandQueue::Enum consumerQueue,
    NotNull<GraphTaskDeclaration> declareTask,
    GpuPhysicalQueueId requiredTerminalQueue,
    void* userData,
    StandaloneGraphWorkspacePool& workspaces,
    QueueSubmissionToken& outUploadToken
);
[[nodiscard]] b8 SubmitGraphOwnedFrameTimingReset(const GraphicsRuntime& graphics, StandaloneGraphWorkspacePool& workspaces, GpuTimingRecorder& timing);

b8 ValidateBufferSetupUpload(const GraphicsRuntime::BufferSetupDesc& desc);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

