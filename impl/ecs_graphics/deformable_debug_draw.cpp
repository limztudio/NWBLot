// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deformable_debug_draw.h"

#include <impl/assets_graphics/deformable_geometry_validation.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_deformable_debug_draw{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr Float3U s_ColorHit = Float3U(1.0f, 0.95f, 0.25f);
static constexpr Float3U s_ColorNormal = Float3U(0.2f, 0.45f, 1.0f);
static constexpr Float3U s_ColorTangent = Float3U(1.0f, 0.25f, 0.25f);
static constexpr Float3U s_ColorBitangent = Float3U(0.25f, 1.0f, 0.35f);
static constexpr Float3U s_ColorWall = Float3U(1.0f, 0.68f, 0.18f);
static constexpr Float3U s_ColorAccessory = Float3U(0.2f, 0.95f, 1.0f);
static constexpr Float3U s_ColorInvalid = Float3U(1.0f, 0.0f, 1.0f);
static constexpr Float3U s_ColorRestrictedMask = Float3U(1.0f, 0.52f, 0.08f);
static constexpr Float3U s_ColorRepairMask = Float3U(0.95f, 0.25f, 1.0f);
static constexpr Float3U s_ColorForbiddenMask = Float3U(1.0f, 0.08f, 0.08f);
static constexpr Float3U s_ColorSkin = Float3U(0.65f, 0.35f, 1.0f);
static constexpr Float3U s_ColorMorph = Float3U(1.0f, 0.9f, 0.15f);
static constexpr Float3U s_ColorDisplacement = Float3U(0.1f, 0.85f, 0.9f);
static constexpr f32 s_WallBasisLineLength = 0.08f;
static constexpr f32 s_SkinWeightLineLength = 0.06f;
static constexpr f32 s_MorphDeltaLineScale = 1.0f;
static constexpr f32 s_DisplacementLineScale = 1.0f;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] SIMDVector LoadPoint3(const Float4& value){
    return VectorSetW(LoadFloat(value), 0.0f);
}

[[nodiscard]] SIMDVector ScaleAdd(const SIMDVector begin, const SIMDVector direction, const f32 scale){
    return VectorMultiplyAdd(direction, VectorReplicate(scale), begin);
}

[[nodiscard]] bool FinitePoint(const SIMDVector point){
    return DeformableValidation::FiniteVector(point, 0x7u);
}

[[nodiscard]] u32 SaturateUsizeToU32(const usize value){
    return value > static_cast<usize>(Limit<u32>::s_Max)
        ? Limit<u32>::s_Max
        : static_cast<u32>(value)
    ;
}

[[nodiscard]] f32 Length3(const SIMDVector value){
    const f32 lengthSquared = VectorGetX(Vector3LengthSq(value));
    return IsFinite(lengthSquared)
        ? Sqrt(Max(0.0f, lengthSquared))
        : 0.0f
    ;
}

[[nodiscard]] u32 ActiveSkinInfluenceCount(const SkinInfluence4& skin){
    u32 count = 0u;
    for(u32 influence = 0u; influence < 4u; ++influence){
        if(Abs(skin.weight[influence]) > DeformableValidation::s_Epsilon)
            ++count;
    }
    return count;
}

[[nodiscard]] f32 StrongestSkinWeight(const SkinInfluence4& skin){
    f32 weight = 0.0f;
    for(u32 influence = 0u; influence < 4u; ++influence)
        weight = Max(weight, Abs(skin.weight[influence]));
    return weight;
}

[[nodiscard]] bool ActivePositionDelta(const DeformableMorphDelta& delta){
    return Length3(LoadFloat(delta.deltaPosition)) > DeformableValidation::s_Epsilon;
}

void AppendLine(DeformableSurfaceEditDebugSnapshot& snapshot, const SIMDVector begin, const SIMDVector end, const Float3U& color){
    if(!FinitePoint(begin) || !FinitePoint(end))
        return;

    DeformableDebugLine line;
    StoreFloat(begin, &line.begin);
    StoreFloat(end, &line.end);
    line.color = color;
    snapshot.lines.push_back(line);
}

