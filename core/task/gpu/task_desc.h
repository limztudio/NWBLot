// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "types.h"

#include <core/graphics/rhi/device.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CommandListResourceStateHandoff;
struct GpuTaskRecordContext;

// Declaration-owned external state snapshot, captured at accept and filtered through declared resources pre-record.
// Producer storage may retire after the declaration captures its immutable snapshot.
struct GpuTaskExternalStateSource{
    const CommandListResourceStateHandoff* states = nullptr;
    // This predicate is evaluated after queue assignment and never constrains routing.
    // kCount keeps the snapshot applicable to every consumer queue class.
    CommandQueue::Enum applicableConsumerQueueClass = CommandQueue::kCount;
};

// Task payloads live in graph-owned memory; these static thunks let the recorder invoke them with no per-task callable alloc.
// Acceptance and discard stay separate from record: CPU-side effects publish only after queue submission is accepted.
using GpuTaskRecordThunk = b8(*)(const void* payload, CommandList& commandList, const GpuTaskRecordContext& context);
using GpuTaskAcceptedThunk = void(*)(void* payload, const QueueSubmissionToken& token);
using GpuTaskDiscardedThunk = void(*)(void* payload);
using GpuTaskPayloadDestroyThunk = void(*)(GraphicsArena& arena, void* payload)noexcept;


