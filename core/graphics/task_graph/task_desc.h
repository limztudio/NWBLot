// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "types.h"

#include <core/graphics/rhi/device.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuCompiledGraph;
class CommandListResourceStateHandoff;
struct GpuTaskRecordContext;

// A declaration-owned native state snapshot from work outside the graph's ordinary internal packet edges. The graph
// captures it in graph-owned storage when it accepts the task declaration, then filters that immutable snapshot
// through the task's declared resources before opening the packet command list. The producer may release its
// original handoff after task creation. This is the migration path for accepted cross-frame state until every
// producer becomes an in-graph packet, and avoids renderer-owned packet-specific record overrides.
struct GpuTaskExternalStateSource{
    const CommandListResourceStateHandoff* states = nullptr;
};

// Typed task payloads are stored in graph-owned memory, while these static thunks let the packet recorder invoke
// them without allocating a type-erased callable per task.  Acceptance and discard are deliberately separate from
// record: a task may only publish CPU-side effects once the containing queue submission is accepted.
using GpuTaskRecordThunk = bool(*)(const void* payload, CommandList& commandList, const GpuTaskRecordContext& context);
using GpuTaskAcceptedThunk = void(*)(void* payload, const QueueSubmissionToken& token);
using GpuTaskDiscardedThunk = void(*)(void* payload);
using GpuTaskPayloadDestroyThunk = void(*)(GraphicsArena& arena, void* payload)noexcept;


struct GpuQueueRequest{
    GpuQueueCapability::Mask requiredCapabilities = GpuQueueCapability::None;
    GpuQueuePreference::Enum preferredQueue = GpuQueuePreference::Any;
    bool allowFallback = true;
    bool compilerMayOverridePreference = true;
};

struct GpuTaskSchedulingHint{
    GpuTaskCostHint::Enum cost = GpuTaskCostHint::Medium;
    bool overlapPreferred = true;
    bool avoidQueueCrossing = false;
    bool forceSubmissionBoundary = false;
    bool allowPacketMerge = true;
    // Merging remains opt-in while imported recording bridges are retired incrementally.  When set, this task may
    // share the immediately preceding compatible packet instead of creating a new queue submission.
    bool mergeWithPrevious = false;
    // An explicit immediate successor may keep an accepting packet whole even when that task has direct cross-queue
    // consumers. Those consumers then wait for the complete merged packet, including packet-local tail work and its
    // final state. This never changes queue routing and remains opt-in; shared-state tails keep serial recording.
    bool allowMergeAcrossConsumerFrontier = false;
    // A late recovery/finalization packet must wait for the latest accepted work on every other physical queue.
    // The compiler preserves it as a separate packet and the submitter derives those waits from the graph-owned
    // submission transaction; callers do not assemble a queue-class token ladder.
    bool joinsAcceptedQueueFrontier = false;
    // Recording stays serial unless every task in a packet explicitly opts in.  An opt-in record thunk may run on a
    // worker concurrently with other opt-in packets from the same compiler-derived ready frontier, so it must not
    // mutate shared CPU state or rely on thread-affine APIs. Submission remains in compiler order.
    bool allowParallelRecording = false;
    // Opts this task into deterministic load balancing across physical queues that share the selected queue class
    // and Vulkan family. The default retains the current one-transport-per-class behavior; callers that opt in
    // accept explicit timeline waits when a producer and consumer land on different queues in that family.
    bool allowSameClassQueueRouting = false;
    // When same-class routing finds an equal-cost alternative, prefer a non-primary physical queue. This remains
    // opt-in because broad CommandQueue callers retain the primary transport by default; upload/offload work can
    // use it to create real overlap even when it is the first task in an otherwise-empty standalone graph.
    bool preferNonPrimarySameClassQueue = false;
    // Retains the exact physical queue selected for the last compatible direct dependency. This is for explicitly
    // serial same-class chains (for example, multi-mip uploads): the first task may offload, while later tasks stay
    // with it instead of creating avoidable timeline/ownership crossings at every stage.
    bool preserveSameClassQueueWithDirectDependency = false;
    // Extends the same-class opt-in to a physical queue from another Vulkan family. This remains separately opt-in
    // because exclusive resource uses cross that boundary through compiler-owned release/acquire ownership pairs.
    // It has no effect unless allowSameClassQueueRouting is also set.
    bool allowCrossFamilySameClassQueueRouting = false;
    // Enables timing-history routing and bounded calibration for this task. This is separate from ordinary
    // same-class routing so an application can keep historical measurement scoped to a small, proven-safe subset.
    // Timing feedback itself remains same-family only and never manufactures an ownership-transfer route.
    bool allowTimingFeedbackRouting = false;
};

