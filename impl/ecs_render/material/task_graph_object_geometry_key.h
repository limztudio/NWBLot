// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "renderer_draw_types.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Source content and decoder identity are independent of view-dependent generated index ownership.
struct ObjectGeometryEquivalenceKey{
    const Core::Buffer* buffer = nullptr;
    const Core::ComputePipeline* decoder = nullptr;
    const Core::ComputePipeline* selectedDecoder = nullptr;
    Core::GpuDescriptorHandle heapHandle = Core::GpuDescriptorHandle::invalid();
    u64 sourceRevision = 0u;
    bool initialized = false;
    bool requiresDecode = true;

    [[nodiscard]] bool matches(const MaterialPassDrawItem& draw)const noexcept{
        const ObjectGeometryCacheSnapshot& cache = draw.meshResources.objectGeometryCache;
        return buffer == cache.buffer.get() && decoder == cache.decoderPipeline.get()
            && selectedDecoder == draw.pipelineResources.objectGeometryDecodePipeline.get()
            && heapHandle == cache.heapHandle && sourceRevision == cache.sourceRevision
            && initialized == cache.initialized && requiresDecode == cache.requiresDecode;
    }
};

[[nodiscard]] inline ObjectGeometryEquivalenceKey MakeObjectGeometryEquivalenceKey(const MaterialPassDrawItem& draw)noexcept{
    const ObjectGeometryCacheSnapshot& cache = draw.meshResources.objectGeometryCache;
    return {
        cache.buffer.get(), cache.decoderPipeline.get(), draw.pipelineResources.objectGeometryDecodePipeline.get(),
        cache.heapHandle, cache.sourceRevision, cache.initialized, cache.requiresDecode,
    };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

