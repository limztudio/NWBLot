// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "meshlet_cluster.h"

#include <core/alloc/scratch.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_GEOMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_geometry_meshlet_cluster{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr f32 s_TriangleAreaEpsilon = 0.000000000001f;

struct PendingMeshlet{
    using ScratchIndexVector = Vector<u32, Core::Alloc::ScratchAllocator<u32>>;

    explicit PendingMeshlet(Core::Alloc::ScratchArena<>& scratchArena)
        : vertices(Core::Alloc::ScratchAllocator<u32>(scratchArena))
        , indices(Core::Alloc::ScratchAllocator<u32>(scratchArena))
    {}

    ScratchIndexVector vertices;
    ScratchIndexVector indices;
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

[[nodiscard]] bool ValidTriangle(const Vector<Float3U>& positions, const TriangleVertices& triangle){
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

template<typename VertexAllocator>
[[nodiscard]] bool FindLocalVertex(const Vector<u32, VertexAllocator>& vertices, const u32 vertex, u32& outLocalIndex){
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

template<typename VertexAllocator>
[[nodiscard]] bool ComputeMeshletBounds(const Vector<Float3U>& positions, const Vector<u32, VertexAllocator>& vertices, MeshletBounds& outBounds){
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

[[nodiscard]] bool ValidExpansionRadius(const f32 radius){
    return IsFinite(radius) && radius >= 0.0f;
}

[[nodiscard]] bool ResolveVertexExpansionRadius(
    const Vector<f32>& vertexExpansionRadii,
    const u32 vertex,
    const f32 uniformExpansionRadius,
    f32& outRadius)
{
    outRadius = uniformExpansionRadius;
    if(!vertexExpansionRadii.empty()){
        if(vertex >= vertexExpansionRadii.size() || !ValidExpansionRadius(vertexExpansionRadii[vertex]))
            return false;
        outRadius += vertexExpansionRadii[vertex];
    }
    return ValidExpansionRadius(outRadius);
}

template<typename MeshletAllocator, typename VertexIndexAllocator, typename LocalIndexAllocator>
[[nodiscard]] bool FlushMeshlet(
    const Vector<Float3U>& positions,
    PendingMeshlet& pending,
    Vector<MeshletCluster, MeshletAllocator>& outMeshlets,
    Vector<u32, VertexIndexAllocator>& outVertexIndices,
    Vector<u32, LocalIndexAllocator>& outLocalIndices)
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
    outVertexIndices.insert(outVertexIndices.end(), pending.vertices.begin(), pending.vertices.end());
    outLocalIndices.insert(outLocalIndices.end(), pending.indices.begin(), pending.indices.end());

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

    Core::Alloc::ScratchArena<> scratchArena;
    PendingMeshlet pending(scratchArena);
    pending.vertices.reserve(config.maxVertices);
    pending.indices.reserve(config.maxTriangles * 3u);
    Vector<MeshletCluster, Core::Alloc::ScratchAllocator<MeshletCluster>> meshlets{
        Core::Alloc::ScratchAllocator<MeshletCluster>(scratchArena)
    };
    Vector<u32, Core::Alloc::ScratchAllocator<u32>> vertexIndices{
        Core::Alloc::ScratchAllocator<u32>(scratchArena)
    };
    Vector<u32, Core::Alloc::ScratchAllocator<u32>> localIndices{
        Core::Alloc::ScratchAllocator<u32>(scratchArena)
    };

    const usize triangleCount = indices.size() / 3u;
    const usize meshletCountReserve = (triangleCount + static_cast<usize>(config.maxTriangles) - 1u) / config.maxTriangles;
    meshlets.reserve(meshletCountReserve);
    vertexIndices.reserve(indices.size());
    localIndices.reserve(indices.size());
    for(usize triangleIndex = 0u; triangleIndex < triangleCount; ++triangleIndex){
        const usize indexBase = triangleIndex * 3u;
        TriangleVertices triangle;
        triangle.values[0] = indices[indexBase + 0u];
        triangle.values[1] = indices[indexBase + 1u];
        triangle.values[2] = indices[indexBase + 2u];

        if(!ValidTriangle(positions, triangle))
            return false;
        bool triangleFits = TriangleFitsMeshlet(pending, triangle, config);
        if(!triangleFits){
            if(!FlushMeshlet(positions, pending, meshlets, vertexIndices, localIndices))
                return false;
            triangleFits = TriangleFitsMeshlet(pending, triangle, config);
        }
        if(!triangleFits || !AppendTriangle(pending, triangle))
            return false;
    }

    if(!FlushMeshlet(positions, pending, meshlets, vertexIndices, localIndices) || meshlets.empty())
        return false;

    outMeshlets.assign(meshlets.begin(), meshlets.end());
    outVertexIndices.assign(vertexIndices.begin(), vertexIndices.end());
    outLocalIndices.assign(localIndices.begin(), localIndices.end());
    return true;
}

bool ComputeMeshletDeformationBounds(
    const Vector<Float3U>& positions,
    const Vector<u32>& meshletVertexIndices,
    const MeshletCluster& meshlet,
    const Vector<f32>& vertexExpansionRadii,
    const f32 uniformExpansionRadius,
    MeshletBounds& outBounds)
{
    using namespace __hidden_geometry_meshlet_cluster;

    outBounds = MeshletBounds{};
    if(positions.empty()
        || meshletVertexIndices.empty()
        || meshlet.vertexCount == 0u
        || meshlet.firstVertex > meshletVertexIndices.size()
        || meshlet.vertexCount > meshletVertexIndices.size() - meshlet.firstVertex
        || !ValidExpansionRadius(uniformExpansionRadius)
        || (!vertexExpansionRadii.empty() && vertexExpansionRadii.size() != positions.size())
    )
        return false;

    Float3U minimum;
    Float3U maximum;
    bool initialized = false;
    for(u32 index = 0u; index < meshlet.vertexCount; ++index){
        const u32 vertex = meshletVertexIndices[meshlet.firstVertex + index];
        if(vertex >= positions.size())
            return false;

        const Float3U& position = positions[vertex];
        if(!FiniteVector(LoadFloat(position), 0x7u))
            return false;

        f32 radius = 0.0f;
        if(!ResolveVertexExpansionRadius(vertexExpansionRadii, vertex, uniformExpansionRadius, radius))
            return false;

        const Float3U expandedMinimum(position.x - radius, position.y - radius, position.z - radius);
        const Float3U expandedMaximum(position.x + radius, position.y + radius, position.z + radius);
        if(!IsFinite(expandedMinimum.x)
            || !IsFinite(expandedMinimum.y)
            || !IsFinite(expandedMinimum.z)
            || !IsFinite(expandedMaximum.x)
            || !IsFinite(expandedMaximum.y)
            || !IsFinite(expandedMaximum.z)
        )
            return false;

        if(!initialized){
            minimum = expandedMinimum;
            maximum = expandedMaximum;
            initialized = true;
        }
        else{
            minimum.x = Min(minimum.x, expandedMinimum.x);
            minimum.y = Min(minimum.y, expandedMinimum.y);
            minimum.z = Min(minimum.z, expandedMinimum.z);
            maximum.x = Max(maximum.x, expandedMaximum.x);
            maximum.y = Max(maximum.y, expandedMaximum.y);
            maximum.z = Max(maximum.z, expandedMaximum.z);
        }
    }

    if(!initialized)
        return false;

    const Float3U center(
        (minimum.x + maximum.x) * 0.5f,
        (minimum.y + maximum.y) * 0.5f,
        (minimum.z + maximum.z) * 0.5f
    );
    if(!FiniteVector(LoadFloat(center), 0x7u))
        return false;

    f32 radius = 0.0f;
    const SIMDVector centerVector = LoadFloat(center);
    for(u32 index = 0u; index < meshlet.vertexCount; ++index){
        const u32 vertex = meshletVertexIndices[meshlet.firstVertex + index];
        f32 expansionRadius = 0.0f;
        if(!ResolveVertexExpansionRadius(vertexExpansionRadii, vertex, uniformExpansionRadius, expansionRadius))
            return false;

        const SIMDVector offset = VectorSubtract(LoadFloat(positions[vertex]), centerVector);
        const f32 offsetLengthSquared = VectorGetX(Vector3LengthSq(offset));
        if(!IsFinite(offsetLengthSquared))
            return false;
        const f32 vertexRadius = Sqrt(offsetLengthSquared) + expansionRadius;
        if(!IsFinite(vertexRadius))
            return false;
        radius = Max(radius, vertexRadius);
    }

    outBounds.minimum = minimum;
    outBounds.maximum = maximum;
    outBounds.center = center;
    outBounds.radius = radius;
    return IsFinite(outBounds.radius);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_GEOMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