void AppendPoint(DeformableSurfaceEditDebugSnapshot& snapshot, const SIMDVector position, const Float3U& color, const u32 id){
    if(!FinitePoint(position))
        return;

    DeformableDebugPoint point;
    StoreFloat(position, &point.position);
    point.color = color;
    point.id = id;
    snapshot.points.push_back(point);
}

void AppendWallVertexBasisDebug(
    const DeformableVertexRest& vertex,
    const SIMDVector position,
    DeformableSurfaceEditDebugSnapshot& snapshot)
{
    if(!DeformableValidation::ValidRestVertexFrame(vertex))
        return;

    const SIMDVector normal = VectorSetW(LoadFloat(vertex.normal), 0.0f);
    const SIMDVector tangent = VectorSetW(LoadFloat(vertex.tangent), 0.0f);
    AppendLine(snapshot, position, ScaleAdd(position, normal, s_WallBasisLineLength), s_ColorNormal);
    AppendLine(snapshot, position, ScaleAdd(position, tangent, s_WallBasisLineLength), s_ColorTangent);
    ++snapshot.wallNormalBasisLineCount;
    ++snapshot.wallTangentBasisLineCount;
}

void AppendSkinWeightDebug(const DeformableRuntimeMeshInstance& instance, DeformableSurfaceEditDebugSnapshot& snapshot){
    if(instance.skin.size() != instance.restVertices.size())
        return;

    for(usize vertexIndex = 0u; vertexIndex < instance.skin.size(); ++vertexIndex){
        const SkinInfluence4& skin = instance.skin[vertexIndex];
        if(!DeformableValidation::ValidSkinInfluence(skin))
            continue;

        const f32 weight = StrongestSkinWeight(skin);
        if(!DeformableValidation::ActiveWeight(weight))
            continue;

        const DeformableVertexRest& vertex = instance.restVertices[vertexIndex];
        if(!DeformableValidation::ValidRestVertexFrame(vertex))
            continue;

        const SIMDVector position = LoadFloat(vertex.position);
        const SIMDVector normal = VectorSetW(LoadFloat(vertex.normal), 0.0f);
        AppendLine(snapshot, position, ScaleAdd(position, normal, weight * s_SkinWeightLineLength), s_ColorSkin);
        ++snapshot.skinWeightLineCount;
    }
}

void AppendMorphDeltaDebug(const DeformableRuntimeMeshInstance& instance, DeformableSurfaceEditDebugSnapshot& snapshot){
    for(const DeformableMorph& morph : instance.morphs){
        for(const DeformableMorphDelta& delta : morph.deltas){
            if(delta.vertexId >= instance.restVertices.size() || !ActivePositionDelta(delta))
                continue;

            const SIMDVector position = LoadFloat(instance.restVertices[delta.vertexId].position);
            const SIMDVector deltaPosition = VectorSetW(LoadFloat(delta.deltaPosition), 0.0f);
            if(!FinitePoint(position) || !DeformableValidation::FiniteVector(deltaPosition, 0x7u))
                continue;

            AppendLine(
                snapshot,
                position,
                VectorMultiplyAdd(deltaPosition, VectorReplicate(s_MorphDeltaLineScale), position),
                s_ColorMorph
            );
            ++snapshot.morphDeltaLineCount;
        }
    }
}

void AppendDisplacementMagnitudeDebug(
    const DeformableRuntimeMeshInstance& instance,
    DeformableSurfaceEditDebugSnapshot& snapshot)
{
    if(instance.displacement.mode != DeformableDisplacementMode::ScalarUvRamp)
        return;
    if(!IsFinite(instance.displacement.amplitude) || !IsFinite(instance.displacement.bias))
        return;

    for(const DeformableVertexRest& vertex : instance.restVertices){
        if(!DeformableValidation::ValidRestVertexFrame(vertex))
            continue;

        const f32 scalarOffset = (Max(0.0f, Min(1.0f, vertex.uv0.x)) * instance.displacement.amplitude)
            + instance.displacement.bias
        ;
        if(!IsFinite(scalarOffset))
            continue;

        snapshot.maxDisplacementMagnitude = Max(snapshot.maxDisplacementMagnitude, Abs(scalarOffset));
        if(!DeformableValidation::ActiveWeight(scalarOffset))
            continue;

        const SIMDVector position = LoadFloat(vertex.position);
        const SIMDVector normal = VectorSetW(LoadFloat(vertex.normal), 0.0f);
        AppendLine(
            snapshot,
            position,
            ScaleAdd(position, normal, scalarOffset * s_DisplacementLineScale),
            s_ColorDisplacement
        );
        ++snapshot.displacementMagnitudeLineCount;
    }
}

