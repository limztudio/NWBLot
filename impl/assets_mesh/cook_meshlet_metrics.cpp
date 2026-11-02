// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_meshlet_metrics.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


usize MeshletCookMetricsReport::EstimateMeshletSourceBytes(const Span<const u32> indices, const MeshCookEntry& entry)noexcept{
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


usize MeshletCookMetricsReport::EstimateMeshletRuntimeBytes(const MeshCookEntry& entry)noexcept{
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


void MeshletCookMetricsReport::LogMeshletCookMetrics(const Path& nwbFilePath, const TStringView metaKind, const Span<const u32> indices, const MeshCookEntry& entry){
    const MeshletCookMetrics metrics = BuildMeshletCookMetricsFromStreams(entry.meshlets, entry.meshletBounds);
    if(metrics.meshletCount == 0u)
        return;

    const f64 invMeshletCount = 1.0 / static_cast<f64>(metrics.meshletCount);
    const f64 primitiveCountAverage = static_cast<f64>(metrics.primitiveCountSum) * invMeshletCount;
    const f64 vertexCountAverage = static_cast<f64>(metrics.vertexCountSum) * invMeshletCount;
    const f64 positionCountAverage = static_cast<f64>(metrics.positionCountSum) * invMeshletCount;
    const f64 attributeCountAverage = static_cast<f64>(metrics.attributeCountSum) * invMeshletCount;
    const f64 radiusAverage = metrics.radiusSum * invMeshletCount;
    const f64 coneDisabledPercentage = static_cast<f64>(metrics.coneDisabledCount) * s_MeshletPercentScale * invMeshletCount;
    const f64 coneCutoffAverage = metrics.coneEnabledCount != 0u ? (metrics.coneCutoffSum / static_cast<f64>(metrics.coneEnabledCount)) : 0.0;
    const f32 worstConeCutoff = metrics.coneEnabledCount != 0u ? metrics.worstConeCutoff : 0.0f;
    const usize sourceBytes = EstimateMeshletSourceBytes(indices, entry);
    const usize runtimeBytes = EstimateMeshletRuntimeBytes(entry);

    NWB_LOGGER_ESSENTIAL_INFO(
        NWB_TEXT("{} meta '{}': meshlet cook metrics - meshlets {}, primitives avg {:.2f} min {} max {}, local vertices avg {:.2f} min {} max {}, deformed positions avg {:.2f}, ")
        NWB_TEXT("attributes avg {:.2f}, sphere radius avg {:.4f}, cones disabled {:.2f}% ({}/{}), cone cutoff avg {:.4f} worst {:.4f}, bytes source {} runtime {}")
        , metaKind
        , PathToString<tchar>(nwbFilePath)
        , metrics.meshletCount
        , primitiveCountAverage
        , metrics.minPrimitiveCount
        , metrics.maxPrimitiveCount
        , vertexCountAverage
        , metrics.minVertexCount
        , metrics.maxVertexCount
        , positionCountAverage
        , attributeCountAverage
        , radiusAverage
        , coneDisabledPercentage
        , metrics.coneDisabledCount
        , metrics.meshletCount
        , coneCutoffAverage
        , worstConeCutoff
        , sourceBytes
        , runtimeBytes
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

