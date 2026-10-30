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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Cook-time meshlet statistics and the source/runtime byte estimate logged after each build.
class MeshletCookMetricsReport final : NoCopy{
public:
    template<typename VectorT>
    [[nodiscard]] static usize MeshletCookVectorBytes(const VectorT& values)noexcept(noexcept(static_cast<usize>(values.size() * sizeof(typename VectorT::value_type))));
    [[nodiscard]] static usize EstimateMeshletSourceBytes(Span<const u32> indices, const MeshCookEntry& entry)noexcept;
    [[nodiscard]] static usize EstimateMeshletRuntimeBytes(const MeshCookEntry& entry)noexcept;
    [[nodiscard]] static MeshletCookMetrics BuildMeshletCookMetrics(const MeshCookEntry& entry)noexcept;
    static void LogMeshletCookMetrics(const Path& nwbFilePath, const TStringView metaKind, Span<const u32> indices, const MeshCookEntry& entry);


public:
    MeshletCookMetricsReport() = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename VectorT>
usize MeshletCookMetricsReport::MeshletCookVectorBytes(const VectorT& values)noexcept(noexcept(static_cast<usize>(values.size() * sizeof(typename VectorT::value_type)))){
    return static_cast<usize>(values.size() * sizeof(typename VectorT::value_type));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