void AppendPreviewDebug(
    const DeformableSurfaceEditSession* session,
    const DeformableHolePreview* preview,
    DeformableSurfaceEditDebugSnapshot& snapshot)
{
    if(!session || !preview || !preview->valid)
        return;

    snapshot.previewValid = true;
    snapshot.previewTriangle = session->hit.triangle;
    snapshot.previewSourceTriangle = session->hit.restSample.sourceTri;
    snapshot.previewPermission = preview->editPermission;
    const SIMDVector previewCenter = LoadPoint3(preview->center);
    const SIMDVector posedHitPoint = LoadPoint3(session->hit.position);
    const SIMDVector previewNormal = LoadPoint3(preview->normal);
    const SIMDVector previewTangent = LoadPoint3(preview->tangent);
    const SIMDVector previewBitangent = LoadPoint3(preview->bitangent);
    StoreFloat(previewCenter, &snapshot.previewCenter);
    StoreFloat(previewCenter, &snapshot.previewRestHitPoint);
    StoreFloat(posedHitPoint, &snapshot.previewPosedHitPoint);
    StoreFloat(previewNormal, &snapshot.previewNormal);
    StoreFloat(previewTangent, &snapshot.previewTangent);
    StoreFloat(previewBitangent, &snapshot.previewBitangent);
    snapshot.previewBarycentric[0] = session->hit.bary[0];
    snapshot.previewBarycentric[1] = session->hit.bary[1];
    snapshot.previewBarycentric[2] = session->hit.bary[2];
    snapshot.previewRadius = preview->radius;
    snapshot.previewDepth = preview->depth;

    const f32 radius = Max(preview->radius, 0.02f);
    const f32 normalLength = Max(preview->depth, radius * 0.5f);
    AppendPoint(snapshot, posedHitPoint, s_ColorHit, session->hit.triangle);
    AppendLine(snapshot, previewCenter, ScaleAdd(previewCenter, previewTangent, radius), s_ColorTangent);
    AppendLine(snapshot, previewCenter, ScaleAdd(previewCenter, previewBitangent, radius), s_ColorBitangent);
    AppendLine(snapshot, previewCenter, ScaleAdd(previewCenter, previewNormal, normalLength), s_ColorNormal);
}

void AppendWallLoopDebug(
    const DeformableRuntimeMeshInstance& instance,
    const u32 firstWallVertex,
    const u32 wallVertexCount,
    const Float3U& color,
    u32& inOutMarkerCount,
    DeformableSurfaceEditDebugSnapshot& snapshot)
{
    if(firstWallVertex == Limit<u32>::s_Max || wallVertexCount < 3u)
        return;

    const usize first = static_cast<usize>(firstWallVertex);
    const usize count = static_cast<usize>(wallVertexCount);
    if(first > instance.restVertices.size() || count > instance.restVertices.size() - first)
        return;

    inOutMarkerCount += wallVertexCount;
    for(usize i = 0u; i < count; ++i){
        const usize next = (i + 1u) % count;
        const u32 vertexId = static_cast<u32>(first + i);
        const DeformableVertexRest& vertex = instance.restVertices[first + i];
        const SIMDVector position = LoadFloat(vertex.position);
        const SIMDVector nextPosition = LoadFloat(instance.restVertices[first + next].position);
        AppendPoint(snapshot, position, color, vertexId);
        AppendLine(snapshot, position, nextPosition, color);
        AppendWallVertexBasisDebug(vertex, position, snapshot);
    }
}

