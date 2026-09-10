// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deform_edit.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_deform_edit{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using ScratchArena = Core::Alloc::ScratchArena;

inline constexpr f32 s_MinEpsilon = 0.0000001f;
inline constexpr usize s_MaxDeformVertices = 1u << 20u;
inline constexpr usize s_MaxDeformTriangles = 1u << 20u;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool FiniteFloat(const f32 value){
    return value == value && value != Limit<f32>::s_Infinity && value != -Limit<f32>::s_Infinity;
}

[[nodiscard]] bool FiniteVertex(const CsgDeformVertex& vertex){
    return FiniteFloat(vertex.position.x)
        && FiniteFloat(vertex.position.y)
        && FiniteFloat(vertex.position.z)
        && FiniteFloat(vertex.normal.x)
        && FiniteFloat(vertex.normal.y)
        && FiniteFloat(vertex.normal.z)
        && FiniteFloat(vertex.normal.w)
        && FiniteFloat(vertex.tangent.x)
        && FiniteFloat(vertex.tangent.y)
        && FiniteFloat(vertex.tangent.z)
        && FiniteFloat(vertex.tangent.w)
        && FiniteFloat(vertex.uv0.x)
        && FiniteFloat(vertex.uv0.y)
        && FiniteFloat(vertex.color.x)
        && FiniteFloat(vertex.color.y)
        && FiniteFloat(vertex.color.z)
        && FiniteFloat(vertex.color.w)
    ;
}

[[nodiscard]] f32 SaturateFloat(const f32 value){
    if(value < 0.0f)
        return 0.0f;
    if(value > 1.0f)
        return 1.0f;
    return value;
}

[[nodiscard]] f32 ShapeEpsilon(const CsgDeformBuildOptions& options){
    return options.distanceEpsilon > s_MinEpsilon ? options.distanceEpsilon : s_MinEpsilon;
}

[[nodiscard]] bool ValidOptions(const CsgDeformBuildOptions& options){
    return options.distanceEpsilon > 0.0f && options.distanceEpsilon < 1.0f;
}

[[nodiscard]] bool ValidTopology(
    const CsgDeformVertex* vertices,
    const usize vertexCount,
    const CsgDeformTriangle* triangles,
    const usize triangleCount
){
    if(!vertices || !triangles)
        return false;
    for(usize triangleIndex = 0u; triangleIndex < triangleCount; ++triangleIndex){
        const CsgDeformTriangle& triangle = triangles[triangleIndex];
        for(usize corner = 0u; corner < 3u; ++corner){
            if(triangle.indices[corner] >= vertexCount)
                return false;
        }
    }
    return true;
}

[[nodiscard]] bool FiniteInput(
    const CsgDeformVertex* vertices,
    const usize vertexCount,
    CsgDeformViabilityReason::Enum& outReason
){
    outReason = CsgDeformViabilityReason::Ok;
    if(!vertices){
        outReason = CsgDeformViabilityReason::NonFiniteInput;
        return false;
    }
    for(usize vertexIndex = 0u; vertexIndex < vertexCount; ++vertexIndex){
        if(!FiniteVertex(vertices[vertexIndex])){
            outReason = CsgDeformViabilityReason::NonFiniteInput;
            return false;
        }
    }
    return true;
}

[[nodiscard]] f32 PlaneSignedDistance(const SIMDVector shapePosition, const Float4& parameter0){
    const f32 nx = parameter0.x;
    const f32 ny = parameter0.y;
    const f32 nz = parameter0.z;
    f32 position[4u] = {};
    SIMDConvertDetail::StoreF32(position, shapePosition);
    return position[0u] * nx + position[1u] * ny + position[2u] * nz + parameter0.w;
}

[[nodiscard]] f32 BoxSignedDistance(const SIMDVector shapePosition, const Float4& parameter0){
    f32 position[4u] = {};
    SIMDConvertDetail::StoreF32(position, shapePosition);
    const f32 qx = Abs(position[0u]) - parameter0.x;
    const f32 qy = Abs(position[1u]) - parameter0.y;
    const f32 qz = Abs(position[2u]) - parameter0.z;
    const f32 outsideX = qx > 0.0f ? qx : 0.0f;
    const f32 outsideY = qy > 0.0f ? qy : 0.0f;
    const f32 outsideZ = qz > 0.0f ? qz : 0.0f;
    const f32 outside = Sqrt(outsideX * outsideX + outsideY * outsideY + outsideZ * outsideZ);
    const f32 inside = Min(qx, Min(qy, qz)) < 0.0f ? Min(qx, Min(qy, qz)) : 0.0f;
    return outside + inside;
}

