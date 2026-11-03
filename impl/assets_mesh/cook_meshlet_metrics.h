// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "cook_meshlet_triangles.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Cook-time meshlet statistics and the source/runtime byte estimate logged after each build.
namespace MeshletCookMetricsReport{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename VectorT>
[[nodiscard]] usize MeshletCookVectorBytes(const VectorT& values)noexcept(noexcept(static_cast<usize>(values.size() * sizeof(typename VectorT::value_type))));
[[nodiscard]] inline usize EstimateMeshletSourceBytes(const Span<const u32> indices, const MeshCookEntry& entry)noexcept{
    return
        MeshletCookVectorBytes(indices)
        + MeshletCookVectorBytes(entry.positions)
        + MeshletCookVectorBytes(entry.normals)
        + MeshletCookVectorBytes(entry.tangents)
        + MeshletCookVectorBytes(entry.uv0)
        + MeshletCookVectorBytes(entry.colors)
        + MeshletCookVectorBytes(entry.vertexRefs)
    ;
}
[[nodiscard]] inline usize EstimateMeshletRuntimeBytes(const MeshCookEntry& entry)noexcept{
    return
        MeshletCookVectorBytes(entry.positions)
        + MeshletCookVectorBytes(entry.normals)
        + MeshletCookVectorBytes(entry.tangents)
        + MeshletCookVectorBytes(entry.uv0)
        + MeshletCookVectorBytes(entry.colors)
        + MeshletCookVectorBytes(entry.meshlets)
        + MeshletCookVectorBytes(entry.meshletBounds)
        + MeshletCookVectorBytes(entry.meshletPositionStreamRefs)
        + MeshletCookVectorBytes(entry.meshletAttributeStreamRefs)
        + MeshletCookVectorBytes(entry.meshletLocalVertexRefs)
        + MeshletCookVectorBytes(entry.meshletPrimitiveIndices)
    ;
}
inline void LogMeshletCookMetrics(const Path& nwbFilePath, const TStringView metaKind, const Span<const u32> indices, const MeshCookEntry& entry){
    const MeshletCookMetrics metrics = BuildMeshletCookMetricsFromStreams(entry.meshlets, entry.meshletBounds);
    if(metrics.meshletCount == 0u)
        return;

    const MeshletCookSummary summary = SummarizeMeshletCookMetrics(metrics);
    const usize sourceBytes = EstimateMeshletSourceBytes(indices, entry);
    const usize runtimeBytes = EstimateMeshletRuntimeBytes(entry);

    NWB_LOGGER_ESSENTIAL_INFO(
        NWB_TEXT("{} meta '{}': meshlet cook metrics - meshlets {}, primitives avg {:.2f} min {} max {}, local vertices avg {:.2f} min {} max {}, deformed positions avg {:.2f}, ")
        NWB_TEXT("attributes avg {:.2f}, sphere radius avg {:.4f}, cones disabled {:.2f}% ({}/{}), cone cutoff avg {:.4f} worst {:.4f}, bytes source {} runtime {}")
        , metaKind
        , PathToString<tchar>(nwbFilePath)
        , metrics.meshletCount
        , summary.primitiveCountAverage
        , metrics.minPrimitiveCount
        , metrics.maxPrimitiveCount
        , summary.vertexCountAverage
        , metrics.minVertexCount
        , metrics.maxVertexCount
        , summary.positionCountAverage
        , summary.attributeCountAverage
        , summary.radiusAverage
        , summary.coneDisabledPercentage
        , metrics.coneDisabledCount
        , metrics.meshletCount
        , summary.coneCutoffAverage
        , summary.worstConeCutoff
        , sourceBytes
        , runtimeBytes
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename VectorT>
usize MeshletCookMetricsReport::MeshletCookVectorBytes(const VectorT& values)noexcept(noexcept(static_cast<usize>(values.size() * sizeof(typename VectorT::value_type)))){
    return static_cast<usize>(values.size() * sizeof(typename VectorT::value_type));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

