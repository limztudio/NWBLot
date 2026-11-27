// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "types.h"

#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTaskGraph;

enum class GpuTaskGraphInstanceTransformPatchLayout : u32{
    InstanceTrs = 0u,
    Affine3x4ObjectToWorld = 1u,
    Affine3x4WorldToObject = 2u
};

struct GpuTaskGraphInstanceTransformPatchRequest{
    GpuGraphResourceId instanceBufferResource;
    u32 instanceBufferUavSlot = Limit<u32>::s_Max;
    u32 instanceStrideFloat4 = 0u;
    u32 baseFloat4Offset = 0u;
    GpuTaskGraphInstanceTransformPatchLayout layout = GpuTaskGraphInstanceTransformPatchLayout::InstanceTrs;
    Name passIdentityPrefix = s_NameNone;
    GpuTaskId dependency;
    b8 mergeWithPrevious = true;
    b8 allowMergeAcrossConsumerFrontier = false;
};


interface IGpuTaskGraphInstanceTransformPatchContributor{
public:
    virtual ~IGpuTaskGraphInstanceTransformPatchContributor()noexcept = default;


public:
    [[nodiscard]] virtual b8 prepareInstanceTransformPatches() = 0;
    [[nodiscard]] virtual b8 hasActiveInstanceTransformPatches()const = 0;
    [[nodiscard]] virtual u64 instanceTransformPatchMembershipSignature()const = 0;
    [[nodiscard]] virtual Expected<GpuTaskId> declareInstanceTransformPatch(const GpuTaskGraphInstanceTransformPatchRequest& request, Span<const u32> instanceEntityIds, GpuTaskGraph& graph) = 0;
    [[nodiscard]] virtual Expected<GpuTaskId> declareBatchInstanceTransformPatches(
        Span<const GpuTaskGraphInstanceTransformPatchRequest> requests,
        Span<const u32> packedEntityIds,
        Span<const u32> entityIdOffsets,
        Span<const u32> entityIdCounts,
        Name batchIdentityPrefix,
        GpuTaskId dependency,
        b8 mergeWithPrevious,
        b8 allowMergeAcrossConsumerFrontier,
        GpuTaskGraph& graph
    ) = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