[[nodiscard]] f32 SphereSignedDistance(const SIMDVector shapePosition, const Float4& parameter0){
    f32 position[4u] = {};
    SIMDConvertDetail::StoreF32(position, shapePosition);
    return Sqrt(position[0u] * position[0u] + position[1u] * position[1u] + position[2u] * position[2u]) - parameter0.x;
}

[[nodiscard]] f32 CapsuleSignedDistance(const SIMDVector shapePosition, const Float4& parameter0){
    f32 position[4u] = {};
    SIMDConvertDetail::StoreF32(position, shapePosition);
    const f32 halfHeight = parameter0.y;
    const f32 clampedY = position[1u] < -halfHeight ? -halfHeight : (position[1u] > halfHeight ? halfHeight : position[1u]);
    const f32 dx = position[0u];
    const f32 dy = position[1u] - clampedY;
    const f32 dz = position[2u];
    return Sqrt(dx * dx + dy * dy + dz * dz) - parameter0.x;
}

[[nodiscard]] bool ShapeDistances(
    const CsgDeformShape& shape,
    const CsgDeformVertexVector<ScratchArena>& vertices,
    const f32 epsilon,
    Vector<f32, ScratchArena>& outDistances,
    CsgDeformViabilityReason::Enum& outReason
){
    outReason = CsgDeformViabilityReason::Ok;
    if(!shape.shapeType){
        outReason = CsgDeformViabilityReason::InvalidCutter;
        return false;
    }
    const usize vertexCount = vertices.size();
    outDistances.clear();
    outDistances.resize(vertexCount, 0.0f);

    const SIMDMatrix worldToShape = LoadFloat(shape.worldToShape);
    for(usize vertexIndex = 0u; vertexIndex < vertexCount; ++vertexIndex){
        const CsgDeformVertex& vertex = vertices[vertexIndex];
        const SIMDVector worldPosition = VectorSet(vertex.position.x, vertex.position.y, vertex.position.z, 1.0f);
        const SIMDVector shapePosition = Vector4Transform(worldPosition, worldToShape);

        f32 distance = 0.0f;
        if(shape.shapeType == Name("engine/csg/plane"))
            distance = PlaneSignedDistance(shapePosition, shape.parameter0);
        else if(shape.shapeType == Name("engine/csg/box"))
            distance = BoxSignedDistance(shapePosition, shape.parameter0);
        else if(shape.shapeType == Name("engine/csg/sphere"))
            distance = SphereSignedDistance(shapePosition, shape.parameter0);
        else if(shape.shapeType == Name("engine/csg/capsule"))
            distance = CapsuleSignedDistance(shapePosition, shape.parameter0);
        else{
            outReason = CsgDeformViabilityReason::InvalidCutter;
            return false;
        }

        if(!FiniteFloat(distance)){
            outReason = CsgDeformViabilityReason::NonFiniteInput;
            return false;
        }
        static_cast<void>(epsilon);
        outDistances[vertexIndex] = distance;
    }
    return true;
}

// Canonical edge id keeps (a,b) and (b,a) identical without hashing pointers.
[[nodiscard]] u64 EdgeKey(const u32 first, const u32 second){
    const u32 lo = first < second ? first : second;
    const u32 hi = first < second ? second : first;
    return (static_cast<u64>(lo) << 32u) | static_cast<u64>(hi);
}

struct EdgeSplitRecord{
    u64 edge = 0u;
    u32 vertex = 0u;
};

struct EdgeSplitKeyHash{
    [[nodiscard]] usize operator()(const u64 key)const noexcept{ return static_cast<usize>(key ^ (key >> 33u)); }
};

using EdgeSplitMap = HashMap<u64, u32, EdgeSplitKeyHash, EqualTo<u64>, ScratchArena>;

[[nodiscard]] CsgDeformVertex MixVertices(const CsgDeformVertex& first, const CsgDeformVertex& second, const f32 firstWeight){
    const f32 blend = SaturateFloat(firstWeight);
    const f32 other = 1.0f - blend;
    CsgDeformVertex mixed;
    mixed.position = Float3U(
        first.position.x * blend + second.position.x * other,
        first.position.y * blend + second.position.y * other,
        first.position.z * blend + second.position.z * other
    );
    mixed.normal = Float4(
        first.normal.x * blend + second.normal.x * other,
        first.normal.y * blend + second.normal.y * other,
        first.normal.z * blend + second.normal.z * other,
        first.normal.w * blend + second.normal.w * other
    );
    mixed.tangent = Float4(
        first.tangent.x * blend + second.tangent.x * other,
        first.tangent.y * blend + second.tangent.y * other,
        first.tangent.z * blend + second.tangent.z * other,
        first.tangent.w * blend + second.tangent.w * other
    );
    mixed.uv0 = Float2U(
        first.uv0.x * blend + second.uv0.x * other,
        first.uv0.y * blend + second.uv0.y * other
    );
    mixed.color = Float4(
        first.color.x * blend + second.color.x * other,
        first.color.y * blend + second.color.y * other,
        first.color.z * blend + second.color.z * other,
        first.color.w * blend + second.color.w * other
    );
    return mixed;
}