// Optional dimensions for immutable timing-history keys. Variant distinguishes compatible task implementations;
// resolutionClass groups a renderer-defined resolution bucket or exact target extent. Both default to zero for
// existing declarations that intentionally use one stable timing dimension.
struct GpuTaskTimingMetadata{
    u32 variant = 0u;
    u32 resolutionClass = 0u;
};

// One immutable external ownership source for an imported texture range. Multiple sources let a later graph consume
// a prior graph's disjoint terminal texture exports without collapsing them into one fake physical owner. Every
// source supplies its exact releasing queue, the fixed first-consumer queue, a graph-local external completion, and
// the native state snapshot that recorded the release. Buffer and AS ownership remain whole-allocation and use the
// single-owner fields below instead.
struct GpuGraphInitialOwnerHandoffSourceDesc{
    GpuTaskResourceRange range;
    GpuPhysicalQueueId sourceQueue;
    GpuPhysicalQueueId destinationQueue;
    GpuExternalCompletionId completion;
    // The later graph may wait on a newer token from this exact physical queue, but never an earlier one. This
    // prevents an externally supplied completion from aliasing the source queue while racing the terminal export.
    QueueSubmissionToken minimumCompletionToken;
    const CommandListResourceStateHandoff* stateSource = nullptr;
};

// A resource may be metadata-only during the shadow-graph phase, or may retain an imported engine handle through
// one of GpuTaskGraph's typed import overloads. The latter is the required path before graph recording is enabled.
struct GpuGraphResourceDesc{
    Name identity = NAME_NONE;
    AStringView markerLabel;
    GpuGraphResourceType::Enum type = GpuGraphResourceType::HazardDomain;
    // Compiler-generated packet-boundary transitions begin from this state. Unknown remains valid only while a
    // transitional CommandListResourceStateHandoff supplies the authoritative imported state at recording time.
    // Typed imports inherit their resource descriptor state only when this field was left unspecified; an explicit
    // Unknown preserves Vulkan's fresh-resource UNDEFINED origin for the graph's first writer.
    ResourceStates::Mask initialState = ResourceStates::Unknown;
    // Optional required state when graph work completes. The compiler applies this to every terminal range the
    // graph declared for an imported texture, buffer, or acceleration structure and publishes it in the accepted
    // packet's native state snapshot. Unknown leaves the resource's final state under ordinary task ownership.
    ResourceStates::Mask externalFinalState = ResourceStates::Unknown;
    // Optional physical queue that receives an exclusive imported texture, buffer, or acceleration structure after
    // its final graph use. The compiler emits the terminal state export first, then a release to this exact destination. The accepted
    // terminal packet token and recorded state snapshot form the external handoff consumed by subsequent native
    // work or a later graph import.
    GpuPhysicalQueueId externalFinalReleaseDestinationQueue;
    // Optional owner of an exclusive imported texture, buffer, or acceleration structure before its first graph
    // use. An exact first-packet match needs no extra synchronization because submission order on one physical queue is sufficient.
    GpuPhysicalQueueId initialOwnerQueue;
    // A different first packet is permitted only when an already-recorded external producer released ownership to
    // this exact physical queue, exports the state snapshot below, and supplies the completion node imported into
    // this graph before the resource. The graph captures the source snapshot at declaration, so the producer may
    // release its original handoff before late packet recording. The compiler attaches that completion to the first
    // consumer packet.
    GpuPhysicalQueueId initialOwnerReleaseDestinationQueue;
    GpuExternalCompletionId initialOwnerCompletion;
    // The bound completion may advance on the same source queue, but it must never precede this release token.
    // This makes the legacy whole-resource handoff as race-safe as the texture multi-source form above.
    QueueSubmissionToken initialOwnerMinimumCompletionToken;
    const CommandListResourceStateHandoff* initialOwnerStateSource = nullptr;
    // Texture-only multi-producer companion to the single-owner fields above. Sources must be non-overlapping and
    // must not be mixed with those legacy fields; the graph copies every state source at declaration time.
    const GpuGraphInitialOwnerHandoffSourceDesc* initialOwnerHandoffSources = nullptr;
    usize initialOwnerHandoffSourceCount = 0u;
    ResourceQueueSharing::Mask queueSharing = ResourceQueueSharing::Exclusive;
    // Appended so positional aggregate initializers retain their existing field layout. Prefer setInitialState()
    // whenever Unknown is intended as an explicit physical initial state rather than an unspecified default.
    bool hasExplicitInitialState = false;