// The task implementation owns its command contract; graph callers do not select a queue.
// Every required capability and at least one capability from each nonempty alternative mask must be supported.
struct GpuTaskCommandRequirements{
    GpuQueueCapability::Mask requiredCapabilities = GpuQueueCapability::None;
    // External frame timing uses the primary graphics submission timeline.
    b8 requiresPrimaryGraphicsQueue = false;
    GpuQueueCapability::Mask alternativeCapabilities = GpuQueueCapability::None;
    // Built-ins preserve an independent hook alternative when composing their native command contract.
    GpuQueueCapability::Mask additionalAlternativeCapabilities = GpuQueueCapability::None;
    // A framework-owned bridge can target an existing external consumer timeline.
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
    // Sharing the preceding compatible packet instead of a new queue submission.
    b8 mergeWithPrevious = false;
    // An explicit immediate successor may keep an accepting packet whole despite direct cross-queue consumers.
    // Those consumers then wait for the full merged packet, tail work and final state included.
    // Queue routing never changes and shared-state tails keep serial recording.
    b8 allowMergeAcrossConsumerFrontier = false;
    // Late recovery/finalization packet: waits latest accepted work on every other queue (submitter derives waits).
    // No incoming deps/state sources/concrete uses; HazardDomains or outgoing edges only.
    b8 joinsAcceptedQueueFrontier = false;
    // True partial-submission recovery packet in the broader recovery/finalization class.
    // Recovery also joins the accepted queue frontier and counts only after native submission acceptance.
    b8 isRecoverySubmission = false;
    // Recording stays serial unless every task in a packet explicitly opts in.
    // A record thunk may run on a worker beside other opt-in packets from the same frontier: no shared CPU mutation, no thread-affine APIs.
    // Submission remains in compiler order.
    b8 allowParallelRecording = false;
    // Deterministic load balancing across physical queues sharing the selected queue class and Vulkan family.
    // Default keeps one-transport-per-class; opt-in callers accept explicit timeline waits when producer and consumer land on different queues.
    b8 allowSameClassQueueRouting = false;
    // Prefer a non-primary queue on equal-cost same-class routing.
    // Broad callers keep the primary transport; upload/offload work can use this for real overlap in an otherwise-empty standalone graph.
    b8 preferNonPrimarySameClassQueue = false;
    // Keeps the physical queue of the last compatible direct dependency.
    // For serial same-class chains (e.g. multi-mip uploads): the first task may offload, later tasks stay with it instead of crossing timelines.
    b8 preserveSameClassQueueWithDirectDependency = false;
    // Extends physical routing to another Vulkan family.
    // Separately opt-in: exclusive uses cross that boundary through compiler-owned release/acquire pairs.
    // Same-class routing still requires allowSameClassQueueRouting; cross-class timing also requires its stronger opt-in below.
    b8 allowCrossFamilySameClassQueueRouting = false;
    // Enables timing-history routing and bounded calibration for this task.
    // Separate from ordinary same-class routing so measurement stays scoped to a proven-safe subset.
    // Another family also needs allowCrossFamilySameClassQueueRouting: dual opt-in before the compiler plans an ownership-transfer route.
    b8 allowTimingFeedbackRouting = false;
    // Extends timing feedback to another queue class already permitted by the command contract.
    // Never relaxes capabilities or timeline constraints; accepted history only replaces an otherwise legal static placement.
    b8 allowCrossClassTimingFeedbackRouting = false;
    // FrontierScored automatic coalescing requires this nonempty domain to match every task already in the preceding packet.
    // Empty domains are never scored-merge eligible; explicit mergeWithPrevious ignores it.
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

// Optional dimensions for immutable timing-history keys.
// Variant distinguishes compatible task implementations; resolutionClass groups a renderer-defined resolution bucket or exact target extent.
// Policy selects graph-owned packet/task query scopes and defaults to None so existing manually timed declarations remain unchanged.
struct GpuTaskTimingMetadata{
    u32 variant = 0u;
    u32 resolutionClass = 0u;
    GpuTaskTimingPolicy::Enum policy = GpuTaskTimingPolicy::None;
};

// Timing identities derive from the first semantic task, so recompiles into a new generation keep one stable key.
[[nodiscard]] inline Expected<Name> GpuTaskPacketTimingScopeName(const Name& firstTaskIdentity){
    return DeriveName(firstTaskIdentity, AStringView(".packet"));
}

// External ownership source per imported range; acceleration structures use one whole-allocation source.
struct GpuGraphInitialOwnerHandoffSourceDesc{
    GpuTaskResourceRange range;
    GpuPhysicalQueueId sourceQueue;
    GpuPhysicalQueueId destinationQueue;
    GpuExternalCompletionId completion;
    // The later graph may wait on a newer token from this exact physical queue, but never an earlier one.
    // This prevents an externally supplied completion from aliasing the source queue while racing the terminal export.
    QueueSubmissionToken minimumCompletionToken;
    const CommandListResourceStateHandoff* stateSource = nullptr;
};

// A resource may be metadata-only in the shadow-graph phase, or hold an imported engine handle. The latter is required before recording.
struct GpuGraphResourceDesc{
    Name identity = s_NameNone;
    AStringView markerLabel;
    // Packet-boundary transitions start here; Unknown needs a transitional handoff at record time.
    // Unspecified inherits the descriptor state; explicit Unknown keeps Vulkan UNDEFINED for the first writer.
    ResourceStates::Mask initialState = ResourceStates::Unknown;
    // Optional required state at completion; Unknown leaves final state under task ownership.
    ResourceStates::Mask externalFinalState = ResourceStates::Unknown;
    // Optional queue receiving exclusive imports after final use; token plus snapshot form the handoff.
    GpuPhysicalQueueId externalFinalReleaseDestinationQueue;
    // Optional pre-first-use owner; exact first-packet matches need no extra sync.
    GpuPhysicalQueueId initialOwnerQueue;
    // Sources cover disjoint imported ranges and are copied into graph-owned immutable storage at declaration.
    const GpuGraphInitialOwnerHandoffSourceDesc* initialOwnerHandoffSources = nullptr;
    usize initialOwnerHandoffSourceCount = 0u;
    // Pure availability is independent of queue-family ownership and native state.
    // The compiler attaches this completion to every first consumer range and lets packet submission elide same-queue waits normally.
    GpuExternalCompletionId initialAvailabilityCompletion;
    // A direct native consumer cannot acquire a graph-exported ownership handoff. Exclusive imports stay in its family.
    GpuPhysicalQueueId directConsumerQueue = {};
    GpuGraphResourceType::Enum type = GpuGraphResourceType::HazardDomain;
    ResourceQueueSharing::Mask queueSharing = ResourceQueueSharing::Exclusive;
    // setInitialState() distinguishes explicit Unknown from an unspecified default inherited from the native descriptor.
    b8 hasExplicitInitialState = false;


public:
    constexpr GpuGraphResourceDesc& setIdentity(const Name& value)noexcept{ identity = value; return *this; }

    constexpr GpuGraphResourceDesc& setMarkerLabel(const AStringView value)noexcept{ markerLabel = value; return *this; }

    constexpr GpuGraphResourceDesc& setType(const GpuGraphResourceType::Enum value)noexcept{ type = value; return *this; }

    constexpr GpuGraphResourceDesc& setInitialState(const ResourceStates::Mask value)noexcept{ initialState = value; hasExplicitInitialState = true; return *this; }

