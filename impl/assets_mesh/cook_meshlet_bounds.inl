// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CookEntryT, typename CallbackT>
static void ForEachMeshletFaceNormalVector(const CookEntryT& entry, const MeshletDesc& meshlet, CallbackT callback){
    for(u32 primitiveIndex = 0u; primitiveIndex < MeshletPrimitiveCount(meshlet); ++primitiveIndex){
        const usize primitiveOffset = meshlet.primitiveOffset + static_cast<usize>(primitiveIndex) * 3u;
        const u8 localVertex0 = entry.meshletPrimitiveIndices[primitiveOffset + 0u];
        const u8 localVertex1 = entry.meshletPrimitiveIndices[primitiveOffset + 1u];
        const u8 localVertex2 = entry.meshletPrimitiveIndices[primitiveOffset + 2u];
        const SIMDVector p0 = LoadMeshletLocalPositionVector(entry, meshlet, localVertex0);
        const SIMDVector p1 = LoadMeshletLocalPositionVector(entry, meshlet, localVertex1);
        const SIMDVector p2 = LoadMeshletLocalPositionVector(entry, meshlet, localVertex2);
        callback(BuildMeshletFaceNormal(p0, p1, p2));
    }
}

template<typename CookEntryT>
static MeshletBounds BuildMeshletBounds(const CookEntryT& entry, const MeshletDesc& meshlet){
    SIMDVector minBounds = VectorReplicate(Limit<f32>::s_Max);
    SIMDVector maxBounds = VectorReplicate(-Limit<f32>::s_Max);
    for(u32 localPositionIndex = 0u; localPositionIndex < MeshletPositionCount(meshlet); ++localPositionIndex){
        const MeshletPositionStreamRef& ref = entry.meshletPositionStreamRefs[meshlet.positionRefOffset + localPositionIndex];
        const SIMDVector position = LoadMeshletPositionVector(entry, ref);
        minBounds = VectorMin(minBounds, position);
        maxBounds = VectorMax(maxBounds, position);
    }

    const SIMDVector center = VectorSetW(VectorScale(VectorAdd(minBounds, maxBounds), 0.5f), 0.0f);
    SIMDVector radiusSquared = VectorZero();
    for(u32 localPositionIndex = 0u; localPositionIndex < MeshletPositionCount(meshlet); ++localPositionIndex){
        const MeshletPositionStreamRef& ref = entry.meshletPositionStreamRefs[meshlet.positionRefOffset + localPositionIndex];
        const SIMDVector delta = VectorSubtract(LoadMeshletPositionVector(entry, ref), center);
        radiusSquared = VectorMax(radiusSquared, Vector3LengthSq(delta));
    }

    SIMDVector areaWeightedNormal = VectorZero();
    u32 validFaceNormalCount = 0u;
    ForEachMeshletFaceNormalVector(entry, meshlet, [&](const SIMDVector faceNormal){
        if(!Core::Mesh::FrameValidDirection(faceNormal))
            return;

        areaWeightedNormal = VectorAdd(areaWeightedNormal, faceNormal);
        ++validFaceNormalCount;
    });

    const SIMDVector coneAxis = NormalizeMeshletDirectionOrZero(areaWeightedNormal);
    f32 coneCutoff = -1.0f;
    if(validFaceNormalCount == MeshletPrimitiveCount(meshlet) && Core::Mesh::FrameValidDirection(coneAxis)){
        coneCutoff = 1.0f;
        ForEachMeshletFaceNormalVector(entry, meshlet, [&](const SIMDVector meshletFaceNormal){
            const SIMDVector faceNormal = NormalizeMeshletDirectionOrZero(meshletFaceNormal);
            if(Core::Mesh::FrameValidDirection(faceNormal))
                coneCutoff = Min(coneCutoff, VectorGetX(Vector3Dot(coneAxis, faceNormal)));
        });
        if(coneCutoff <= 0.0f)
            coneCutoff = -1.0f;
    }

    MeshletBounds bounds;
    StoreFloat(VectorSetW(center, VectorGetX(VectorSqrt(radiusSquared))), &bounds.sphere);
    bounds.conePacked = PackMeshletCone(coneAxis, coneCutoff);
    return bounds;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

