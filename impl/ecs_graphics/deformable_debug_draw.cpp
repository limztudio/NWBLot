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
    const SIMDVector previewNormal = LoadPoint3(preview->normal);
    const SIMDVector previewTangent = LoadPoint3(preview->tangent);
    const SIMDVector previewBitangent = LoadPoint3(preview->bitangent);
    StoreFloat(previewCenter, &snapshot.previewCenter);
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
    AppendPoint(snapshot, LoadPoint3(session->hit.position), s_ColorHit, session->hit.triangle);
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
        const SIMDVector position = LoadFloat(instance.restVertices[first + i].position);
        const SIMDVector nextPosition = LoadFloat(instance.restVertices[first + next].position);
        AppendPoint(snapshot, position, color, vertexId);
        AppendLine(snapshot, position, nextPosition, color);
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
        "deformable_debug_snapshot entity={} runtime_mesh={} revision={} vertices={} triangles={} source_triangles={} invalid_frames={} skin_vertices={} max_skin_influences={} morphs={} morph_deltas={} max_morph_position_delta={} displacement_mode={} displacement_amplitude={} displacement_bias={} displacement_texture={} masks(editable={} restricted={} forbidden={}) invalid_triangles={} wall_vertices={} accessory_anchors={} preview={} lines={} points={}\n",
        snapshot.entity.id,
        snapshot.runtimeMesh.value,
        snapshot.editRevision,
        snapshot.vertexCount,
        snapshot.triangleCount,
        snapshot.sourceTriangleCount,
        snapshot.invalidFrameCount,
        snapshot.skinnedVertexCount,
        snapshot.maxSkinInfluenceCount,
        snapshot.morphCount,
        snapshot.morphDeltaCount,
        snapshot.maxMorphPositionDelta,
        snapshot.displacementMode,
        snapshot.displacementAmplitude,
        snapshot.displacementBias,
        snapshot.displacementTextureBound ? "yes" : "no",
        snapshot.editableTriangleCount,
        snapshot.restrictedTriangleCount,
        snapshot.forbiddenTriangleCount,
        snapshot.invalidTriangleCount,
        snapshot.wallVertexCount,
        snapshot.accessoryAnchorCount,
        snapshot.previewValid ? "yes" : "no",
        snapshot.lines.size(),
        snapshot.points.size()
    );
    if(snapshot.previewValid){
        outDump += StringFormat(
            "preview triangle={} source_triangle={} bary=({}, {}, {}) radius={} depth={} permission={} center=({}, {}, {}) normal=({}, {}, {}) tangent=({}, {}, {}) bitangent=({}, {}, {})\n",
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

