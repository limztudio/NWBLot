// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "types.h"

#include <core/graphics/rhi/device.h>
#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CommandListResourceStateHandoff;
struct GpuTaskRecordContext;

struct GpuTaskExternalStateSource{
    const CommandListResourceStateHandoff* states = nullptr;
    CommandQueue::Enum applicableConsumerQueueClass = CommandQueue::kCount;

    [[nodiscard]] b8 hasState()const noexcept{ return states != nullptr; }
    void setState(const CommandListResourceStateHandoff& source)noexcept{ states = &source; }
    void setState(const CommandListResourceStateHandoff&&) = delete;
};

using GpuTaskRecordThunk = b8(*)(const void* payload, CommandList& commandList, const GpuTaskRecordContext& context);
using GpuTaskAcceptedThunk = void(*)(void* payload, const QueueSubmissionToken& token);
using GpuTaskDiscardedThunk = void(*)(void* payload);
using GpuTaskPayloadDestroyThunk = void(*)(GraphicsArena& arena, void* payload)noexcept;

struct GpuTaskCommandRequirements{
    GpuQueueCapability::Mask requiredCapabilities = GpuQueueCapability::None;
    b8 requiresPrimaryGraphicsQueue = false;
    GpuQueueCapability::Mask alternativeCapabilities = GpuQueueCapability::None;
    GpuQueueCapability::Mask additionalAlternativeCapabilities = GpuQueueCapability::None;
    GpuPhysicalQueueId externalQueue = {};


public:
    [[nodiscard]] constexpr GpuQueueCapability::Mask allowedCapabilities()const noexcept{
        return static_cast<GpuQueueCapability::Mask>(static_cast<u8>(requiredCapabilities) | static_cast<u8>(alternativeCapabilities) | static_cast<u8>(additionalAlternativeCapabilities));
    }
};

struct GpuTaskSchedulingHint{
    GpuTaskCostHint::Enum cost = GpuTaskCostHint::Medium;
    b8 overlapPreferred = true;
    b8 avoidQueueCrossing = false;
    b8 forceSubmissionBoundary = false;
    b8 allowPacketMerge = true;
    b8 mergeWithPrevious = false;
    b8 allowMergeAcrossConsumerFrontier = false;
    b8 joinsAcceptedQueueFrontier = false;
    b8 isRecoverySubmission = false;
    b8 allowParallelRecording = false;
    b8 allowSameClassQueueRouting = false;
    b8 preferNonPrimarySameClassQueue = false;
    b8 preserveSameClassQueueWithDirectDependency = false;
    b8 allowCrossFamilySameClassQueueRouting = false;
    b8 allowTimingFeedbackRouting = false;
    b8 allowCrossClassTimingFeedbackRouting = false;
    Name frontierScoredMergeDomain = {};
};

namespace GpuTaskTimingPolicy{
    enum Enum : u8{
        None,
        PacketOnly,
        Task,

        Count,
    };
};

struct GpuTaskTimingMetadata{
    u32 variant = 0u;
    u32 resolutionClass = 0u;
    GpuTaskTimingPolicy::Enum policy = GpuTaskTimingPolicy::None;
};

[[nodiscard]] inline Expected<Name> GpuTaskPacketTimingScopeName(const Name& firstTaskIdentity){
    return DeriveName(firstTaskIdentity, AStringView(".packet"));
}

struct GpuGraphInitialOwnerHandoffSourceDesc{
    GpuTaskResourceRange range;
    GpuPhysicalQueueId sourceQueue;
    GpuPhysicalQueueId destinationQueue;
    GpuExternalCompletionId completion;
    QueueSubmissionToken minimumCompletionToken;
    const CommandListResourceStateHandoff* stateSource = nullptr;
    void setStateSource(const CommandListResourceStateHandoff& source)noexcept{ stateSource = &source; }
    void setStateSource(const CommandListResourceStateHandoff&&) = delete;
};

struct GpuGraphResourceDesc{
    Name identity = s_NameNone;
    AStringView markerLabel;
    ResourceStates::Mask initialState = ResourceStates::Unknown;
    ResourceStates::Mask externalFinalState = ResourceStates::Unknown;
    GpuPhysicalQueueId externalFinalReleaseDestinationQueue;
    GpuPhysicalQueueId initialOwnerQueue;
    const GpuGraphInitialOwnerHandoffSourceDesc* initialOwnerHandoffSources = nullptr;
    usize initialOwnerHandoffSourceCount = 0u;
    GpuExternalCompletionId initialAvailabilityCompletion;
    GpuPhysicalQueueId directConsumerQueue = {};
    GpuGraphResourceType::Enum type = GpuGraphResourceType::HazardDomain;
    ResourceQueueSharing::Mask queueSharing = ResourceQueueSharing::Exclusive;
    b8 hasExplicitInitialState = false;


public:
    constexpr GpuGraphResourceDesc& setIdentity(const Name& value)noexcept{ identity = value; return *this; }