    constexpr GpuGraphResourceDesc& setIdentity(const Name& value){ identity = value; return *this; }
    constexpr GpuGraphResourceDesc& setMarkerLabel(const AStringView value){ markerLabel = value; return *this; }
    constexpr GpuGraphResourceDesc& setType(const GpuGraphResourceType::Enum value){ type = value; return *this; }
    constexpr GpuGraphResourceDesc& setInitialState(const ResourceStates::Mask value){ initialState = value; hasExplicitInitialState = true; return *this; }
    constexpr GpuGraphResourceDesc& setExternalFinalState(const ResourceStates::Mask value){ externalFinalState = value; return *this; }
    constexpr GpuGraphResourceDesc& setExternalFinalReleaseDestinationQueue(const GpuPhysicalQueueId value){ externalFinalReleaseDestinationQueue = value; return *this; }
    constexpr GpuGraphResourceDesc& setInitialOwnerQueue(const GpuPhysicalQueueId value){ initialOwnerQueue = value; return *this; }
    constexpr GpuGraphResourceDesc& setInitialOwnerReleaseDestinationQueue(const GpuPhysicalQueueId value){ initialOwnerReleaseDestinationQueue = value; return *this; }
    constexpr GpuGraphResourceDesc& setInitialOwnerCompletion(const GpuExternalCompletionId value){ initialOwnerCompletion = value; return *this; }
    constexpr GpuGraphResourceDesc& setInitialOwnerMinimumCompletionToken(const QueueSubmissionToken& value){ initialOwnerMinimumCompletionToken = value; return *this; }
    constexpr GpuGraphResourceDesc& setInitialOwnerStateSource(const CommandListResourceStateHandoff* const value){ initialOwnerStateSource = value; return *this; }
    constexpr GpuGraphResourceDesc& setInitialOwnerHandoffSources(
        const GpuGraphInitialOwnerHandoffSourceDesc* const values,
        const usize count
    ){
        initialOwnerHandoffSources = values;
        initialOwnerHandoffSourceCount = count;
        return *this;
    }
    constexpr GpuGraphResourceDesc& setQueueSharing(const ResourceQueueSharing::Mask value){ queueSharing = value; return *this; }
};

// Resource sets retain graph resource IDs, not backend pointers. Their member list is copied into graph-owned
// storage, which makes dynamic enumerable bindless declarations immutable before compilation and native recording.
struct GpuGraphResourceSetDesc{
    Name identity = NAME_NONE;
    AStringView markerLabel;
    const GpuGraphResourceId* members = nullptr;
    usize memberCount = 0u;

    constexpr GpuGraphResourceSetDesc& setIdentity(const Name& value){ identity = value; return *this; }
    constexpr GpuGraphResourceSetDesc& setMarkerLabel(const AStringView value){ markerLabel = value; return *this; }
    constexpr GpuGraphResourceSetDesc& setMembers(const GpuGraphResourceId* values, const usize count){
        members = values;
        memberCount = count;
        return *this;
    }
};