[[nodiscard]] bool NormalizeDeformVertex(CsgDeformVertex& vertex){
    const f32 nx = vertex.normal.x;
    const f32 ny = vertex.normal.y;
    const f32 nz = vertex.normal.z;
    const f32 normalLength = Sqrt(nx * nx + ny * ny + nz * nz);
    if(normalLength > 0.000001f){
        vertex.normal.x = nx / normalLength;
        vertex.normal.y = ny / normalLength;
        vertex.normal.z = nz / normalLength;
    }
    else{
        vertex.normal.x = 0.0f;
        vertex.normal.y = 1.0f;
        vertex.normal.z = 0.0f;
    }
    const f32 tx = vertex.tangent.x;
    const f32 ty = vertex.tangent.y;
    const f32 tz = vertex.tangent.z;
    const f32 tangentLength = Sqrt(tx * tx + ty * ty + tz * tz);
    if(tangentLength > 0.000001f){
        const f32 handedness = vertex.tangent.w < 0.0f ? -1.0f : 1.0f;
        vertex.tangent.x = tx / tangentLength;
        vertex.tangent.y = ty / tangentLength;
        vertex.tangent.z = tz / tangentLength;
        vertex.tangent.w = handedness;
    }
    else{
        vertex.tangent.x = 1.0f;
        vertex.tangent.y = 0.0f;
        vertex.tangent.z = 0.0f;
        vertex.tangent.w = 1.0f;
    }
    return FiniteVertex(vertex);
}

[[nodiscard]] bool SplitEdgeVertex(
    CsgDeformVertexVector<ScratchArena>& vertices,
    EdgeSplitMap& edgeSplits,
    const u32 first,
    const u32 second,
    const f32 firstDistance,
    const f32 secondDistance,
    u32& outVertex
){
    outVertex = 0u;
    if(vertices.size() + 1u > s_MaxDeformVertices)
        return false;
    const u64 edge = EdgeKey(first, second);
    const auto found = edgeSplits.find(edge);
    if(found != edgeSplits.end()){
        outVertex = found.value();
        return true;
    }
    const f32 denominator = firstDistance - secondDistance;
    if(!FiniteFloat(denominator) || Abs(denominator) < 0.0000001f)
        return false;
    // firstWeight lands on second when secondDistance is zero.
    const f32 firstWeight = SaturateFloat(Abs(secondDistance / denominator));
    CsgDeformVertex mixed = MixVertices(vertices[first], vertices[second], firstWeight);
    if(!NormalizeDeformVertex(mixed))
        return false;
    const u32 created = static_cast<u32>(vertices.size());
    vertices.push_back(mixed);
    edgeSplits.emplace(edge, created);
    outVertex = created;
    return true;
}

void EmitTriangle(
    CsgDeformTriangleVector<ScratchArena>& triangles,
    const u32 first,
    const u32 second,
    const u32 third
){
    CsgDeformTriangle triangle;
    triangle.indices[0u] = first;
    triangle.indices[1u] = second;
    triangle.indices[2u] = third;
    triangles.push_back(triangle);
}