    constexpr GpuGraphResourceDesc& setMarkerLabel(const AStringView value)noexcept{ markerLabel = value; return *this; }

    constexpr GpuGraphResourceDesc& setType(const GpuGraphResourceType::Enum value)noexcept{ type = value; return *this; }

    constexpr GpuGraphResourceDesc& setInitialState(const ResourceStates::Mask value)noexcept{ initialState = value; hasExplicitInitialState = true; return *this; }

    constexpr GpuGraphResourceDesc& setExternalFinalState(const ResourceStates::Mask value)noexcept{ externalFinalState = value; return *this; }

    constexpr GpuGraphResourceDesc& setExternalFinalReleaseDestinationQueue(const GpuPhysicalQueueId value)noexcept{ externalFinalReleaseDestinationQueue = value; return *this; }

    constexpr GpuGraphResourceDesc& setInitialOwnerQueue(const GpuPhysicalQueueId value)noexcept{ initialOwnerQueue = value; return *this; }

    constexpr GpuGraphResourceDesc& setInitialOwnerHandoffSources(const Span<const GpuGraphInitialOwnerHandoffSourceDesc> values)noexcept{
        initialOwnerHandoffSources = values.data();
        initialOwnerHandoffSourceCount = values.size();
        return *this;
    }

    constexpr GpuGraphResourceDesc& setQueueSharing(const ResourceQueueSharing::Mask value)noexcept{ queueSharing = value; return *this; }

    constexpr GpuGraphResourceDesc& setInitialAvailabilityCompletion(const GpuExternalCompletionId value)noexcept{ initialAvailabilityCompletion = value; return *this; }

    constexpr GpuGraphResourceDesc& setDirectConsumerQueue(const GpuPhysicalQueueId value)noexcept{ directConsumerQueue = value; return *this; }
};

struct GpuGraphResourceVersionDesc{
    GpuGraphResourceId resource;
    GpuTaskResourceRange range;
    GpuGraphResourceVersionOrigin::Enum origin = GpuGraphResourceVersionOrigin::Count;


public:
    constexpr GpuGraphResourceVersionDesc& setResource(const GpuGraphResourceId value)noexcept{ resource = value; return *this; }

    constexpr GpuGraphResourceVersionDesc& setRange(const GpuTaskResourceRange& value)noexcept{ range = value; return *this; }

    constexpr GpuGraphResourceVersionDesc& setOrigin(const GpuGraphResourceVersionOrigin::Enum value)noexcept{ origin = value; return *this; }
};

struct GpuGraphResourceSetDesc{
    Name identity = s_NameNone;
    AStringView markerLabel;
    const GpuGraphResourceId* members = nullptr;
    usize memberCount = 0u;


public:
    constexpr GpuGraphResourceSetDesc& setIdentity(const Name& value)noexcept{ identity = value; return *this; }

    constexpr GpuGraphResourceSetDesc& setMarkerLabel(const AStringView value)noexcept{ markerLabel = value; return *this; }

    constexpr GpuGraphResourceSetDesc& setMembers(const Span<const GpuGraphResourceId> values)noexcept{
        members = values.data();
        memberCount = values.size();
        return *this;
    }
};

struct GpuGraphPipelineDesc{
    Name identity = s_NameNone;
    AStringView markerLabel;
    GpuGraphPipelineType::Enum type = GpuGraphPipelineType::Count;


public:
    constexpr GpuGraphPipelineDesc& setIdentity(const Name& value)noexcept{ identity = value; return *this; }

    constexpr GpuGraphPipelineDesc& setMarkerLabel(const AStringView value)noexcept{ markerLabel = value; return *this; }

    constexpr GpuGraphPipelineDesc& setType(const GpuGraphPipelineType::Enum value)noexcept{ type = value; return *this; }
};

struct GpuExternalCompletionDesc{
    Name identity = s_NameNone;
    AStringView markerLabel;
    QueueSubmissionToken token;


public:
    constexpr GpuExternalCompletionDesc& setIdentity(const Name& value)noexcept{ identity = value; return *this; }

    constexpr GpuExternalCompletionDesc& setMarkerLabel(const AStringView value)noexcept{ markerLabel = value; return *this; }

