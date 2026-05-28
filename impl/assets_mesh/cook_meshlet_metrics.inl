// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename VectorT>
[[nodiscard]] static usize MeshletCookVectorBytes(const VectorT& values){
    return values.size() * sizeof(typename VectorT::value_type);
}

template<typename CookEntryT>
[[nodiscard]] static usize EstimateCommonMeshletSourceBytes(
    const Core::Assets::AssetVector<u32>& indices,
    const CookEntryT& entry
){
    return MeshletCookVectorBytes(indices)
        + MeshletCookVectorBytes(entry.positions)
        + MeshletCookVectorBytes(entry.normals)
        + MeshletCookVectorBytes(entry.tangents)
        + MeshletCookVectorBytes(entry.uv0)
        + MeshletCookVectorBytes(entry.colors)
        + MeshletCookVectorBytes(entry.vertexRefs)
    ;
}

[[nodiscard]] static usize EstimateMeshletSkinBytes(const SkinnedMeshCookEntry& entry){
    return MeshletCookVectorBytes(entry.skin)
        + MeshletCookVectorBytes(entry.inverseBindMatrices)
    ;
}

[[nodiscard]] static usize EstimateMeshletSourceBytes(
    const Core::Assets::AssetVector<u32>& indices,
    const MeshCookEntry& entry
){
    return EstimateCommonMeshletSourceBytes(indices, entry);
}

[[nodiscard]] static usize EstimateMeshletSourceBytes(
    const Core::Assets::AssetVector<u32>& indices,
    const SkinnedMeshCookEntry& entry
){
    return EstimateCommonMeshletSourceBytes(indices, entry)
        + EstimateMeshletSkinBytes(entry)
    ;
}

template<typename CookEntryT>
[[nodiscard]] static usize EstimateCommonMeshletRuntimeBytes(const CookEntryT& entry){
    return MeshletCookVectorBytes(entry.positions)
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

[[nodiscard]] static usize EstimateMeshletRuntimeBytes(const MeshCookEntry& entry){
    return EstimateCommonMeshletRuntimeBytes(entry);
}

[[nodiscard]] static usize EstimateMeshletRuntimeBytes(const SkinnedMeshCookEntry& entry){
    return EstimateCommonMeshletRuntimeBytes(entry)
        + EstimateMeshletSkinBytes(entry)
    ;
}

template<typename CookEntryT>
[[nodiscard]] static MeshletCookMetrics BuildMeshletCookMetrics(const CookEntryT& entry){
    MeshletCookMetrics metrics;
    metrics.meshletCount = static_cast<u32>(entry.meshlets.size());
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
        metrics.radiusSum += bounds.sphere.w;

        if(!MeshletConeEnabled(bounds)){
            ++metrics.coneDisabledCount;
            continue;
        }

        const f32 coneCutoff = static_cast<f32>(MeshletConePackedCutoff(bounds)) * (1.0f / 255.0f);
        ++metrics.coneEnabledCount;
        metrics.coneCutoffSum += coneCutoff;
        metrics.worstConeCutoff = Min(metrics.worstConeCutoff, coneCutoff);
    }

    if(metrics.meshletCount == 0u){
        metrics.minPrimitiveCount = 0u;
        metrics.minVertexCount = 0u;
    }

    return metrics;
}

template<typename CookEntryT>
static void LogMeshletCookMetrics(
    const Path& nwbFilePath,
    const tchar* metaKind,
    const Core::Assets::AssetVector<u32>& indices,
    const CookEntryT& entry
){
    const MeshletCookMetrics metrics = BuildMeshletCookMetrics(entry);
    if(metrics.meshletCount == 0u)
        return;

    const f64 invMeshletCount = 1.0 / static_cast<f64>(metrics.meshletCount);
    const f64 primitiveCountAverage = static_cast<f64>(metrics.primitiveCountSum) * invMeshletCount;
    const f64 vertexCountAverage = static_cast<f64>(metrics.vertexCountSum) * invMeshletCount;
    const f64 positionCountAverage = static_cast<f64>(metrics.positionCountSum) * invMeshletCount;
    const f64 attributeCountAverage = static_cast<f64>(metrics.attributeCountSum) * invMeshletCount;
    const f64 radiusAverage = metrics.radiusSum * invMeshletCount;
    const f64 coneDisabledPercentage = static_cast<f64>(metrics.coneDisabledCount) * 100.0 * invMeshletCount;
    const f64 coneCutoffAverage = metrics.coneEnabledCount != 0u
        ? metrics.coneCutoffSum / static_cast<f64>(metrics.coneEnabledCount)
        : 0.0
    ;
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