// Keep side is distance >= 0; caller snaps |distance| <= epsilon to zero first.
[[nodiscard]] bool ClipShell(
    ScratchArena& scratchArena,
    const CsgDeformShape& shape,
    const f32 epsilon,
    CsgDeformVertexVector<ScratchArena>& inOutVertices,
    CsgDeformTriangleVector<ScratchArena>& inOutTriangles,
    CsgDeformTriangleVector<ScratchArena>& scratchKept,
    Vector<f32, ScratchArena>& scratchDistances,
    CsgDeformViabilityReason::Enum& outReason
){
    outReason = CsgDeformViabilityReason::Ok;
    if(!ShapeDistances(shape, inOutVertices, epsilon, scratchDistances, outReason))
        return false;
    for(f32& distance : scratchDistances){
        if(Abs(distance) <= epsilon)
            distance = 0.0f;
    }

    EdgeSplitMap edgeSplits(0, EdgeSplitKeyHash(), EqualTo<u64>(), scratchArena);
    edgeSplits.reserve(inOutTriangles.size() * 3u + 1u);
    scratchKept.clear();
    scratchKept.reserve(inOutTriangles.size() * 2u + 1u);

    const usize triangleCount = inOutTriangles.size();
    for(usize triangleIndex = 0u; triangleIndex < triangleCount; ++triangleIndex){
        const CsgDeformTriangle triangle = inOutTriangles[triangleIndex];
        if(
            triangle.indices[0u] >= inOutVertices.size()
            || triangle.indices[1u] >= inOutVertices.size()
            || triangle.indices[2u] >= inOutVertices.size()
        ){
            outReason = CsgDeformViabilityReason::InvalidTopology;
            return false;
        }
        const f32 distances[3u] = {
            scratchDistances[triangle.indices[0u]],
            scratchDistances[triangle.indices[1u]],
            scratchDistances[triangle.indices[2u]],
        };
        const bool kept[3u] = { distances[0u] >= 0.0f, distances[1u] >= 0.0f, distances[2u] >= 0.0f };
        const u32 keepCount = (kept[0u] ? 1u : 0u) + (kept[1u] ? 1u : 0u) + (kept[2u] ? 1u : 0u);
        if(keepCount == 3u){
            EmitTriangle(scratchKept, triangle.indices[0u], triangle.indices[1u], triangle.indices[2u]);
            continue;
        }
        if(keepCount == 0u)
            continue;
        if(keepCount == 1u){
            u32 keepCorner = 0u;
            for(u32 corner = 0u; corner < 3u; ++corner){
                if(kept[corner]){
                    keepCorner = corner;
                    break;
                }
            }
            const u32 keepVertex = triangle.indices[keepCorner];
            const u32 dropA = triangle.indices[(keepCorner + 1u) % 3u];
            const u32 dropB = triangle.indices[(keepCorner + 2u) % 3u];
            const f32 keepDistance = distances[keepCorner];
            const f32 dropDistanceA = distances[(keepCorner + 1u) % 3u];
            const f32 dropDistanceB = distances[(keepCorner + 2u) % 3u];
            u32 splitA = 0u;
            u32 splitB = 0u;
            if(!SplitEdgeVertex(inOutVertices, edgeSplits, keepVertex, dropA, keepDistance, dropDistanceA, splitA))
                return false;
            if(!SplitEdgeVertex(inOutVertices, edgeSplits, keepVertex, dropB, keepDistance, dropDistanceB, splitB))
                return false;
            EmitTriangle(scratchKept, keepVertex, splitA, splitB);
            continue;
        }
        u32 dropCorner = 0u;
        for(u32 corner = 0u; corner < 3u; ++corner){
            if(!kept[corner]){
                dropCorner = corner;
                break;
            }
        }
        const u32 dropVertex = triangle.indices[dropCorner];
        const u32 keepA = triangle.indices[(dropCorner + 1u) % 3u];
        const u32 keepB = triangle.indices[(dropCorner + 2u) % 3u];
        const f32 dropDistance = distances[dropCorner];
        const f32 keepDistanceA = distances[(dropCorner + 1u) % 3u];
        const f32 keepDistanceB = distances[(dropCorner + 2u) % 3u];
        u32 splitA = 0u;
        u32 splitB = 0u;
        if(!SplitEdgeVertex(inOutVertices, edgeSplits, keepA, dropVertex, keepDistanceA, dropDistance, splitA))
            return false;
        if(!SplitEdgeVertex(inOutVertices, edgeSplits, keepB, dropVertex, keepDistanceB, dropDistance, splitB))
            return false;
        EmitTriangle(scratchKept, keepA, splitA, splitB);
        EmitTriangle(scratchKept, keepA, splitB, keepB);
        if(scratchKept.size() > s_MaxDeformTriangles)
            return false;
    }
    inOutTriangles = scratchKept;
    return true;
}

struct CutLoopEdge{
    u32 first = 0u;
    u32 second = 0u;
};

[[nodiscard]] bool CollectBoundaryEdges(
    ScratchArena& scratchArena,
    const CsgDeformVertexVector<ScratchArena>& vertices,
    const CsgDeformTriangleVector<ScratchArena>& triangles,
    Vector<CutLoopEdge, ScratchArena>& outEdges
){
    static_cast<void>(vertices);
    outEdges.clear();
    if(triangles.empty())
        return true;
    HashMap<u64, u32, EdgeSplitKeyHash, EqualTo<u64>, ScratchArena> edgeUses(0, EdgeSplitKeyHash(), EqualTo<u64>(), scratchArena);
    edgeUses.reserve(triangles.size() * 3u + 1u);
    for(const CsgDeformTriangle& triangle : triangles){
        const u64 edges[3u] = {
            EdgeKey(triangle.indices[0u], triangle.indices[1u]),
            EdgeKey(triangle.indices[1u], triangle.indices[2u]),
            EdgeKey(triangle.indices[2u], triangle.indices[0u]),
        };
        for(u32 corner = 0u; corner < 3u; ++corner){
            const auto found = edgeUses.find(edges[corner]);
            if(found == edgeUses.end())
                edgeUses.emplace(edges[corner], 1u);
            else
                found.value() = found.value() + 1u;
        }
    }
    outEdges.reserve(triangles.size());
    for(const CsgDeformTriangle& triangle : triangles){
        const u32 corners[3u][2u] = {
            { triangle.indices[0u], triangle.indices[1u] },
            { triangle.indices[1u], triangle.indices[2u] },
            { triangle.indices[2u], triangle.indices[0u] },
        };
        for(u32 corner = 0u; corner < 3u; ++corner){
            const auto found = edgeUses.find(EdgeKey(corners[corner][0u], corners[corner][1u]));
            if(found != edgeUses.end() && found.value() == 1u)
                outEdges.push_back(CutLoopEdge{ corners[corner][0u], corners[corner][1u] });
        }
    }
    // Deterministic boundary order: traversal input matches emitted triangle order.
    return true;
}

