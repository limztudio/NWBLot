// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr f32 s_MeshletBoundsRadiusInflation = 1.25f;

struct MeshletBoundsCalculation{
    SIMDVector center = {};
    SIMDVector coneAxis = {};
    f32 radius = 0.0f;
    f32 coneCutoff = -1.0f;
};

template<typename PositionAtT, typename VisitFaceNormalsT>
[[nodiscard]] static MeshletBoundsCalculation CalculateMeshletBounds(
    const u32 positionCount,
    const u32 primitiveCount,
    const PositionAtT& positionAt,
    const VisitFaceNormalsT& visitFaceNormals
){
    MeshletBoundsCalculation result;

    SIMDVector minBounds;
    SIMDVector maxBounds;
    AabbTests::Reset(minBounds, maxBounds);
    for(u32 localPositionIndex = 0u; localPositionIndex < positionCount; ++localPositionIndex)
        AabbTests::Expand(positionAt(localPositionIndex), minBounds, maxBounds);

    result.center = AabbTests::Center(minBounds, maxBounds);
    SIMDVector radiusSquared = VectorZero();
    for(u32 localPositionIndex = 0u; localPositionIndex < positionCount; ++localPositionIndex){
        const SIMDVector delta = VectorSubtract(positionAt(localPositionIndex), result.center);
        radiusSquared = VectorMax(radiusSquared, Vector3LengthSq(delta));
    }
    result.radius = VectorGetX(VectorSqrt(radiusSquared));

    SIMDVector areaWeightedNormal = VectorZero();
    u32 validFaceNormalCount = 0u;
    visitFaceNormals([&](const SIMDVector faceNormal){
        if(!::FrameValidDirection(faceNormal))
            return;

        areaWeightedNormal = VectorAdd(areaWeightedNormal, faceNormal);
        ++validFaceNormalCount;
    });

    result.coneAxis = NormalizeMeshletDirectionOrZero(areaWeightedNormal);
    if(validFaceNormalCount == primitiveCount && ::FrameValidDirection(result.coneAxis)){
        result.coneCutoff = 1.0f;
        visitFaceNormals([&](const SIMDVector meshletFaceNormal){
            const SIMDVector faceNormal = NormalizeMeshletDirectionOrZero(meshletFaceNormal);
            if(::FrameValidDirection(faceNormal))
                result.coneCutoff = Min(result.coneCutoff, VectorGetX(Vector3Dot(result.coneAxis, faceNormal)));
        });
        if(result.coneCutoff <= 0.0f)
            result.coneCutoff = -1.0f;
    }

    return result;
}

template<typename CookEntryT>
static MeshletBounds BuildMeshletBounds(const CookEntryT& entry, const MeshletDesc& meshlet){
    const auto positionAt = [&](const u32 localPositionIndex){
        const MeshletPositionStreamRef& ref = entry.meshletPositionStreamRefs[meshlet.positionRefOffset + localPositionIndex];
        return LoadMeshletPositionStreamVector(entry, ref);
    };
    const auto visitFaceNormals = [&](auto&& callback){
        for(u32 primitiveIndex = 0u; primitiveIndex < MeshletPrimitiveCount(meshlet); ++primitiveIndex){
            const usize primitiveOffset = meshlet.primitiveOffset + static_cast<usize>(primitiveIndex) * 3u;
            const u8 localVertex0 = entry.meshletPrimitiveIndices[primitiveOffset + 0u];
            const u8 localVertex1 = entry.meshletPrimitiveIndices[primitiveOffset + 1u];
            const u8 localVertex2 = entry.meshletPrimitiveIndices[primitiveOffset + 2u];
            const SIMDVector p0 = LoadMeshletLocalPositionStreamVector(entry, meshlet, localVertex0);
            const SIMDVector p1 = LoadMeshletLocalPositionStreamVector(entry, meshlet, localVertex1);
            const SIMDVector p2 = LoadMeshletLocalPositionStreamVector(entry, meshlet, localVertex2);
            callback(TriangleTests::AreaNormal(p0, p1, p2));
        }
    };
    const MeshletBoundsCalculation calculation = CalculateMeshletBounds(
        MeshletPositionCount(meshlet),
        MeshletPrimitiveCount(meshlet),
        positionAt,
        visitFaceNormals
    );

    MeshletBounds bounds;
    StoreFloat(
        VectorSetW(calculation.center, calculation.radius * s_MeshletBoundsRadiusInflation),
        &bounds.sphere
    );
    bounds.conePacked = PackMeshletCone(calculation.coneAxis, calculation.coneCutoff);
    return bounds;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

