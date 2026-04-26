// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "meshlet_cluster.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_GEOMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_geometry_meshlet_cluster{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr f32 s_TriangleAreaEpsilon = 0.000000000001f;

struct PendingMeshlet{
    Vector<u32> vertices;
    Vector<u32> indices;
};

struct TriangleVertices{
    u32 values[3] = { 0u, 0u, 0u };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool FiniteVector(const SIMDVector value, const u32 activeMask){
    const SIMDVector invalid = VectorOrInt(VectorIsNaN(value), VectorIsInfinite(value));
    return (VectorMoveMask(invalid) & activeMask) == 0u;
}

[[nodiscard]] bool ValidConfig(const MeshletBuildConfig& config){
    return config.maxVertices >= 3u
        && config.maxTriangles >= 1u
        && config.maxTriangles <= (Limit<u32>::s_Max / 3u)
    ;
}

[[nodiscard]] bool ValidTriangle(
    const Vector<Float3U>& positions,
    const TriangleVertices& triangle)
{
    if(triangle.values[0] >= positions.size()
        || triangle.values[1] >= positions.size()
        || triangle.values[2] >= positions.size()
        || triangle.values[0] == triangle.values[1]
        || triangle.values[0] == triangle.values[2]
        || triangle.values[1] == triangle.values[2]
    )
        return false;

    const SIMDVector p0 = LoadFloat(positions[triangle.values[0]]);
    const SIMDVector p1 = LoadFloat(positions[triangle.values[1]]);
    const SIMDVector p2 = LoadFloat(positions[triangle.values[2]]);
    if(!FiniteVector(p0, 0x7u) || !FiniteVector(p1, 0x7u) || !FiniteVector(p2, 0x7u))
        return false;

    const SIMDVector areaVector = Vector3Cross(VectorSubtract(p1, p0), VectorSubtract(p2, p0));
    const f32 areaLengthSquared = VectorGetX(Vector3LengthSq(areaVector));
    return IsFinite(areaLengthSquared) && areaLengthSquared > s_TriangleAreaEpsilon;
}

[[nodiscard]] bool FindLocalVertex(const Vector<u32>& vertices, const u32 vertex, u32& outLocalIndex){
    for(usize index = 0u; index < vertices.size(); ++index){
        if(vertices[index] == vertex){
            outLocalIndex = static_cast<u32>(index);
            return true;
        }
    }
    return false;
}

[[nodiscard]] u32 CountMissingVertices(const PendingMeshlet& meshlet, const TriangleVertices& triangle){
    u32 missingVertexCount = 0u;
    for(const u32 vertex : triangle.values){
        u32 localIndex = 0u;
        if(!FindLocalVertex(meshlet.vertices, vertex, localIndex))
            ++missingVertexCount;
    }
    return missingVertexCount;
}

[[nodiscard]] bool TriangleFitsMeshlet(
    const PendingMeshlet& meshlet,
    const TriangleVertices& triangle,
    const MeshletBuildConfig& config)
{
    const usize triangleCount = meshlet.indices.size() / 3u;
    if(triangleCount >= config.maxTriangles)
        return false;

    const u32 missingVertexCount = CountMissingVertices(meshlet, triangle);
    return meshlet.vertices.size() + missingVertexCount <= config.maxVertices;
}

[[nodiscard]] bool AppendTriangle(PendingMeshlet& meshlet, const TriangleVertices& triangle){
    for(const u32 vertex : triangle.values){
        u32 localIndex = 0u;
        if(!FindLocalVertex(meshlet.vertices, vertex, localIndex)){
            if(meshlet.vertices.size() >= static_cast<usize>(Limit<u32>::s_Max))
                return false;
            localIndex = static_cast<u32>(meshlet.vertices.size());
            meshlet.vertices.push_back(vertex);
        }
        meshlet.indices.push_back(localIndex);
    }
    return true;
}

[[nodiscard]] bool ComputeMeshletBounds(
    const Vector<Float3U>& positions,
    const Vector<u32>& vertices,
    MeshletBounds& outBounds)
{
    if(vertices.empty())
        return false;

    const Float3U& firstPosition = positions[vertices[0]];
    Float3U minimum = firstPosition;
    Float3U maximum = firstPosition;
    for(const u32 vertex : vertices){
        if(vertex >= positions.size())
            return false;

        const Float3U& position = positions[vertex];
        if(!FiniteVector(LoadFloat(position), 0x7u))
            return false;

        minimum.x = Min(minimum.x, position.x);
        minimum.y = Min(minimum.y, position.y);
        minimum.z = Min(minimum.z, position.z);
        maximum.x = Max(maximum.x, position.x);
        maximum.y = Max(maximum.y, position.y);
        maximum.z = Max(maximum.z, position.z);
    }

    const Float3U center(
        (minimum.x + maximum.x) * 0.5f,
        (minimum.y + maximum.y) * 0.5f,
        (minimum.z + maximum.z) * 0.5f
    );
    f32 radiusSquared = 0.0f;
    const SIMDVector centerVector = LoadFloat(center);
    for(const u32 vertex : vertices){
        const SIMDVector offset = VectorSubtract(LoadFloat(positions[vertex]), centerVector);
        radiusSquared = Max(radiusSquared, VectorGetX(Vector3LengthSq(offset)));
    }
    if(!IsFinite(radiusSquared))
        return false;

    outBounds.minimum = minimum;
    outBounds.maximum = maximum;
    outBounds.center = center;
    outBounds.radius = Sqrt(radiusSquared);
    return IsFinite(outBounds.radius);
}

[[nodiscard]] bool FlushMeshlet(
    const Vector<Float3U>& positions,
    PendingMeshlet& pending,
    Vector<MeshletCluster>& outMeshlets,
    Vector<u32>& outVertexIndices,
    Vector<u32>& outLocalIndices)
{
    if(pending.vertices.empty())
        return true;
    if(pending.indices.empty()
        || (pending.indices.size() % 3u) != 0u
        || pending.vertices.size() > static_cast<usize>(Limit<u32>::s_Max)
        || pending.indices.size() > static_cast<usize>(Limit<u32>::s_Max)
        || outVertexIndices.size() > static_cast<usize>(Limit<u32>::s_Max) - pending.vertices.size()
        || outLocalIndices.size() > static_cast<usize>(Limit<u32>::s_Max) - pending.indices.size()
    )
        return false;

    MeshletCluster cluster;
    cluster.firstVertex = static_cast<u32>(outVertexIndices.size());
    cluster.vertexCount = static_cast<u32>(pending.vertices.size());
    cluster.firstIndex = static_cast<u32>(outLocalIndices.size());
    cluster.indexCount = static_cast<u32>(pending.indices.size());
    if(!ComputeMeshletBounds(positions, pending.vertices, cluster.bounds))
        return false;

    outMeshlets.push_back(cluster);
    for(const u32 vertex : pending.vertices)
        outVertexIndices.push_back(vertex);
    for(const u32 index : pending.indices)
        outLocalIndices.push_back(index);

    pending.vertices.clear();
    pending.indices.clear();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool BuildMeshlets(
    const Vector<Float3U>& positions,
    const Vector<u32>& indices,
    const MeshletBuildConfig& config,
    Vector<MeshletCluster>& outMeshlets,
    Vector<u32>& outVertexIndices,
    Vector<u32>& outLocalIndices)
{
    using namespace __hidden_geometry_meshlet_cluster;

    outMeshlets.clear();
    outVertexIndices.clear();
    outLocalIndices.clear();
    if(positions.empty()
        || indices.empty()
        || (indices.size() % 3u) != 0u
        || !ValidConfig(config)
    )
        return false;

    PendingMeshlet pending;
    pending.vertices.reserve(config.maxVertices);
    pending.indices.reserve(config.maxTriangles * 3u);
    Vector<MeshletCluster> meshlets;
    Vector<u32> vertexIndices;
    Vector<u32> localIndices;

    const usize triangleCount = indices.size() / 3u;
    for(usize triangleIndex = 0u; triangleIndex < triangleCount; ++triangleIndex){
        const usize indexBase = triangleIndex * 3u;
        TriangleVertices triangle;
        triangle.values[0] = indices[indexBase + 0u];
        triangle.values[1] = indices[indexBase + 1u];
        triangle.values[2] = indices[indexBase + 2u];

        if(!ValidTriangle(positions, triangle))
            return false;
        if(!TriangleFitsMeshlet(pending, triangle, config)){
            if(!FlushMeshlet(positions, pending, meshlets, vertexIndices, localIndices))
                return false;
        }
        if(!TriangleFitsMeshlet(pending, triangle, config) || !AppendTriangle(pending, triangle))
            return false;
    }

    if(!FlushMeshlet(positions, pending, meshlets, vertexIndices, localIndices) || meshlets.empty())
        return false;

    outMeshlets = Move(meshlets);
    outVertexIndices = Move(vertexIndices);
    outLocalIndices = Move(localIndices);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_GEOMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