    constexpr GpuExternalCompletionDesc& setToken(const QueueSubmissionToken& value)noexcept{ token = value; return *this; }
};

struct GpuTaskDesc{
    Name identity = s_NameNone;
    AStringView markerLabel;
    GpuTaskSchedulingHint scheduling;
    const GpuTaskId* dependencies = nullptr;
    usize dependencyCount = 0u;
    const GpuExternalCompletionId* externalDependencies = nullptr;
    usize externalDependencyCount = 0u;
    const GpuTaskExternalStateSource* externalStateSources = nullptr;
    usize externalStateSourceCount = 0u;
    const GpuTaskResourceUse* resourceUses = nullptr;
    usize resourceUseCount = 0u;
    const GpuTaskResourceSetUse* resourceSetUses = nullptr;
    usize resourceSetUseCount = 0u;
    const GpuTaskResourceVersionUse* resourceVersionUses = nullptr;
    usize resourceVersionUseCount = 0u;
    GpuTaskTimingMetadata timing;


public:
    constexpr GpuTaskDesc& setIdentity(const Name& value)noexcept{ identity = value; return *this; }

    constexpr GpuTaskDesc& setMarkerLabel(const AStringView value)noexcept{ markerLabel = value; return *this; }

    constexpr GpuTaskDesc& setScheduling(const GpuTaskSchedulingHint& value)noexcept{ scheduling = value; return *this; }

    constexpr GpuTaskDesc& setDependencies(const Span<const GpuTaskId> values)noexcept{
        dependencies = values.data();
        dependencyCount = values.size();
        return *this;
    }

    constexpr GpuTaskDesc& setDependency(const GpuTaskId& value)noexcept{ return setDependencies({ &value, 1u }); }
    GpuTaskDesc& setDependency(const GpuTaskId&&) = delete;

    constexpr GpuTaskDesc& setExternalDependencies(const Span<const GpuExternalCompletionId> values)noexcept{
        externalDependencies = values.data();
        externalDependencyCount = values.size();
        return *this;
    }

    constexpr GpuTaskDesc& setExternalStateSources(const Span<const GpuTaskExternalStateSource> values)noexcept{
        externalStateSources = values.data();
        externalStateSourceCount = values.size();
        return *this;
    }

    constexpr GpuTaskDesc& setResourceUses(const Span<const GpuTaskResourceUse> values)noexcept{
        resourceUses = values.data();
        resourceUseCount = values.size();
        return *this;
    }

    constexpr GpuTaskDesc& setResourceSetUses(const Span<const GpuTaskResourceSetUse> values)noexcept{
        resourceSetUses = values.data();
        resourceSetUseCount = values.size();
        return *this;
    }

    constexpr GpuTaskDesc& setTiming(const GpuTaskTimingMetadata& value)noexcept{ timing = value; return *this; }

    constexpr GpuTaskDesc& setResourceVersionUses(const Span<const GpuTaskResourceVersionUse> values)noexcept{
        resourceVersionUses = values.data();
        resourceVersionUseCount = values.size();
        return *this;
    }
};

struct GpuCopyBufferTaskRegion{
    GpuGraphResourceId source;
    u64 sourceOffsetBytes = 0u;
    GpuGraphResourceId destination;
    u64 destinationOffsetBytes = 0u;
    u64 dataSizeBytes = 0u;
};

struct GpuCopyBufferTaskDesc{
    const GpuCopyBufferTaskRegion* regions = nullptr;
    usize regionCount = 0u;
    QueueSubmissionToken* acceptedToken = nullptr;

    void publishAcceptedToken(QueueSubmissionToken& token)noexcept{ acceptedToken = &token; }
    void setRegions(const Span<const GpuCopyBufferTaskRegion> values)noexcept{ regions = values.data(); regionCount = values.size(); }
};

struct GpuCopyTextureTaskRegion{
    GpuGraphResourceId source;
    TextureSlice sourceSlice;
    GpuGraphResourceId destination;
    TextureSlice destinationSlice;
};

struct GpuCopyTextureTaskDesc{
    const GpuCopyTextureTaskRegion* regions = nullptr;
    usize regionCount = 0u;
    QueueSubmissionToken* acceptedToken = nullptr;

    void publishAcceptedToken(QueueSubmissionToken& token)noexcept{ acceptedToken = &token; }
};

struct GpuResolveTextureTaskRegion{
    GpuGraphResourceId source;
    TextureSubresourceSet sourceSubresources = s_AllSubresources;
    GpuGraphResourceId destination;
    TextureSubresourceSet destinationSubresources = s_AllSubresources;
};

