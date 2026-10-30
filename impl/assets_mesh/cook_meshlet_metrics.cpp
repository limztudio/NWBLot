// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_meshlet_metrics.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr f64 s_MeshletPercentScale = 100.0;


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


MeshletCookMetrics MeshletCookMetricsReport::BuildMeshletCookMetrics(const MeshCookEntry& entry)noexcept{
    MeshletCookMetrics metrics;
    metrics.meshletCount = static_cast<u32>(entry.meshlets.size());
    SIMDVectorDouble radiusAndCutoffSums = VectorDoubleZero();
    SIMDVectorSingle worstConeCutoff = VectorReplicate(metrics.worstConeCutoff);
    for(usize meshletIndex = 0u; meshletIndex < entry.meshlets.size(); ++meshletIndex){
        const MeshletDesc& meshlet = entry.meshlets[meshletIndex];
        const MeshletBounds& bounds = entry.meshletBounds[meshletIndex];
        const u32 primitiveCount = MeshletPrimitiveCount(meshlet);
        const u32 vertexCount = MeshletVertexCount(meshlet);
        metrics.minPrimitiveCount = Min(metrics.minPrimitiveCount, primitiveCount);
        metrics.maxPrimitiveCount = Max(metrics.maxPrimitiveCount, primitiveCount);
        metrics.minVertexCount = Min(metrics.minVertexCount, vertexCount);
        metrics.maxVertexCount = Max(metrics.maxVertexCount, vertexCount);
        metrics.primitiveCountSum += primitiveCount;
        metrics.vertexCountSum += vertexCount;
        metrics.positionCountSum += MeshletPositionCount(meshlet);
        metrics.attributeCountSum += MeshletAttributeCount(meshlet);
        if(!MeshletConeEnabled(bounds)){
            radiusAndCutoffSums = VectorDoubleAdd(radiusAndCutoffSums, VectorDoubleSet(bounds.sphere.w, 0.0, 0.0, 0.0));
            ++metrics.coneDisabledCount;
            continue;
        }

        const SIMDVectorSingle coneCutoff = VectorScale(VectorReplicate(static_cast<f32>(MeshletConePackedCutoff(bounds))), 1.0f / s_MeshletUnorm8Max);
        ++metrics.coneEnabledCount;
        radiusAndCutoffSums = VectorDoubleAdd(radiusAndCutoffSums, VectorDoublePermute<0, 4, 2, 3>(VectorDoubleSet(bounds.sphere.w, 0.0, 0.0, 0.0), ConvertVectorSingleToDouble(coneCutoff)));
        worstConeCutoff = VectorMin(worstConeCutoff, coneCutoff);
    }
    metrics.worstConeCutoff = VectorGetX(worstConeCutoff);
    metrics.radiusSum = VectorDoubleGetX(radiusAndCutoffSums);
    metrics.coneCutoffSum = VectorDoubleGetY(radiusAndCutoffSums);

    if(metrics.meshletCount == 0u){
        metrics.minPrimitiveCount = 0u;
        metrics.minVertexCount = 0u;
    }

    return metrics;
}


void MeshletCookMetricsReport::LogMeshletCookMetrics(const Path& nwbFilePath, const TStringView metaKind, const Span<const u32> indices, const MeshCookEntry& entry){
    const MeshletCookMetrics metrics = BuildMeshletCookMetrics(entry);
    if(metrics.meshletCount == 0u)
        return;

    const SIMDVectorDouble reciprocalAndCutoff = VectorDoubleDivide(
        VectorDoubleSet(1.0, metrics.coneEnabledCount != 0u ? metrics.coneCutoffSum : 0.0, 0.0, 0.0),
        VectorDoubleSet(static_cast<f64>(metrics.meshletCount), metrics.coneEnabledCount != 0u ? static_cast<f64>(metrics.coneEnabledCount) : 1.0, 1.0, 1.0)
    );
    const SIMDVectorDouble invMeshletCount = VectorDoubleSplatX(reciprocalAndCutoff);
    const SIMDVectorDouble countAverages = VectorDoubleMultiply(VectorDoubleSet(static_cast<f64>(metrics.primitiveCountSum), static_cast<f64>(metrics.vertexCountSum), static_cast<f64>(metrics.positionCountSum), static_cast<f64>(metrics.attributeCountSum)), invMeshletCount);
    const f64 primitiveCountAverage = VectorDoubleGetX(countAverages);
    const f64 vertexCountAverage = VectorDoubleGetY(countAverages);
    const f64 positionCountAverage = VectorDoubleGetZ(countAverages);
    const f64 attributeCountAverage = VectorDoubleGetW(countAverages);
    const SIMDVectorDouble radiusAndPercentage = VectorDoubleMultiply(VectorDoubleMultiply(VectorDoubleSet(metrics.radiusSum, static_cast<f64>(metrics.coneDisabledCount), 0.0, 0.0), VectorDoubleSet(1.0, s_MeshletPercentScale, 1.0, 1.0)), invMeshletCount);
    const f64 radiusAverage = VectorDoubleGetX(radiusAndPercentage);
    const f64 coneDisabledPercentage = VectorDoubleGetY(radiusAndPercentage);
    const f64 coneCutoffAverage = VectorDoubleGetY(reciprocalAndCutoff);
    const f32 worstConeCutoff = metrics.coneEnabledCount != 0u ? metrics.worstConeCutoff : 0.0f;
    const usize sourceBytes = EstimateMeshletSourceBytes(indices, entry);
    const usize runtimeBytes = EstimateMeshletRuntimeBytes(entry);

    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("{} meta '{}': meshlet cook metrics - meshlets {}, primitives avg {:.2f} min {} max {}, local vertices avg {:.2f} min {} max {}, deformed positions avg {:.2f}, attributes avg {:.2f}, sphere radius avg {:.4f}, cones disabled {:.2f}% ({}/{}), cone cutoff avg {:.4f} worst {:.4f}, bytes source {} runtime {}")
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

