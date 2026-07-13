// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr f32 s_MeshletBoundsRadiusInflation = 1.25f;

template<typename CookEntryT>
static MeshletBounds BuildMeshletBounds(const CookEntryT& entry, const MeshletDesc& meshlet){
    SIMDVector minBounds;
    SIMDVector maxBounds;
    AabbTests::Reset(minBounds, maxBounds);
    for(u32 localPositionIndex = 0u; localPositionIndex < MeshletPositionCount(meshlet); ++localPositionIndex){
        const MeshletPositionStreamRef& ref = entry.meshletPositionStreamRefs[meshlet.positionRefOffset + localPositionIndex];
        const SIMDVector position = VectorSetW(LoadFloat(MeshletPositionStreamValue(entry, ref)), 0.0f);
        AabbTests::Expand(position, minBounds, maxBounds);
    }

    const SIMDVector center = AabbTests::Center(minBounds, maxBounds);
    SIMDVector radiusSquared = VectorZero();
    for(u32 localPositionIndex = 0u; localPositionIndex < MeshletPositionCount(meshlet); ++localPositionIndex){
        const MeshletPositionStreamRef& ref = entry.meshletPositionStreamRefs[meshlet.positionRefOffset + localPositionIndex];
        const SIMDVector position = VectorSetW(LoadFloat(MeshletPositionStreamValue(entry, ref)), 0.0f);
        const SIMDVector delta = VectorSubtract(position, center);
        radiusSquared = VectorMax(radiusSquared, Vector3LengthSq(delta));
    }

    SIMDVector areaWeightedNormal = VectorZero();
    u32 validFaceNormalCount = 0u;
    auto visitMeshletFaceNormals = [&](auto&& callback){
        for(u32 primitiveIndex = 0u; primitiveIndex < MeshletPrimitiveCount(meshlet); ++primitiveIndex){
            const usize primitiveOffset = meshlet.primitiveOffset + static_cast<usize>(primitiveIndex) * 3u;
            const u8 localVertex0 = entry.meshletPrimitiveIndices[primitiveOffset + 0u];
            const u8 localVertex1 = entry.meshletPrimitiveIndices[primitiveOffset + 1u];
            const u8 localVertex2 = entry.meshletPrimitiveIndices[primitiveOffset + 2u];
            const SIMDVector p0 = VectorSetW(
                LoadFloat(MeshletLocalPositionStreamValue(entry, meshlet, localVertex0)),
                0.0f
            );
            const SIMDVector p1 = VectorSetW(
                LoadFloat(MeshletLocalPositionStreamValue(entry, meshlet, localVertex1)),
                0.0f
            );
            const SIMDVector p2 = VectorSetW(
                LoadFloat(MeshletLocalPositionStreamValue(entry, meshlet, localVertex2)),
                0.0f
            );
            callback(TriangleTests::AreaNormal(p0, p1, p2));
        }
    };
    visitMeshletFaceNormals([&](const SIMDVector faceNormal){
        if(!Core::Mesh::FrameValidDirection(faceNormal))
            return;

        areaWeightedNormal = VectorAdd(areaWeightedNormal, faceNormal);
        ++validFaceNormalCount;
    });

    const SIMDVector coneAxis = NormalizeMeshletDirectionOrZero(areaWeightedNormal);
    f32 coneCutoff = -1.0f;
    if(validFaceNormalCount == MeshletPrimitiveCount(meshlet) && Core::Mesh::FrameValidDirection(coneAxis)){
        coneCutoff = 1.0f;
        visitMeshletFaceNormals([&](const SIMDVector meshletFaceNormal){
            const SIMDVector faceNormal = NormalizeMeshletDirectionOrZero(meshletFaceNormal);
            if(Core::Mesh::FrameValidDirection(faceNormal))
                coneCutoff = Min(coneCutoff, VectorGetX(Vector3Dot(coneAxis, faceNormal)));
        });
        if(coneCutoff <= 0.0f)
            coneCutoff = -1.0f;
    }

    MeshletBounds bounds;
    StoreFloat(VectorSetW(center, VectorGetX(VectorSqrt(radiusSquared)) * s_MeshletBoundsRadiusInflation), &bounds.sphere);
    bounds.conePacked = PackMeshletCone(coneAxis, coneCutoff);
    return bounds;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

