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
    bool enabled = false;
    bool crossesQueueFamily = false;
};


using GraphTaskDeclaration = GpuTaskId(*)(void* userData, GpuTaskGraph& graph);


[[nodiscard]] SetupUploadSameClassRouting ResolveSetupUploadSameClassRouting(
    GraphicsBackend::Device& device,
    CommandQueue::Enum consumerQueue,
    usize uploadBytes
)noexcept;
[[nodiscard]] ResourceQueueSharing::Mask ResolveSetupUploadConsumerSharing(
    ResourceQueueSharing::Mask requestedSharing,
    CommandQueue::Enum consumerQueue,
    bool crossFamilySameClassRouting = false
)noexcept;
[[nodiscard]] CommandQueue::Enum ResolveSetupUploadConsumerQueue(
    GraphicsBackend::Device& device,
    CommandQueue::Enum requestedConsumerQueue,
    usize uploadBytes,
    bool hasKnownFinalState,
    bool requiresGraphicsConsumerQueue = false
)noexcept;
[[nodiscard]] GpuTaskSchedulingHint SetupUploadGraphScheduling(
    usize byteCount,
    bool sameClassRouting = false,
    bool crossFamilySameClassRouting = false
)noexcept;
[[nodiscard]] ResourceStates::Mask SetupUploadGraphFinalState(ResourceStates::Mask declaredInitialState)noexcept;

[[nodiscard]] bool SubmitGraphOwnedStandaloneTask(
    const GraphicsRuntime& graphics,
    GraphicsArena& graphArena,
    void* userData,
    GraphTaskDeclaration declareTask,
    QueueSubmissionToken& outSubmissionToken,
    GpuPhysicalQueueId requiredTerminalQueue = {},
    CpuTaskScheduler* readyFrontierScheduler = nullptr,
    GpuTimingRecorder* timingRecorder = nullptr
);
[[nodiscard]] bool SubmitGraphOwnedSetupUpload(
    const GraphicsRuntime& graphics,
    GraphicsArena& graphArena,
    ResourceQueueSharing::Mask queueSharing,
    CommandQueue::Enum consumerQueue,
    void* userData,
    GraphTaskDeclaration declareTask,
    QueueSubmissionToken& outUploadToken,
    GpuPhysicalQueueId requiredTerminalQueue = {}
);
[[nodiscard]] bool SubmitGraphOwnedFrameTimingReset(
    const GraphicsRuntime& graphics,
    GraphicsArena& graphArena,
    GpuTimingRecorder& timing
);

bool ValidateBufferSetupUpload(const GraphicsRuntime::BufferSetupDesc& desc);
bool ValidateTextureSetupUpload(const GraphicsRuntime::TextureSetupDesc& desc);
bool ValidateMeshSetupDesc(const GraphicsRuntime::MeshSetupDesc& desc);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