// Pipeline metadata is graph-owned so optional command capture can refer to stable graph IDs instead of backend
// pointers. Typed import overloads retain the matching engine pipeline handle; importPipeline is metadata-only for
// analysis/tooling paths that do not record a native pipeline bind yet.
struct GpuGraphPipelineDesc{
    Name identity = NAME_NONE;
    AStringView markerLabel;
    GpuGraphPipelineType::Enum type = GpuGraphPipelineType::kCount;

    constexpr GpuGraphPipelineDesc& setIdentity(const Name& value){ identity = value; return *this; }
    constexpr GpuGraphPipelineDesc& setMarkerLabel(const AStringView value){ markerLabel = value; return *this; }
    constexpr GpuGraphPipelineDesc& setType(const GpuGraphPipelineType::Enum value){ type = value; return *this; }
};

// Phase 1 represents prior-frame and other out-of-graph completions as named metadata nodes. It deliberately does
// not make QueueSubmissionToken authoritative until the physical-queue and device-generation contracts arrive.
struct GpuExternalCompletionDesc{
    Name identity = NAME_NONE;
    AStringView markerLabel;

    constexpr GpuExternalCompletionDesc& setIdentity(const Name& value){ identity = value; return *this; }
    constexpr GpuExternalCompletionDesc& setMarkerLabel(const AStringView value){ markerLabel = value; return *this; }
};

struct GpuTaskDesc{
    Name identity = NAME_NONE;
    AStringView markerLabel;
    GpuQueueRequest queue;
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
    // Appended so positional aggregate initializers retain their existing field layout.
    GpuTaskTimingMetadata timing;

    constexpr GpuTaskDesc& setIdentity(const Name& value){ identity = value; return *this; }
    constexpr GpuTaskDesc& setMarkerLabel(const AStringView value){ markerLabel = value; return *this; }
    constexpr GpuTaskDesc& setQueue(const GpuQueueRequest& value){ queue = value; return *this; }
    constexpr GpuTaskDesc& setScheduling(const GpuTaskSchedulingHint& value){ scheduling = value; return *this; }
    constexpr GpuTaskDesc& setDependencies(const GpuTaskId* values, const usize count){ dependencies = values; dependencyCount = count; return *this; }
    constexpr GpuTaskDesc& setExternalDependencies(const GpuExternalCompletionId* values, const usize count){
        externalDependencies = values;
        externalDependencyCount = count;
        return *this;
    }
    constexpr GpuTaskDesc& setExternalStateSources(const GpuTaskExternalStateSource* values, const usize count){
        externalStateSources = values;
        externalStateSourceCount = count;
        return *this;
    }
    constexpr GpuTaskDesc& setResourceUses(const GpuTaskResourceUse* values, const usize count){
        resourceUses = values;
        resourceUseCount = count;
        return *this;
    }
    constexpr GpuTaskDesc& setResourceSetUses(const GpuTaskResourceSetUse* values, const usize count){
        resourceSetUses = values;
        resourceSetUseCount = count;
        return *this;
    }
    constexpr GpuTaskDesc& setTimingMetadata(const GpuTaskTimingMetadata& value){ timing = value; return *this; }
};

// Primitive native copies remain task-level operations: they are scheduled, packetized, and synchronized by the
// graph, but record directly to the selected CommandList without introducing a general command IR. The helpers
// own the resource-use declarations for every region, so callers must leave GpuTaskDesc::resourceUses empty.
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
    // Optional lifecycle output. It is written only after the containing packet submission has been accepted.
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
    // Optional lifecycle output. It is written only after the containing packet submission has been accepted.
    QueueSubmissionToken* acceptedToken = nullptr;
};

// Resolve operations are task-level primitives with explicit source/destination state declarations. The helper
// retains both textures until late recording and records directly through CommandList after queue assignment.
struct GpuResolveTextureTaskRegion{
    GpuGraphResourceId source;
    TextureSubresourceSet sourceSubresources = s_AllSubresources;
    GpuGraphResourceId destination;
    TextureSubresourceSet destinationSubresources = s_AllSubresources;
};

struct GpuResolveTextureTaskDesc{
    const GpuResolveTextureTaskRegion* regions = nullptr;
    usize regionCount = 0u;
    // Optional lifecycle output. It is written only after the containing packet submission has been accepted.
    QueueSubmissionToken* acceptedToken = nullptr;
};


