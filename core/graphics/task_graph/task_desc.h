// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "types.h"

#include <core/graphics/rhi/device.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuCompiledGraph;
struct GpuTaskRecordContext;

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
};

// A resource may be metadata-only during the shadow-graph phase, or may retain an imported engine handle through
// one of GpuTaskGraph's typed import overloads. The latter is the required path before graph recording is enabled.
struct GpuGraphResourceDesc{
    Name identity = NAME_NONE;
    AStringView markerLabel;
    GpuGraphResourceType::Enum type = GpuGraphResourceType::HazardDomain;
    // Compiler-generated packet-boundary transitions begin from this state. Unknown remains valid only while a
    // transitional CommandListResourceStateHandoff supplies the authoritative imported state at recording time.
    ResourceStates::Mask initialState = ResourceStates::Unknown;
    ResourceQueueSharing::Mask queueSharing = ResourceQueueSharing::Exclusive;

    constexpr GpuGraphResourceDesc& setIdentity(const Name& value){ identity = value; return *this; }
    constexpr GpuGraphResourceDesc& setMarkerLabel(const AStringView value){ markerLabel = value; return *this; }
    constexpr GpuGraphResourceDesc& setType(const GpuGraphResourceType::Enum value){ type = value; return *this; }
    constexpr GpuGraphResourceDesc& setInitialState(const ResourceStates::Mask value){ initialState = value; return *this; }
    constexpr GpuGraphResourceDesc& setQueueSharing(const ResourceQueueSharing::Mask value){ queueSharing = value; return *this; }
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
    const GpuTaskResourceUse* resourceUses = nullptr;
    usize resourceUseCount = 0u;

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
    constexpr GpuTaskDesc& setResourceUses(const GpuTaskResourceUse* values, const usize count){
        resourceUses = values;
        resourceUseCount = count;
        return *this;
    }
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
    GpuGraphResourceId destination;
    TextureSubresourceSet subresources = s_AllSubresources;
    GpuClearTextureTaskValueType::Enum valueType = GpuClearTextureTaskValueType::UInt;
    Color floatValue;
    UIntColor uintValue;
    IntColor intValue;
    f32 depthValue = 1.f;
    u8 stencilValue = 0u;
    bool clearDepth = false;
    bool clearStencil = false;
    // Optional recording-local hooks. They bracket the native clear in the same graph task, so a later cross-queue
    // consumer cannot split an instrumentation endpoint away from the command it observes.
    GpuClearTextureTaskRecordHooks recordHooks;
    // Optional lifecycle output. It is written only after the containing packet submission has been accepted.
    QueueSubmissionToken* acceptedToken = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