void AppendEditStateDebug(
    const DeformableRuntimeMeshInstance& instance,
    const DeformableSurfaceEditState* state,
    DeformableSurfaceEditDebugSnapshot& snapshot)
{
    if(!state)
        return;

    for(const DeformableSurfaceEditRecord& edit : state->edits){
        if(snapshot.removedTriangleCount > Limit<u32>::s_Max - edit.result.removedTriangleCount)
            snapshot.removedTriangleCount = Limit<u32>::s_Max;
        else
            snapshot.removedTriangleCount += edit.result.removedTriangleCount;

        AppendWallLoopDebug(
            instance,
            edit.result.firstWallVertex,
            edit.result.wallVertexCount,
            s_ColorWall,
            snapshot.wallVertexCount,
            snapshot
        );
    }
    for(const DeformableAccessoryAttachmentRecord& accessory : state->accessories){
        AppendWallLoopDebug(
            instance,
            accessory.firstWallVertex,
            accessory.wallVertexCount,
            s_ColorAccessory,
            snapshot.accessoryAnchorCount,
            snapshot
        );
    }
}

[[nodiscard]] SIMDVector TriangleCenter(const DeformableRuntimeMeshInstance& instance, const u32 a, const u32 b, const u32 c){
    if(a >= instance.restVertices.size()
        || b >= instance.restVertices.size()
        || c >= instance.restVertices.size()
    )
        return VectorZero();

    return VectorScale(
        VectorAdd(
            VectorAdd(LoadFloat(instance.restVertices[a].position), LoadFloat(instance.restVertices[b].position)),
            LoadFloat(instance.restVertices[c].position)
        ),
        1.0f / 3.0f
    );
}

void AppendInvalidTriangleDebug(const DeformableRuntimeMeshInstance& instance, DeformableSurfaceEditDebugSnapshot& snapshot){
    const usize triangleCount = instance.indices.size() / 3u;
    for(usize triangle = 0u; triangle < triangleCount; ++triangle){
        const usize indexBase = triangle * 3u;
        const u32 a = instance.indices[indexBase + 0u];
        const u32 b = instance.indices[indexBase + 1u];
        const u32 c = instance.indices[indexBase + 2u];
        if(DeformableValidation::ValidTriangle(instance.restVertices, a, b, c))
            continue;

        ++snapshot.invalidTriangleCount;
        AppendPoint(snapshot, TriangleCenter(instance, a, b, c), s_ColorInvalid, static_cast<u32>(triangle));
    }
}

void AppendEditMaskMarker(
    const DeformableRuntimeMeshInstance& instance,
    const usize triangle,
    const DeformableEditMaskFlags flags,
    DeformableSurfaceEditDebugSnapshot& snapshot)
{
    if(flags == s_DeformableEditMaskDefault)
        return;

    const usize indexBase = triangle * 3u;
    const u32 a = instance.indices[indexBase + 0u];
    const u32 b = instance.indices[indexBase + 1u];
    const u32 c = instance.indices[indexBase + 2u];
    const u32 markerId = static_cast<u32>(triangle);
    if((flags & DeformableEditMaskFlag::Forbidden) != 0u){
        AppendPoint(snapshot, TriangleCenter(instance, a, b, c), s_ColorForbiddenMask, markerId);
        ++snapshot.forbiddenMaskPointCount;
    }
    else if((flags & DeformableEditMaskFlag::RequiresRepair) != 0u){
        AppendPoint(snapshot, TriangleCenter(instance, a, b, c), s_ColorRepairMask, markerId);
        ++snapshot.repairMaskPointCount;
    }
    else if((flags & DeformableEditMaskFlag::Restricted) != 0u){
        AppendPoint(snapshot, TriangleCenter(instance, a, b, c), s_ColorRestrictedMask, markerId);
        ++snapshot.restrictedMaskPointCount;
    }
}

void CountEditMasks(const DeformableRuntimeMeshInstance& instance, DeformableSurfaceEditDebugSnapshot& snapshot){
    const usize triangleCount = instance.indices.size() / 3u;
    for(usize triangle = 0u; triangle < triangleCount; ++triangle){
        const DeformableEditMaskFlags flags =
            ResolveDeformableTriangleEditMask(instance, static_cast<u32>(triangle))
        ;
        if(!ValidDeformableEditMaskFlags(flags)){
            ++snapshot.invalidTriangleCount;
            continue;
        }
        if((flags & DeformableEditMaskFlag::Forbidden) != 0u)
            ++snapshot.forbiddenTriangleCount;
        else if((flags & (DeformableEditMaskFlag::Restricted | DeformableEditMaskFlag::RequiresRepair)) != 0u)
            ++snapshot.restrictedTriangleCount;
        else
            ++snapshot.editableTriangleCount;
        AppendEditMaskMarker(instance, triangle, flags, snapshot);
    }
}

