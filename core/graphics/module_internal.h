// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"

#include "task_graph/task_desc.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GraphicsModuleDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SetupUploadSameClassRouting{
    GpuPhysicalQueueId primaryQueue;
    bool enabled = false;
    bool crossesQueueFamily = false;
};


using GraphTaskDeclaration = GpuTaskId(*)(void* userData, GpuTaskGraph& graph);


[[nodiscard]] SetupUploadSameClassRouting ResolveSetupUploadSameClassRouting(
    GraphicsBackend::Device& device,
    CommandQueue::Enum uploadQueue,
    usize uploadBytes
)noexcept;
[[nodiscard]] ResourceQueueSharing::Mask ResolveSetupUploadQueueSharing(
    ResourceQueueSharing::Mask requestedSharing,
    CommandQueue::Enum uploadQueue,
    bool crossFamilySameClassRouting = false
)noexcept;
[[nodiscard]] CommandQueue::Enum ResolveSetupUploadQueue(
    GraphicsBackend::Device& device,
    CommandQueue::Enum requestedQueue,
    usize uploadBytes,
    bool hasKnownFinalState,
    bool requiresGraphicsQueue = false
)noexcept;
[[nodiscard]] GpuQueueRequest SetupUploadGraphQueueRequest(
    CommandQueue::Enum uploadQueue,
    bool requiresGraphicsQueue = false
)noexcept;
[[nodiscard]] GpuTaskSchedulingHint SetupUploadGraphScheduling(
    usize byteCount,
    bool sameClassRouting = false,
    bool crossFamilySameClassRouting = false
)noexcept;
[[nodiscard]] ResourceStates::Mask SetupUploadGraphFinalState(ResourceStates::Mask declaredInitialState)noexcept;

[[nodiscard]] bool SubmitGraphOwnedStandaloneTask(
    const Graphics& graphics,
    GraphicsArena& graphArena,
    void* userData,
    GraphTaskDeclaration declareTask,
    QueueSubmissionToken& outSubmissionToken,
    GpuPhysicalQueueId requiredTerminalQueue = {},
    Alloc::ThreadPool* readyFrontierWorkerPool = nullptr
);
[[nodiscard]] bool SubmitGraphOwnedSetupUpload(
    const Graphics& graphics,
    GraphicsArena& graphArena,
    ResourceQueueSharing::Mask queueSharing,
    CommandQueue::Enum uploadQueue,
    void* userData,
    GraphTaskDeclaration declareTask,
    QueueSubmissionToken& outUploadToken,
    bool bridgePrimaryUploadQueue = false,
    GpuPhysicalQueueId requiredTerminalQueue = {}
);
[[nodiscard]] bool SubmitGraphOwnedFrameTimingReset(
    const Graphics& graphics,
    GraphicsArena& graphArena,
    GpuTimingRecorder& timing
);

bool ValidateBufferSetupUpload(const Graphics::BufferSetupDesc& desc);
bool ValidateTextureSetupUpload(const Graphics::TextureSetupDesc& desc);
bool ValidateMeshSetupDesc(const Graphics::MeshSetupDesc& desc);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

