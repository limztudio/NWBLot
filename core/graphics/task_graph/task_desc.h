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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

