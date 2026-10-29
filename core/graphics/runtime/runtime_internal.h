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
[[nodiscard]] CommandQueue::Enum ResolveSetupUploadConsumerQueue(GraphicsBackend::Device& device, CommandQueue::Enum requestedConsumerQueue, usize uploadBytes, b8 hasKnownFinalState, b8 requiresGraphicsConsumerQueue = false)noexcept;
[[nodiscard]] GpuTaskSchedulingHint SetupUploadGraphScheduling(usize byteCount, b8 sameClassRouting = false, b8 crossFamilySameClassRouting = false)noexcept;
[[nodiscard]] ResourceStates::Mask SetupUploadGraphFinalState(ResourceStates::Mask declaredInitialState)noexcept;

[[nodiscard]] Expected<QueueSubmissionToken> SubmitGraphOwnedStandaloneTask(
    const GraphicsRuntime& graphics,
    StandaloneGraphWorkspacePool& workspaces,
    void* userData,
    NotNull<GraphTaskDeclaration> declareTask,
    GpuPhysicalQueueId requiredTerminalQueue = {},
    CpuTaskScheduler* readyFrontierScheduler = nullptr,
    GpuTimingRecorder* timingRecorder = nullptr,
    GpuTimingFrameTransaction* frameTimingTransaction = nullptr
);
[[nodiscard]] b8 SubmitGraphOwnedSetupUpload(
    QueueSubmissionToken& inOutUploadToken,
    StandaloneGraphWorkspacePool& workspaces,
    const GraphicsRuntime& graphics,
    ResourceQueueSharing::Mask queueSharing,
    CommandQueue::Enum consumerQueue,
    void* userData,
    NotNull<GraphTaskDeclaration> declareTask,
    GpuPhysicalQueueId requiredTerminalQueue = {}
);
[[nodiscard]] b8 SubmitGraphOwnedFrameTimingReset(const GraphicsRuntime& graphics, StandaloneGraphWorkspacePool& workspaces, GpuTimingRecorder& timing);

b8 ValidateBufferSetupUpload(const GraphicsRuntime::BufferSetupDesc& desc);
b8 ValidateTextureSetupUpload(const GraphicsRuntime::TextureSetupDesc& desc);
b8 ValidateMeshSetupDesc(const GraphicsRuntime::MeshSetupDesc& desc);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