    constexpr GpuGraphResourceDesc& setExternalFinalState(const ResourceStates::Mask value)noexcept{ externalFinalState = value; return *this; }

    constexpr GpuGraphResourceDesc& setExternalFinalReleaseDestinationQueue(const GpuPhysicalQueueId value)noexcept{ externalFinalReleaseDestinationQueue = value; return *this; }

    constexpr GpuGraphResourceDesc& setInitialOwnerQueue(const GpuPhysicalQueueId value)noexcept{ initialOwnerQueue = value; return *this; }

    constexpr GpuGraphResourceDesc& setInitialOwnerHandoffSources(const GpuGraphInitialOwnerHandoffSourceDesc* const values, const usize count)noexcept{
        initialOwnerHandoffSources = values;
        initialOwnerHandoffSourceCount = count;
        return *this;
    }

    constexpr GpuGraphResourceDesc& setQueueSharing(const ResourceQueueSharing::Mask value)noexcept{ queueSharing = value; return *this; }

    constexpr GpuGraphResourceDesc& setInitialAvailabilityCompletion(const GpuExternalCompletionId value)noexcept{ initialAvailabilityCompletion = value; return *this; }

    constexpr GpuGraphResourceDesc& setDirectConsumerQueue(const GpuPhysicalQueueId value)noexcept{ directConsumerQueue = value; return *this; }
};

// A version identifies one semantic value of one exact physical resource range.
// Imported roots enter the graph without an in-graph producer; task-produced versions require a producer declaration during compilation.
struct GpuGraphResourceVersionDesc{
    GpuGraphResourceId resource;
    GpuTaskResourceRange range;
    GpuGraphResourceVersionOrigin::Enum origin = GpuGraphResourceVersionOrigin::Count;


public:
    constexpr GpuGraphResourceVersionDesc& setResource(const GpuGraphResourceId value)noexcept{ resource = value; return *this; }

    constexpr GpuGraphResourceVersionDesc& setRange(const GpuTaskResourceRange& value)noexcept{ range = value; return *this; }

    constexpr GpuGraphResourceVersionDesc& setOrigin(const GpuGraphResourceVersionOrigin::Enum value)noexcept{ origin = value; return *this; }
};

// Resource sets retain graph resource IDs, not backend pointers.
// Members copy into graph-owned storage, freezing bindless declarations before compilation.
struct GpuGraphResourceSetDesc{
    Name identity = s_NameNone;
    AStringView markerLabel;
    const GpuGraphResourceId* members = nullptr;
    usize memberCount = 0u;


public:
    constexpr GpuGraphResourceSetDesc& setIdentity(const Name& value)noexcept{ identity = value; return *this; }

    constexpr GpuGraphResourceSetDesc& setMarkerLabel(const AStringView value)noexcept{ markerLabel = value; return *this; }

    constexpr GpuGraphResourceSetDesc& setMembers(const GpuGraphResourceId* values, const usize count)noexcept{
        members = values;
        memberCount = count;
        return *this;
    }
};

// Pipeline metadata is graph-owned so optional command capture can refer to stable graph IDs instead of backend pointers.
// Typed imports keep the engine pipeline handle; importPipeline is metadata-only for paths that record no native bind.
struct GpuGraphPipelineDesc{
    Name identity = s_NameNone;
    AStringView markerLabel;
    GpuGraphPipelineType::Enum type = GpuGraphPipelineType::Count;


public:
    constexpr GpuGraphPipelineDesc& setIdentity(const Name& value)noexcept{ identity = value; return *this; }

    constexpr GpuGraphPipelineDesc& setMarkerLabel(const AStringView value)noexcept{ markerLabel = value; return *this; }

    constexpr GpuGraphPipelineDesc& setType(const GpuGraphPipelineType::Enum value)noexcept{ type = value; return *this; }
};

// Prior-frame and other out-of-graph completions may retain the authoritative accepted native token directly in graph-owned storage.
// Executable graphs require an authoritative token; empty tokens are for graph analysis only.
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
    GpuTaskTimingMetadata timing;
    const GpuTaskResourceVersionUse* resourceVersionUses = nullptr;
    usize resourceVersionUseCount = 0u;


public:
    constexpr GpuTaskDesc& setIdentity(const Name& value)noexcept{ identity = value; return *this; }

    constexpr GpuTaskDesc& setMarkerLabel(const AStringView value)noexcept{ markerLabel = value; return *this; }

