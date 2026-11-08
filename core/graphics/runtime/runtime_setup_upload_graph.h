// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "runtime_internal.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GraphicsModuleDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SetupUploadSameClassRouting{
    GpuPhysicalQueueId primaryQueue;
    b8 enabled = false;
    b8 crossesQueueFamily = false;
};

class SetupUploadGraph final{
public:
    static constexpr usize s_LargeMinimumBytes = 1024u * 1024u;


public:
    [[nodiscard]] static SetupUploadSameClassRouting resolveSameClassRouting(GraphicsBackend::Device& device, CommandQueue::Enum consumerQueue, usize uploadBytes)noexcept;
    [[nodiscard]] static ResourceQueueSharing::Mask resolveConsumerSharing(ResourceQueueSharing::Mask requestedSharing, CommandQueue::Enum consumerQueue, b8 crossFamilySameClassRouting = false)noexcept;
    [[nodiscard]] static CommandQueue::Enum resolveConsumerQueue(GraphicsBackend::Device& device, CommandQueue::Enum requestedConsumerQueue, usize uploadBytes, b8 hasKnownFinalState)noexcept;
    [[nodiscard]] static GpuTaskSchedulingHint scheduling(usize byteCount, b8 sameClassRouting = false, b8 crossFamilySameClassRouting = false)noexcept;
    [[nodiscard]] static ResourceStates::Mask finalState(ResourceStates::Mask declaredInitialState)noexcept;
    [[nodiscard]] static b8 submit(
        const GraphicsRuntime& graphics,
        ResourceQueueSharing::Mask queueSharing,
        CommandQueue::Enum consumerQueue,
        NotNull<GraphTaskDeclaration> declareTask,
        GpuPhysicalQueueId requiredTerminalQueue,
        void* userData,
        StandaloneGraphWorkspacePool& workspaces,
        QueueSubmissionToken& outUploadToken
    );
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


