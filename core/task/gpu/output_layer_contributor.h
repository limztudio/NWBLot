// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "types.h"

#include <core/graphics/rhi/gpu_descriptor_heap.h>
#include <core/graphics/rhi/presentation.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTaskGraph;

struct GpuTaskGraphOutputLayer{
    GpuTaskId readyTask;
    GpuGraphResourceId color;
    GpuGraphResourceVersionId colorVersion;
    GpuDescriptorHandle sampledImage;
    u64 frameGeneration = 0u;


public:
    [[nodiscard]] b8 validShape()const noexcept{
        const b8 hasColor = color.valid();
        return (!readyTask.valid() || frameGeneration != 0u) && (!hasColor || readyTask.valid()) && hasColor == colorVersion.valid() && hasColor == sampledImage.valid();
    }
};


interface IGpuTaskGraphOutputLayerContributor{
public:
    virtual ~IGpuTaskGraphOutputLayerContributor()noexcept = default;


public:
    [[nodiscard]] virtual b8 prepareTaskGraphOutputLayer(const AcquiredPresentationFrame& frame) = 0;
    [[nodiscard]] virtual Expected<GpuTaskGraphOutputLayer> declareTaskGraphOutputLayer(GpuTaskGraph& graph) = 0;
    virtual void acceptTaskGraphOutputLayer(u64 frameGeneration, const QueueSubmissionToken& submissionToken) = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