struct GpuResolveTextureTaskDesc{
    const GpuResolveTextureTaskRegion* regions = nullptr;
    usize regionCount = 0u;
    QueueSubmissionToken* acceptedToken = nullptr;

    void publishAcceptedToken(QueueSubmissionToken& token)noexcept{ acceptedToken = &token; }
};

struct GpuUploadBufferTaskDesc{
    GpuUploadBlobId source;
    GpuGraphResourceId destination;
    u64 destinationOffsetBytes = 0u;
    ResourceStates::Mask finalState = ResourceStates::CopyDest;
    QueueSubmissionToken* acceptedToken = nullptr;

    void publishAcceptedToken(QueueSubmissionToken& token)noexcept{ acceptedToken = &token; }
};

struct GpuUploadTextureTaskDesc{
    GpuUploadBlobId source;
    GpuGraphResourceId destination;
    u32 arraySlice = 0u;
    u32 mipLevel = 0u;
    usize rowPitch = 0u;
    usize depthPitch = 0u;
    ResourceStates::Mask finalState = ResourceStates::CopyDest;
    TextureUploadAspect::Enum aspect = TextureUploadAspect::Automatic;
    QueueSubmissionToken* acceptedToken = nullptr;
};

struct GpuClearBufferTaskDesc{
    GpuGraphResourceId destination;
    u32 clearValue = 0u;
    QueueSubmissionToken* acceptedToken = nullptr;

    void publishAcceptedToken(QueueSubmissionToken& token)noexcept{ acceptedToken = &token; }
};

namespace GpuClearTextureTaskValueType{
    enum Enum : u8{
        Float,
        UInt,
        Int,
        DepthStencil,

        Count,
    };
};

using GpuClearTextureTaskRecordHook = b8(*)(void* context, CommandList& commandList, const GpuTaskRecordContext& recordContext);
using GpuClearTextureTaskDiscardedHook = void(*)(void* context);

struct GpuClearTextureTaskRecordHooks{
    void* context = nullptr;
    GpuClearTextureTaskRecordHook beforeClear = nullptr;
    GpuClearTextureTaskRecordHook afterClear = nullptr;
    GpuClearTextureTaskDiscardedHook discarded = nullptr;
    GpuTaskCommandRequirements commands = {};
};

template<typename State>
[[nodiscard]] GpuClearTextureTaskRecordHooks MakeGpuClearTextureTaskRecordHooks(State& state, const b8 beforeClear, const b8 afterClear)noexcept{
    GpuClearTextureTaskRecordHooks hooks;
    hooks.context = &state;
    if(beforeClear){
        hooks.beforeClear = [](void* rawState, CommandList& commands, const GpuTaskRecordContext& context){
            return rawState && static_cast<State*>(rawState)->beforeClear(commands, context);
        };
    }
    if(afterClear){
        hooks.afterClear = [](void* rawState, CommandList& commands, const GpuTaskRecordContext& context){
            return rawState && static_cast<State*>(rawState)->afterClear(commands, context);
        };
    }
    hooks.discarded = [](void* rawState){
        if(rawState)
            static_cast<State*>(rawState)->discarded();
    };
    return hooks;
}

struct GpuClearTextureTaskDesc{
    QueueSubmissionToken* acceptedToken = nullptr;
    GpuGraphResourceId destination;
    GpuClearTextureTaskRecordHooks recordHooks{};
    f32 depthValue = 1.f;
    TextureSubresourceSet subresources = s_AllSubresources;
    GpuClearTextureTaskValueType::Enum valueType = GpuClearTextureTaskValueType::UInt;
    u8 stencilValue = 0u;
    b8 clearDepth = false;
    b8 clearStencil = false;
    Color floatValue{};
    UIntColor uintValue{};
    IntColor intValue{};

    void publishAcceptedToken(QueueSubmissionToken& token)noexcept{ acceptedToken = &token; }
};
static constexpr usize s_GpuClearTextureTaskDescByteSize = 136u;
static_assert(sizeof(GpuClearTextureTaskDesc) == s_GpuClearTextureTaskDescByteSize, "GpuClearTextureTaskDesc should keep its compact runtime layout");

struct GpuClearTextureRectUIntTaskDesc{
    GpuGraphResourceId destination;
    TextureSubresourceSet subresources = s_AllSubresources;
    Rect rect;
    UIntColor uintValue;
    GpuClearTextureTaskRecordHooks recordHooks;
    QueueSubmissionToken* acceptedToken = nullptr;

    void publishAcceptedToken(QueueSubmissionToken& token)noexcept{ acceptedToken = &token; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