void AppendPayloadDiagnostics(const DeformableRuntimeMeshInstance& instance, DeformableSurfaceEditDebugSnapshot& snapshot){
    for(const DeformableVertexRest& vertex : instance.restVertices){
        if(!DeformableValidation::ValidRestVertexFrame(vertex))
            ++snapshot.invalidFrameCount;
    }

    for(const SkinInfluence4& skin : instance.skin){
        const u32 activeInfluenceCount = ActiveSkinInfluenceCount(skin);
        if(activeInfluenceCount != 0u)
            ++snapshot.skinnedVertexCount;
        snapshot.maxSkinInfluenceCount = Max(snapshot.maxSkinInfluenceCount, activeInfluenceCount);
        if(DeformableValidation::ValidSkinInfluence(skin))
            snapshot.maxSkinWeight = Max(snapshot.maxSkinWeight, StrongestSkinWeight(skin));
    }

    snapshot.morphCount = SaturateUsizeToU32(instance.morphs.size());
    for(const DeformableMorph& morph : instance.morphs){
        const usize remainingDeltaCapacity =
            static_cast<usize>(Limit<u32>::s_Max) - static_cast<usize>(snapshot.morphDeltaCount)
        ;
        snapshot.morphDeltaCount += SaturateUsizeToU32(Min(morph.deltas.size(), remainingDeltaCapacity));
        for(const DeformableMorphDelta& delta : morph.deltas){
            snapshot.maxMorphPositionDelta = Max(
                snapshot.maxMorphPositionDelta,
                Length3(LoadFloat(delta.deltaPosition))
            );
        }
    }

    snapshot.displacementMode = instance.displacement.mode;
    snapshot.displacementAmplitude = instance.displacement.amplitude;
    snapshot.displacementBias = instance.displacement.bias;
    snapshot.displacementTextureBound = instance.displacement.texture.valid();
    AppendSkinWeightDebug(instance, snapshot);
    AppendMorphDeltaDebug(instance, snapshot);
    AppendDisplacementMagnitudeDebug(instance, snapshot);
}

