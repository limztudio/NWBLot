// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "cook_meshlet_triangles.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


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