    constexpr GpuTaskDesc& setScheduling(const GpuTaskSchedulingHint& value)noexcept{ scheduling = value; return *this; }

    constexpr GpuTaskDesc& setDependencies(const GpuTaskId* values, const usize count)noexcept{ dependencies = values; dependencyCount = count; return *this; }

    constexpr GpuTaskDesc& setExternalDependencies(const GpuExternalCompletionId* values, const usize count)noexcept{
        externalDependencies = values;
        externalDependencyCount = count;
        return *this;
    }

    constexpr GpuTaskDesc& setExternalStateSources(const GpuTaskExternalStateSource* values, const usize count)noexcept{
        externalStateSources = values;
        externalStateSourceCount = count;
        return *this;
    }

    constexpr GpuTaskDesc& setResourceUses(const GpuTaskResourceUse* values, const usize count)noexcept{
        resourceUses = values;
        resourceUseCount = count;
        return *this;
    }

    constexpr GpuTaskDesc& setResourceSetUses(const GpuTaskResourceSetUse* values, const usize count)noexcept{
        resourceSetUses = values;
        resourceSetUseCount = count;
        return *this;
    }

    constexpr GpuTaskDesc& setTiming(const GpuTaskTimingMetadata& value)noexcept{ timing = value; return *this; }

    constexpr GpuTaskDesc& setResourceVersionUses(const GpuTaskResourceVersionUse* values, const usize count)noexcept{
        resourceVersionUses = values;
        resourceVersionUseCount = count;
        return *this;
    }
};

// Native copies stay task-level ops and record straight to the CommandList. Helpers own region declarations; leave resourceUses empty.
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
    // Optional lifecycle output, written only after the containing packet submission is accepted.
    QueueSubmissionToken* acceptedToken = nullptr;
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
};

// Resolve ops keep both textures until late recording, then record via CommandList after queue assignment.
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
};


// Caller bytes copy into a graph-owned upload blob; the task records it through the CommandList staging path.
// `finalState` describes the task's graph-visible final state after its internal CopyDest write completes.
struct GpuUploadBufferTaskDesc{
    GpuUploadBlobId source;
    GpuGraphResourceId destination;
    u64 destinationOffsetBytes = 0u;
    ResourceStates::Mask finalState = ResourceStates::CopyDest;
    // Lifecycle output. Storage must outlive late recording; written only after submission accepts.
    QueueSubmissionToken* acceptedToken = nullptr;
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


// Clear helpers share copy scheduling/lifecycle and keep the native clear variant in their payload.
// They always write CopyDest state for the target resource.
struct GpuClearBufferTaskDesc{
    GpuGraphResourceId destination;
    u32 clearValue = 0u;
    QueueSubmissionToken* acceptedToken = nullptr;
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

// A typed clear can bracket its command with narrow instrumentation. Hooks stay outside capture/replay; the clear stays the portable POD op.
using GpuClearTextureTaskRecordHook = b8(*)(void* context, CommandList& commandList, const GpuTaskRecordContext& recordContext);
using GpuClearTextureTaskDiscardedHook = void(*)(void* context);

struct GpuClearTextureTaskRecordHooks{
    void* context = nullptr;
    GpuClearTextureTaskRecordHook beforeClear = nullptr;
    GpuClearTextureTaskRecordHook afterClear = nullptr;
    GpuClearTextureTaskDiscardedHook discarded = nullptr;
    // The hook implementation owns any additional commands or external timeline contract.
    GpuTaskCommandRequirements commands = {};
};

struct GpuClearTextureTaskDesc{
    QueueSubmissionToken* acceptedToken = nullptr;
    GpuGraphResourceId destination;
    // Recording-local hooks in the same graph task, so a later consumer cannot split them from the clear.
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
};
static constexpr usize s_GpuClearTextureTaskDescByteSize = 136u;
static_assert(sizeof(GpuClearTextureTaskDesc) == s_GpuClearTextureTaskDescByteSize, "GpuClearTextureTaskDesc should keep its compact runtime layout");

// Rectangular uint clear: same CopyDest/lifecycle contract as the general texture clear, plus work-region bounds.
struct GpuClearTextureRectUIntTaskDesc{
    GpuGraphResourceId destination;
    TextureSubresourceSet subresources = s_AllSubresources;
    Rect rect;
    UIntColor uintValue;
    GpuClearTextureTaskRecordHooks recordHooks;
    QueueSubmissionToken* acceptedToken = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

