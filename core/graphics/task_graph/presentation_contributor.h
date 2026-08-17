// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "types.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTaskGraph;


// A presentation contributor records the final graphics work that must remain ordered after a renderer's scene
// output and before the swap-chain submission is signalled. The contributor owns its payload and CPU-side
// acceptance lifecycle; Graphics only keeps this non-owning registration seam so renderer and UI modules stay
// independent.
class IGpuTaskGraphPresentationContributor{
public:
    virtual ~IGpuTaskGraphPresentationContributor() = default;


public:
    // Called during ordinary renderer preparation, before graph compilation and native recording. A false return
    // leaves this frame's scene presentation usable without the optional contributor.
    [[nodiscard]] virtual bool prepareTaskGraphPresentation(Framebuffer* framebuffer) = 0;
    // Preparation may succeed while there is no visible overlay work (for example, an empty ImGui draw list).
    // Such a frame deliberately skips declaration instead of manufacturing an empty presentation packet.
    [[nodiscard]] virtual bool hasTaskGraphPresentationWork()const = 0;
    // `previousTask` is the scene-output endpoint and `backbuffer` is its graph-owned presentation hazard domain.
    // The returned task must be Graphics-capable and ordered after that endpoint. A task that writes the backbuffer
    // must declare the same domain; an upload-only contributor may instead return a terminal completion packet.
    [[nodiscard]] virtual GpuTaskId declareTaskGraphPresentation(
        GpuTaskGraph& graph,
        Framebuffer* framebuffer,
        GpuGraphResourceId backbuffer,
        GpuTaskId previousTask
    ) = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