[[nodiscard]] bool OrderBoundaryLoop(
    ScratchArena& scratchArena,
    const Vector<CutLoopEdge, ScratchArena>& edges,
    Vector<u32, ScratchArena>& outLoop
){
    outLoop.clear();
    if(edges.empty())
        return true;
    // Boundary edges keep source-triangle winding, so consecutive edges meet
    // head-to-tail or tail-to-tail. Walk them undirected for one closed ring.
    Vector<CutLoopEdge, ScratchArena> remaining(scratchArena);
    remaining = edges;
    u32 start = remaining.front().first;
    u32 cursor = start;
    for(const CutLoopEdge& edge : remaining){
        if(edge.first < start)
            start = edge.first;
        if(edge.second < start)
            start = edge.second;
    }
    cursor = start;
    outLoop.reserve(edges.size());
    outLoop.push_back(cursor);
    {
        const auto seed = std::find_if(
            remaining.begin(),
            remaining.end(),
            [cursor](const CutLoopEdge& edge){ return edge.first == cursor || edge.second == cursor; }
        );
        if(seed == remaining.end())
            return false;
        cursor = seed->first == cursor ? seed->second : seed->first;
        outLoop.push_back(cursor);
        remaining.erase(seed);
    }
    while(!remaining.empty()){
        bool advanced = false;
        for(auto it = remaining.begin(); it != remaining.end(); ++it){
            if(it->first == cursor || it->second == cursor){
                const u32 next = it->first == cursor ? it->second : it->first;
                remaining.erase(it);
                cursor = next;
                if(cursor == start)
                    break;
                outLoop.push_back(cursor);
                advanced = true;
                break;
            }
        }
        if(cursor == start || !advanced)
            break;
    }
    if(cursor != start || !remaining.empty())
        return false;
    for(usize vertexIndex = 1u; vertexIndex < outLoop.size(); ++vertexIndex){
        for(usize other = 0u; other < vertexIndex; ++other){
            if(outLoop[vertexIndex] == outLoop[other])
                return false;
        }
    }
    return true;
}

[[nodiscard]] bool CapNormal(
    const CsgDeformVertexVector<ScratchArena>& vertices,
    const Vector<u32, ScratchArena>& loop,
    Float4& outNormal
){
    outNormal = Float4(0.0f, 1.0f, 0.0f, 0.0f);
    if(loop.size() < 3u)
        return false;
    f32 areaX = 0.0f;
    f32 areaY = 0.0f;
    f32 areaZ = 0.0f;
    const Float3U& origin = vertices[loop[0u]].position;
    for(usize vertexIndex = 1u; vertexIndex + 1u < loop.size(); ++vertexIndex){
        const Float3U& first = vertices[loop[vertexIndex]].position;
        const Float3U& second = vertices[loop[vertexIndex + 1u]].position;
        const f32 edgeAx = first.x - origin.x;
        const f32 edgeAy = first.y - origin.y;
        const f32 edgeAz = first.z - origin.z;
        const f32 edgeBx = second.x - origin.x;
        const f32 edgeBy = second.y - origin.y;
        const f32 edgeBz = second.z - origin.z;
        areaX += edgeAy * edgeBz - edgeAz * edgeBy;
        areaY += edgeAz * edgeBx - edgeAx * edgeBz;
        areaZ += edgeAx * edgeBy - edgeAy * edgeBx;
    }
    const f32 areaLength = Sqrt(areaX * areaX + areaY * areaY + areaZ * areaZ);
    if(!(areaLength > 0.0000001f))
        return false;
    outNormal = Float4(areaX / areaLength, areaY / areaLength, areaZ / areaLength, 0.0f);
    return true;
}

