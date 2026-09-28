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


// A full-output-size, single-sample RGBA16_FLOAT layer containing premultiplied linear Rec.709 color.
// The final output task reads colorVersion and samples sampledImage after scene display mapping and before encoding.
struct GpuTaskGraphOutputLayer{
    GpuTaskId readyTask;
    GpuGraphResourceId color;
    GpuGraphResourceVersionId colorVersion;
    GpuDescriptorHandle sampledImage;
    u64 frameGeneration = 0u;
};


interface IGpuTaskGraphOutputLayerContributor{
public:
    virtual ~IGpuTaskGraphOutputLayerContributor() = default;


public:
    // Preparation owns allocation and captures immutable frame/resource versions before native recording.
    [[nodiscard]] virtual bool prepareTaskGraphOutputLayer(const AcquiredPresentationFrame& frame) = 0;
    // Producers have no scene or acquired-image dependency. True with an empty result means no work; upload-only
    // work returns readyTask and frameGeneration with no color, colorVersion or sampledImage. False rejects the graph.
    [[nodiscard]] virtual bool declareTaskGraphOutputLayer(GpuTaskGraph& graph, GpuTaskGraphOutputLayer& outLayer) = 0;
    // Only the accepted final consumer calls this. Producer acceptance must not consume the pending CPU frame.
    // Acceptance is not completion: output texture, descriptor and backing versions remain live through this token.
    // A rejected/unsubmitted consumer never calls this, leaving the immutable pending generation available for retry.
    virtual void acceptTaskGraphOutputLayer(u64 frameGeneration, const QueueSubmissionToken& submissionToken) = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

