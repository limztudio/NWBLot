// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "cook_meshlet_triangles.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MeshletCookMetrics{
    u32 meshletCount = 0u;
    u32 minPrimitiveCount = Limit<u32>::s_Max;
    u32 maxPrimitiveCount = 0u;
    u32 minVertexCount = Limit<u32>::s_Max;
    u32 maxVertexCount = 0u;
    f32 worstConeCutoff = 1.0f;
    u64 primitiveCountSum = 0u;
    u64 vertexCountSum = 0u;
    u64 positionCountSum = 0u;
    u64 attributeCountSum = 0u;
    f64 radiusSum = 0.0;
    u32 coneDisabledCount = 0u;
    u32 coneEnabledCount = 0u;
    f64 coneCutoffSum = 0.0;
};

static constexpr f64 s_MeshletPercentScale = 100.0;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Cook-time meshlet statistics and the source/runtime byte estimate logged after each build.
class MeshletCookMetricsReport final : NoCopy{
public:
    template<typename VectorT>
    [[nodiscard]] static usize MeshletCookVectorBytes(const VectorT& values)noexcept(noexcept(static_cast<usize>(values.size() * sizeof(typename VectorT::value_type))));
    template<typename CookEntryT>
    [[nodiscard]] static usize EstimateCommonMeshletSourceBytes(const Core::Assets::AssetVector<u32>& indices, const CookEntryT& entry)noexcept(noexcept(
        MeshletCookVectorBytes(indices)
        + MeshletCookVectorBytes(entry.positions)
        + MeshletCookVectorBytes(entry.normals)
        + MeshletCookVectorBytes(entry.tangents)
        + MeshletCookVectorBytes(entry.uv0)
        + MeshletCookVectorBytes(entry.colors)
        + MeshletCookVectorBytes(entry.vertexRefs)
    ));
    [[nodiscard]] static usize EstimateMeshletSourceBytes(const Core::Assets::AssetVector<u32>& indices, const MeshCookEntry& entry)noexcept;
    template<typename CookEntryT>
    [[nodiscard]] static usize EstimateCommonMeshletRuntimeBytes(const CookEntryT& entry)noexcept(noexcept(
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
    ));
    [[nodiscard]] static usize EstimateMeshletRuntimeBytes(const MeshCookEntry& entry)noexcept;
    template<typename CookEntryT>
    [[nodiscard]] static MeshletCookMetrics BuildMeshletCookMetrics(const CookEntryT& entry)noexcept(
        noexcept(static_cast<u32>(entry.meshlets.size()))
        && noexcept(static_cast<bool>(DeclVal<usize&>() < entry.meshlets.size()))
        && noexcept(DeclVal<void(*)(const MeshletDesc&)noexcept>()(entry.meshlets[DeclVal<usize&>()]))
        && noexcept(DeclVal<void(*)(const MeshletBounds&)noexcept>()(entry.meshletBounds[DeclVal<usize&>()]))
    );
    template<typename CookEntryT>
    static void LogMeshletCookMetrics(const Path& nwbFilePath, const TStringView metaKind, const Core::Assets::AssetVector<u32>& indices, const CookEntryT& entry);


public:
    MeshletCookMetricsReport() = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename VectorT>
usize MeshletCookMetricsReport::MeshletCookVectorBytes(const VectorT& values)noexcept(noexcept(static_cast<usize>(values.size() * sizeof(typename VectorT::value_type)))){
    return static_cast<usize>(values.size() * sizeof(typename VectorT::value_type));
}


template<typename CookEntryT>
usize MeshletCookMetricsReport::EstimateCommonMeshletSourceBytes(const Core::Assets::AssetVector<u32>& indices, const CookEntryT& entry)noexcept(noexcept(
    MeshletCookVectorBytes(indices)
    + MeshletCookVectorBytes(entry.positions)
    + MeshletCookVectorBytes(entry.normals)
    + MeshletCookVectorBytes(entry.tangents)
    + MeshletCookVectorBytes(entry.uv0)
    + MeshletCookVectorBytes(entry.colors)
    + MeshletCookVectorBytes(entry.vertexRefs)
)){
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


template<typename CookEntryT>
usize MeshletCookMetricsReport::EstimateCommonMeshletRuntimeBytes(const CookEntryT& entry)noexcept(noexcept(
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
)){
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


template<typename CookEntryT>
MeshletCookMetrics MeshletCookMetricsReport::BuildMeshletCookMetrics(const CookEntryT& entry)noexcept(
    noexcept(static_cast<u32>(entry.meshlets.size()))
    && noexcept(static_cast<bool>(DeclVal<usize&>() < entry.meshlets.size()))
    && noexcept(DeclVal<void(*)(const MeshletDesc&)noexcept>()(entry.meshlets[DeclVal<usize&>()]))
    && noexcept(DeclVal<void(*)(const MeshletBounds&)noexcept>()(entry.meshletBounds[DeclVal<usize&>()]))
){
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
        radiusAndCutoffSums = VectorDoubleAdd(radiusAndCutoffSums, VectorDoubleSet(bounds.sphere.w, VectorGetX(coneCutoff), 0.0, 0.0));
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


template<typename CookEntryT>
void MeshletCookMetricsReport::LogMeshletCookMetrics(const Path& nwbFilePath, const TStringView metaKind, const Core::Assets::AssetVector<u32>& indices, const CookEntryT& entry){
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