[[nodiscard]] bool FillCapLoop(
    ScratchArena& scratchArena,
    const Float4& loopNormal,
    CsgDeformVertexVector<ScratchArena>& inOutVertices,
    CsgDeformTriangleVector<ScratchArena>& inOutTriangles,
    const Vector<u32, ScratchArena>& loop,
    u32& outCapTriangles
){
    outCapTriangles = 0u;
    if(loop.size() < 3u)
        return false;
    if(inOutVertices.size() + 1u > s_MaxDeformVertices)
        return false;
    f32 centerPosition[3u] = { 0.0f, 0.0f, 0.0f };
    f32 centerUv[2u] = { 0.0f, 0.0f };
    f32 centerColor[4u] = { 0.0f, 0.0f, 0.0f, 0.0f };
    for(const u32 vertexIndex : loop){
        if(vertexIndex >= inOutVertices.size())
            return false;
        const CsgDeformVertex& vertex = inOutVertices[vertexIndex];
        centerPosition[0u] += vertex.position.x;
        centerPosition[1u] += vertex.position.y;
        centerPosition[2u] += vertex.position.z;
        centerUv[0u] += vertex.uv0.x;
        centerUv[1u] += vertex.uv0.y;
        centerColor[0u] += vertex.color.x;
        centerColor[1u] += vertex.color.y;
        centerColor[2u] += vertex.color.z;
        centerColor[3u] += vertex.color.w;
    }
    const f32 loopSize = static_cast<f32>(loop.size());
    CsgDeformVertex center;
    center.position = Float3U(centerPosition[0u] / loopSize, centerPosition[1u] / loopSize, centerPosition[2u] / loopSize);
    center.normal = loopNormal;
    center.tangent = Float4(1.0f, 0.0f, 0.0f, 1.0f);
    center.uv0 = Float2U(centerUv[0u] / loopSize, centerUv[1u] / loopSize);
    center.color = Float4(centerColor[0u] / loopSize, centerColor[1u] / loopSize, centerColor[2u] / loopSize, centerColor[3u] / loopSize);
    if(!NormalizeDeformVertex(center))
        return false;
    const u32 centerIndex = static_cast<u32>(inOutVertices.size());
    inOutVertices.push_back(center);

    // One orientation rule for every loop: reverse kept winding so caps face the cut.
    for(usize vertexIndex = 0u; vertexIndex < loop.size(); ++vertexIndex){
        if(inOutTriangles.size() + 1u > s_MaxDeformTriangles)
            return false;
        const u32 first = loop[vertexIndex];
        const u32 second = loop[(vertexIndex + 1u) % loop.size()];
        EmitTriangle(inOutTriangles, centerIndex, second, first);
        ++outCapTriangles;
    }
    static_cast<void>(scratchArena);
    return true;
}

[[nodiscard]] bool FillCutCaps(
    ScratchArena& scratchArena,
    CsgDeformVertexVector<ScratchArena>& inOutVertices,
    CsgDeformTriangleVector<ScratchArena>& inOutTriangles,
    Vector<CutLoopEdge, ScratchArena>& scratchEdges,
    Vector<u32, ScratchArena>& scratchLoop,
    u32& outCapTriangles
){
    outCapTriangles = 0u;
    static_cast<void>(scratchLoop);
    if(!CollectBoundaryEdges(scratchArena, inOutVertices, inOutTriangles, scratchEdges))
        return false;
    if(scratchEdges.empty())
        return true;
    // Peel one closed loop at a time from the boundary set; leftover edges fail viability.
    Vector<CutLoopEdge, ScratchArena> remaining(scratchArena);
    remaining = scratchEdges;
    Vector<CutLoopEdge, ScratchArena> loopEdges(scratchArena);
    Vector<u32, ScratchArena> loop(scratchArena);
    while(!remaining.empty()){
        loopEdges.clear();
        loopEdges.push_back(remaining.back());
        remaining.pop_back();
        for(bool grown = true; grown;){
            grown = false;
            for(usize edgeIndex = 0u; edgeIndex < remaining.size();){
                const CutLoopEdge& candidate = remaining[edgeIndex];
                bool adjacent = false;
                for(const CutLoopEdge& owned : loopEdges){
                    if(candidate.first == owned.first || candidate.first == owned.second || candidate.second == owned.first || candidate.second == owned.second){
                        adjacent = true;
                        break;
                    }
                }
                if(adjacent){
                    loopEdges.push_back(candidate);
                    remaining.erase(remaining.begin() + static_cast<typename decltype(remaining)::difference_type>(edgeIndex));
                    grown = true;
                }
                else
                    ++edgeIndex;
            }
        }
        // Open boundary chains (e.g. cuts across an open sheet) carry no closable
        // volume loop; they stay viable cap-free. Only a closed but degenerate
        // loop fails viability.
        if(!OrderBoundaryLoop(scratchArena, loopEdges, loop))
            continue;
        Float4 loopNormal;
        if(!CapNormal(inOutVertices, loop, loopNormal))
            return false;
        u32 loopCaps = 0u;
        if(!FillCapLoop(scratchArena, loopNormal, inOutVertices, inOutTriangles, loop, loopCaps))
            return false;
        outCapTriangles += loopCaps;
    }
    return true;
}