[[nodiscard]] const char* PermissionText(const DeformableSurfaceEditPermission::Enum permission){
    switch(permission){
    case DeformableSurfaceEditPermission::Restricted:
        return "restricted";
    case DeformableSurfaceEditPermission::Forbidden:
        return "forbidden";
    case DeformableSurfaceEditPermission::Allowed:
    default:
        return "allowed";
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool BuildDeformableSurfaceEditDebugSnapshot(
    const DeformableRuntimeMeshInstance& instance,
    const DeformableSurfaceEditSession* session,
    const DeformableHolePreview* preview,
    const DeformableSurfaceEditState* state,
    DeformableSurfaceEditDebugSnapshot& outSnapshot)
{
    using namespace __hidden_deformable_debug_draw;

    outSnapshot = DeformableSurfaceEditDebugSnapshot{};
    if(instance.indices.empty() || (instance.indices.size() % 3u) != 0u)
        return false;
    if(instance.restVertices.size() > static_cast<usize>(Limit<u32>::s_Max)
        || instance.indices.size() / 3u > static_cast<usize>(Limit<u32>::s_Max)
    )
        return false;

    outSnapshot.entity = instance.entity;
    outSnapshot.runtimeMesh = instance.handle;
    outSnapshot.editRevision = instance.editRevision;
    outSnapshot.vertexCount = static_cast<u32>(instance.restVertices.size());
    outSnapshot.triangleCount = static_cast<u32>(instance.indices.size() / 3u);
    outSnapshot.sourceTriangleCount = instance.sourceTriangleCount;

    AppendPayloadDiagnostics(instance, outSnapshot);
    CountEditMasks(instance, outSnapshot);
    AppendInvalidTriangleDebug(instance, outSnapshot);
    AppendPreviewDebug(session, preview, outSnapshot);
    AppendEditStateDebug(instance, state, outSnapshot);
    return true;
}

bool BuildDeformableSurfaceEditDebugDump(const DeformableSurfaceEditDebugSnapshot& snapshot, AString& outDump){
    using namespace __hidden_deformable_debug_draw;

    outDump = StringFormat(
        "deformable_debug_snapshot entity={} runtime_mesh={} revision={} vertices={} triangles={} "
        "source_triangles={} invalid_frames={} skin_vertices={} max_skin_influences={} "
        "skin_weight_lines={} max_skin_weight={} morphs={} morph_deltas={} morph_delta_lines={} "
        "max_morph_position_delta={} displacement_mode={} displacement_amplitude={} displacement_bias={} "
        "displacement_texture={} displacement_lines={} max_displacement_magnitude={} "
        "masks(editable={} restricted={} forbidden={}) mask_markers(restricted={} repair={} forbidden={}) "
        "invalid_triangles={} removed_triangles={} "
        "wall_vertices={} accessory_anchors={} wall_basis(normal={} tangent={}) preview={} lines={} points={}\n",
        snapshot.entity.id,
        snapshot.runtimeMesh.value,
        snapshot.editRevision,
        snapshot.vertexCount,
        snapshot.triangleCount,
        snapshot.sourceTriangleCount,
        snapshot.invalidFrameCount,
        snapshot.skinnedVertexCount,
        snapshot.maxSkinInfluenceCount,
        snapshot.skinWeightLineCount,
        snapshot.maxSkinWeight,
        snapshot.morphCount,
        snapshot.morphDeltaCount,
        snapshot.morphDeltaLineCount,
        snapshot.maxMorphPositionDelta,
        snapshot.displacementMode,
        snapshot.displacementAmplitude,
        snapshot.displacementBias,
        snapshot.displacementTextureBound ? "yes" : "no",
        snapshot.displacementMagnitudeLineCount,
        snapshot.maxDisplacementMagnitude,
        snapshot.editableTriangleCount,
        snapshot.restrictedTriangleCount,
        snapshot.forbiddenTriangleCount,
        snapshot.restrictedMaskPointCount,
        snapshot.repairMaskPointCount,
        snapshot.forbiddenMaskPointCount,
        snapshot.invalidTriangleCount,
        snapshot.removedTriangleCount,
        snapshot.wallVertexCount,
        snapshot.accessoryAnchorCount,
        snapshot.wallNormalBasisLineCount,
        snapshot.wallTangentBasisLineCount,
        snapshot.previewValid ? "yes" : "no",
        snapshot.lines.size(),
        snapshot.points.size()
    );
    if(snapshot.previewValid){
        outDump += StringFormat(
            "preview triangle={} source_triangle={} bary=({}, {}, {}) radius={} depth={} "
            "permission={} center=({}, {}, {}) rest_hit=({}, {}, {}) posed_hit=({}, {}, {}) "
            "normal=({}, {}, {}) tangent=({}, {}, {}) bitangent=({}, {}, {})\n",
            snapshot.previewTriangle,
            snapshot.previewSourceTriangle,
            snapshot.previewBarycentric[0],
            snapshot.previewBarycentric[1],
            snapshot.previewBarycentric[2],
            snapshot.previewRadius,
            snapshot.previewDepth,
            PermissionText(snapshot.previewPermission),
            snapshot.previewCenter.x,
            snapshot.previewCenter.y,
            snapshot.previewCenter.z,
            snapshot.previewRestHitPoint.x,
            snapshot.previewRestHitPoint.y,
            snapshot.previewRestHitPoint.z,
            snapshot.previewPosedHitPoint.x,
            snapshot.previewPosedHitPoint.y,
            snapshot.previewPosedHitPoint.z,
            snapshot.previewNormal.x,
            snapshot.previewNormal.y,
            snapshot.previewNormal.z,
            snapshot.previewTangent.x,
            snapshot.previewTangent.y,
            snapshot.previewTangent.z,
            snapshot.previewBitangent.x,
            snapshot.previewBitangent.y,
            snapshot.previewBitangent.z
        );
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