// Caller bytes are copied into a graph-owned upload blob before declaration. The task records that blob through the
// existing CommandList staging path, so the blob is not a second persistent GPU upload allocator. `finalState`
// describes the task's graph-visible final state after its internal CopyDest write completes.
struct GpuUploadBufferTaskDesc{
    GpuUploadBlobId source;
    GpuGraphResourceId destination;
    u64 destinationOffsetBytes = 0u;
    ResourceStates::Mask finalState = ResourceStates::CopyDest;
    // Optional lifecycle output. Storage must outlive late recording/submission; the helper clears it at declaration
    // and again if the task is discarded, then writes the accepted packet token only after successful submission.
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
    // Optional lifecycle output. Storage must outlive late recording/submission; the helper clears it at declaration
    // and again if the task is discarded, then writes the accepted packet token only after successful submission.
    QueueSubmissionToken* acceptedToken = nullptr;
};


// Primitive clear helpers have the same graph-owned scheduling and lifecycle as copies, while retaining the
// selected native clear variant in their compact payload. They always write CopyDest state for the target resource.
struct GpuClearBufferTaskDesc{
    GpuGraphResourceId destination;
    u32 clearValue = 0u;
    // Optional lifecycle output. It is written only after the containing packet submission has been accepted.
    QueueSubmissionToken* acceptedToken = nullptr;
};

namespace GpuClearTextureTaskValueType{
    enum Enum : u8{
        Float,
        UInt,
        Int,
        DepthStencil,

        kCount,
    };
};

// A typed clear can bracket its own native command with narrow graph-owned instrumentation. The hooks stay outside
// command capture/replay: they are recording-local observability, while the clear command remains the portable POD
// operation captured by the optional IR stream.
using GpuClearTextureTaskRecordHook = bool(*)(
    void* context,
    CommandList& commandList,
    const GpuTaskRecordContext& recordContext
);
using GpuClearTextureTaskDiscardedHook = void(*)(void* context);

struct GpuClearTextureTaskRecordHooks{
    void* context = nullptr;
    GpuClearTextureTaskRecordHook beforeClear = nullptr;
    GpuClearTextureTaskRecordHook afterClear = nullptr;
    GpuClearTextureTaskDiscardedHook discarded = nullptr;
};

struct GpuClearTextureTaskDesc{
    // Optional lifecycle output. It is written only after the containing packet submission has been accepted.
    QueueSubmissionToken* acceptedToken = nullptr;
    GpuGraphResourceId destination;
    // Optional recording-local hooks. They bracket the native clear in the same graph task, so a later cross-queue
    // consumer cannot split an instrumentation endpoint away from the command it observes.
    GpuClearTextureTaskRecordHooks recordHooks{};
    f32 depthValue = 1.f;
    TextureSubresourceSet subresources = s_AllSubresources;
    GpuClearTextureTaskValueType::Enum valueType = GpuClearTextureTaskValueType::UInt;
    u8 stencilValue = 0u;
    bool clearDepth = false;
    bool clearStencil = false;
    Color floatValue{};
    UIntColor uintValue{};
    IntColor intValue{};
};
static_assert(sizeof(GpuClearTextureTaskDesc) == 128u, "GpuClearTextureTaskDesc should keep its compact runtime layout");

// A rectangular unsigned-integer clear keeps the same graph-owned CopyDest/lifecycle contract as the general
// texture clear, while preserving the work-region bounds that a whole-image clear cannot represent. It is kept
// deliberately narrow until another renderer path needs a different typed rectangle operation.
struct GpuClearTextureRectUIntTaskDesc{
    GpuGraphResourceId destination;
    TextureSubresourceSet subresources = s_AllSubresources;
    Rect rect;
    UIntColor uintValue;
    // Optional recording-local hooks use the same bracket contract as whole-texture clears.
    GpuClearTextureTaskRecordHooks recordHooks;
    // Optional lifecycle output. It is written only after the containing packet submission has been accepted.
    QueueSubmissionToken* acceptedToken = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