struct DeformRebuildResult{
    CsgDeformViability viability;
    CsgDeformStats stats;
};

[[nodiscard]] bool RebuildSequentialCuts(
    ScratchArena& scratchArena,
    const CsgDeformVertex* inputVertices,
    const usize inputVertexCount,
    const CsgDeformTriangle* inputTriangles,
    const usize inputTriangleCount,
    const CsgDeformCutDesc* cuts,
    const usize cutCount,
    const CsgDeformBuildOptions& options,
    CsgDeformVertexVector<ScratchArena>& outVertices,
    CsgDeformTriangleVector<ScratchArena>& outTriangles,
    DeformRebuildResult& outResult
){
    outResult = DeformRebuildResult{};
    outVertices.clear();
    outTriangles.clear();
    outResult.stats.inputVertexCount = static_cast<u32>(inputVertexCount);
    outResult.stats.inputTriangleCount = static_cast<u32>(inputTriangleCount);
    if(!inputVertices || !inputTriangles || inputVertexCount == 0u || inputTriangleCount == 0u){
        outResult.viability.viable = false;
        outResult.viability.reason = CsgDeformViabilityReason::EmptyInput;
        return false;
    }
    if(inputVertexCount > s_MaxDeformVertices || inputTriangleCount > s_MaxDeformTriangles){
        outResult.viability.viable = false;
        outResult.viability.reason = CsgDeformViabilityReason::TooLarge;
        return false;
    }
    if(!ValidOptions(options)){
        outResult.viability.viable = false;
        outResult.viability.reason = CsgDeformViabilityReason::InvalidCutter;
        return false;
    }
    CsgDeformViabilityReason::Enum inputReason = CsgDeformViabilityReason::Ok;
    if(!FiniteInput(inputVertices, inputVertexCount, inputReason)){
        outResult.viability.viable = false;
        outResult.viability.reason = inputReason;
        return false;
    }
    if(!ValidTopology(inputVertices, inputVertexCount, inputTriangles, inputTriangleCount)){
        outResult.viability.viable = false;
        outResult.viability.reason = CsgDeformViabilityReason::InvalidTopology;
        return false;
    }

    const f32 epsilon = ShapeEpsilon(options);
    outVertices.reserve(inputVertexCount + cutCount * 16u);
    outTriangles.reserve(inputTriangleCount * 2u + cutCount * 16u);
    for(usize vertexIndex = 0u; vertexIndex < inputVertexCount; ++vertexIndex)
        outVertices.push_back(inputVertices[vertexIndex]);
    for(usize triangleIndex = 0u; triangleIndex < inputTriangleCount; ++triangleIndex)
        outTriangles.push_back(inputTriangles[triangleIndex]);

    CsgDeformTriangleVector<ScratchArena> scratchKept(scratchArena);
    Vector<f32, ScratchArena> scratchDistances(scratchArena);
    Vector<CutLoopEdge, ScratchArena> scratchEdges(scratchArena);
    Vector<u32, ScratchArena> scratchLoop(scratchArena);

    u32 appliedCuts = 0u;
    u32 capTriangles = 0u;
    if(cuts){
        for(usize cutIndex = 0u; cutIndex < cutCount; ++cutIndex){
            const CsgDeformCutDesc& cut = cuts[cutIndex];
            if(!cut.active)
                continue;
            CsgDeformViabilityReason::Enum cutReason = CsgDeformViabilityReason::Ok;
            if(!ClipShell(scratchArena, cut.shape, epsilon, outVertices, outTriangles, scratchKept, scratchDistances, cutReason)){
                outResult.viability.viable = false;
                outResult.viability.reason = cutReason == CsgDeformViabilityReason::Ok
                    ? CsgDeformViabilityReason::NoKeptGeometry
                    : cutReason
                ;
                return false;
            }
            ++appliedCuts;
            if(outTriangles.empty() || outVertices.size() > s_MaxDeformVertices || outTriangles.size() > s_MaxDeformTriangles){
                outResult.viability.viable = false;
                outResult.viability.reason = outTriangles.empty()
                    ? CsgDeformViabilityReason::NoKeptGeometry
                    : CsgDeformViabilityReason::TooLarge
                ;
                return false;
            }
            if(options.fillCaps){
                u32 cutCaps = 0u;
                if(!FillCutCaps(scratchArena, outVertices, outTriangles, scratchEdges, scratchLoop, cutCaps)){
                    outResult.viability.viable = false;
                    outResult.viability.reason = CsgDeformViabilityReason::CapLoopFailed;
                    return false;
                }
                capTriangles += cutCaps;
            }
        }
    }

    if(outVertices.empty() || outTriangles.empty()){
        outResult.viability.viable = false;
        outResult.viability.reason = CsgDeformViabilityReason::NoKeptGeometry;
        return false;
    }
    for(const CsgDeformVertex& vertex : outVertices){
        if(!FiniteVertex(vertex)){
            outResult.viability.viable = false;
            outResult.viability.reason = CsgDeformViabilityReason::NonFiniteInput;
            return false;
        }
    }
    outResult.viability.viable = true;
    outResult.viability.reason = CsgDeformViabilityReason::Ok;
    outResult.stats.outputVertexCount = static_cast<u32>(outVertices.size());
    outResult.stats.outputTriangleCount = static_cast<u32>(outTriangles.size());
    outResult.stats.appliedCutCount = appliedCuts;
    outResult.stats.capTriangleCount = capTriangles;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CsgDeformViability CheckCsgDeformCutsViability(
    Core::Alloc::ScratchArena& scratchArena,
    const CsgDeformVertex* inputVertices,
    const usize inputVertexCount,
    const CsgDeformTriangle* inputTriangles,
    const usize inputTriangleCount,
    const CsgDeformCutDesc* cuts,
    const usize cutCount,
    const CsgDeformBuildOptions& options
){
    CsgDeformVertexVector<Core::Alloc::ScratchArena> vertices(scratchArena);
    CsgDeformTriangleVector<Core::Alloc::ScratchArena> triangles(scratchArena);
    __hidden_csg_deform_edit::DeformRebuildResult result;
    if(!__hidden_csg_deform_edit::RebuildSequentialCuts(
        scratchArena,
        inputVertices,
        inputVertexCount,
        inputTriangles,
        inputTriangleCount,
        cuts,
        cutCount,
        options,
        vertices,
        triangles,
        result
    ))
        return result.viability;
    return result.viability;
}

bool PreviewCsgDeformCuts(
    Core::Alloc::ScratchArena& scratchArena,
    const CsgDeformVertex* inputVertices,
    const usize inputVertexCount,
    const CsgDeformTriangle* inputTriangles,
    const usize inputTriangleCount,
    const CsgDeformCutDesc* cuts,
    const usize cutCount,
    const CsgDeformBuildOptions& options,
    CsgDeformVertexVector<Core::Alloc::ScratchArena>& outVertices,
    CsgDeformTriangleVector<Core::Alloc::ScratchArena>& outTriangles,
    CsgDeformStats& outStats
){
    outStats = CsgDeformStats{};
    __hidden_csg_deform_edit::DeformRebuildResult result;
    if(!__hidden_csg_deform_edit::RebuildSequentialCuts(
        scratchArena,
        inputVertices,
        inputVertexCount,
        inputTriangles,
        inputTriangleCount,
        cuts,
        cutCount,
        options,
        outVertices,
        outTriangles,
        result
    )){
        outStats = result.stats;
        return false;
    }
    outStats = result.stats;
    return result.viability.viable;
}

bool CommitCsgDeformCuts(
    Core::Alloc::ScratchArena& scratchArena,
    Core::Alloc::GlobalArena& commitArena,
    const CsgDeformVertex* inputVertices,
    const usize inputVertexCount,
    const CsgDeformTriangle* inputTriangles,
    const usize inputTriangleCount,
    const CsgDeformCutDesc* cuts,
    const usize cutCount,
    const CsgDeformBuildOptions& options,
    CsgDeformVertexVector<Core::Alloc::GlobalArena>& outVertices,
    CsgDeformTriangleVector<Core::Alloc::GlobalArena>& outTriangles,
    CsgDeformStats& outStats
){
    outStats = CsgDeformStats{};
    outVertices.clear();
    outTriangles.clear();
    // Commit reuses the preview entry point so both always observe the same
    // rebuild, viability classifier, and stats for identical inputs.
    CsgDeformVertexVector<Core::Alloc::ScratchArena> previewVertices(scratchArena);
    CsgDeformTriangleVector<Core::Alloc::ScratchArena> previewTriangles(scratchArena);
    CsgDeformStats previewStats{};
    if(!PreviewCsgDeformCuts(
        scratchArena,
        inputVertices,
        inputVertexCount,
        inputTriangles,
        inputTriangleCount,
        cuts,
        cutCount,
        options,
        previewVertices,
        previewTriangles,
        previewStats
    )){
        outStats = previewStats;
        return false;
    }
    outVertices.reserve(previewVertices.size());
    outTriangles.reserve(previewTriangles.size());
    for(const CsgDeformVertex& vertex : previewVertices)
        outVertices.push_back(vertex);
    for(const CsgDeformTriangle& triangle : previewTriangles)
        outTriangles.push_back(triangle);
    outStats = previewStats;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

