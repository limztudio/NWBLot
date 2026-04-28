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


    ScratchIndexVector vertices;
    ScratchIndexVector indices;


    explicit PendingMeshlet(Core::Alloc::ScratchArena<>& scratchArena)
        : vertices(Core::Alloc::ScratchAllocator<u32>(scratchArena))
        , indices(Core::Alloc::ScratchAllocator<u32>(scratchArena))
    {}
};

struct TriangleVertices{
    u32 values[3] = { 0u, 0u, 0u };
};

struct TriangleLocalIndices{
    u32 values[3] = { Limit<u32>::s_Max, Limit<u32>::s_Max, Limit<u32>::s_Max };
    u32 missingVertexCount = 0u;
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
    if(
        triangle.values[0] >= positions.size()
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

[[nodiscard]] TriangleLocalIndices ResolveTriangleLocalIndices(const PendingMeshlet& meshlet, const TriangleVertices& triangle){
    TriangleLocalIndices output;
    for(u32 corner = 0u; corner < 3u; ++corner){
        const u32 vertex = triangle.values[corner];
        u32 localIndex = 0u;
        if(FindLocalVertex(meshlet.vertices, vertex, localIndex))
            output.values[corner] = localIndex;
        else
            ++output.missingVertexCount;
    }
    return output;
}

[[nodiscard]] bool TriangleFitsMeshlet(const PendingMeshlet& meshlet, const TriangleLocalIndices& localIndices, const MeshletBuildConfig& config){
    const usize triangleCount = meshlet.indices.size() / 3u;
    if(triangleCount >= config.maxTriangles)
        return false;

    return meshlet.vertices.size() + localIndices.missingVertexCount <= config.maxVertices;
}

[[nodiscard]] bool AppendTriangle(PendingMeshlet& meshlet, const TriangleVertices& triangle, const TriangleLocalIndices& localIndices){
    for(u32 corner = 0u; corner < 3u; ++corner){
        u32 localIndex = localIndices.values[corner];
        if(localIndex == Limit<u32>::s_Max){
            if(meshlet.vertices.size() >= static_cast<usize>(Limit<u32>::s_Max))
                return false;
            localIndex = static_cast<u32>(meshlet.vertices.size());
            meshlet.vertices.push_back(triangle.values[corner]);
        }
        meshlet.indices.push_back(localIndex);
    }
    return true;
}

template<typename VertexAllocator>
[[nodiscard]] bool ComputeMeshletBounds(const Vector<Float3U>& positions, const Vector<u32, VertexAllocator>& vertices, MeshletBounds& outBounds){
    if(vertices.empty())
        return false;

    const u32 firstVertex = vertices[0];
    if(firstVertex >= positions.size())
        return false;

    SIMDVector minimum = LoadFloat(positions[firstVertex]);
    if(!FiniteVector(minimum, 0x7u))
        return false;

    SIMDVector maximum = minimum;
    for(usize index = 1u; index < vertices.size(); ++index){
        const u32 vertex = vertices[index];
        if(vertex >= positions.size())
            return false;

        const SIMDVector position = LoadFloat(positions[vertex]);
        if(!FiniteVector(position, 0x7u))
            return false;

        minimum = VectorMin(minimum, position);
        maximum = VectorMax(maximum, position);
    }

    const SIMDVector center = VectorScale(VectorAdd(minimum, maximum), 0.5f);
    if(!FiniteVector(center, 0x7u))
        return false;

    f32 radiusSquared = 0.0f;
    for(const u32 vertex : vertices){
        const SIMDVector offset = VectorSubtract(LoadFloat(positions[vertex]), center);
        radiusSquared = Max(radiusSquared, VectorGetX(Vector3LengthSq(offset)));
    }
    if(!IsFinite(radiusSquared))
        return false;

    StoreFloat(minimum, &outBounds.minimum);
    StoreFloat(maximum, &outBounds.maximum);
    StoreFloat(center, &outBounds.center);
    outBounds.radius = Sqrt(radiusSquared);
    return IsFinite(outBounds.radius);
}

[[nodiscard]] bool ValidExpansionRadius(const f32 radius){
    return IsFinite(radius) && radius >= 0.0f;
}

[[nodiscard]] bool ResolveVertexExpansionRadius(const Vector<f32>& vertexExpansionRadii, const u32 vertex, const f32 uniformExpansionRadius, f32& outRadius){
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
    Vector<u32, LocalIndexAllocator>& outLocalIndices
){
    if(pending.vertices.empty())
        return true;
    if(
        pending.indices.empty()
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
    Vector<u32>& outLocalIndices
){
    using namespace __hidden_geometry_meshlet_cluster;

    outMeshlets.clear();
    outVertexIndices.clear();
    outLocalIndices.clear();
    if(
        positions.empty()
        || indices.empty()
        || (indices.size() % 3u) != 0u
        || !ValidConfig(config)
    )
        return false;

    Core::Alloc::ScratchArena<> scratchArena;
    PendingMeshlet pending(scratchArena);
    pending.vertices.reserve(config.maxVertices);
    pending.indices.reserve(config.maxTriangles * 3u);
    auto fail = [&outMeshlets, &outVertexIndices, &outLocalIndices](){
        outMeshlets.clear();
        outVertexIndices.clear();
        outLocalIndices.clear();
        return false;
    };

    const usize triangleCount = indices.size() / 3u;
    const usize meshletCountReserve = (triangleCount + static_cast<usize>(config.maxTriangles) - 1u) / config.maxTriangles;
    outMeshlets.reserve(meshletCountReserve);
    outVertexIndices.reserve(indices.size());
    outLocalIndices.reserve(indices.size());
    for(usize triangleIndex = 0u; triangleIndex < triangleCount; ++triangleIndex){
        const usize indexBase = triangleIndex * 3u;
        TriangleVertices triangle;
        triangle.values[0] = indices[indexBase + 0u];
        triangle.values[1] = indices[indexBase + 1u];
        triangle.values[2] = indices[indexBase + 2u];

        if(!ValidTriangle(positions, triangle))
            return fail();
        TriangleLocalIndices triangleLocalIndices = ResolveTriangleLocalIndices(pending, triangle);
        bool triangleFits = TriangleFitsMeshlet(pending, triangleLocalIndices, config);
        if(!triangleFits){
            if(!FlushMeshlet(positions, pending, outMeshlets, outVertexIndices, outLocalIndices))
                return fail();
            triangleLocalIndices = ResolveTriangleLocalIndices(pending, triangle);
            triangleFits = TriangleFitsMeshlet(pending, triangleLocalIndices, config);
        }
        if(!triangleFits || !AppendTriangle(pending, triangle, triangleLocalIndices))
            return fail();
    }

    if(!FlushMeshlet(positions, pending, outMeshlets, outVertexIndices, outLocalIndices) || outMeshlets.empty())
        return fail();

    return true;
}

bool ComputeMeshletDeformationBounds(
    const Vector<Float3U>& positions,
    const Vector<u32>& meshletVertexIndices,
    const MeshletCluster& meshlet,
    const Vector<f32>& vertexExpansionRadii,
    const f32 uniformExpansionRadius,
    MeshletBounds& outBounds
){
    using namespace __hidden_geometry_meshlet_cluster;

    outBounds = MeshletBounds{};
    if(
        positions.empty()
        || meshletVertexIndices.empty()
        || meshlet.vertexCount == 0u
        || meshlet.firstVertex > meshletVertexIndices.size()
        || meshlet.vertexCount > meshletVertexIndices.size() - meshlet.firstVertex
        || !ValidExpansionRadius(uniformExpansionRadius)
        || (!vertexExpansionRadii.empty() && vertexExpansionRadii.size() != positions.size())
    )
        return false;

    const bool hasVertexExpansion = !vertexExpansionRadii.empty();
    SIMDVector minimum = VectorZero();
    SIMDVector maximum = VectorZero();
    bool initialized = false;
    for(u32 index = 0u; index < meshlet.vertexCount; ++index){
        const u32 vertex = meshletVertexIndices[meshlet.firstVertex + index];
        if(vertex >= positions.size())
            return false;

        const SIMDVector position = LoadFloat(positions[vertex]);
        if(!FiniteVector(position, 0x7u))
            return false;

        f32 radius = 0.0f;
        if(!ResolveVertexExpansionRadius(vertexExpansionRadii, vertex, uniformExpansionRadius, radius))
            return false;

        const SIMDVector radiusVector = VectorReplicate(radius);
        const SIMDVector expandedMinimum = VectorSubtract(position, radiusVector);
        const SIMDVector expandedMaximum = VectorAdd(position, radiusVector);
        if(!FiniteVector(expandedMinimum, 0x7u) || !FiniteVector(expandedMaximum, 0x7u))
            return false;

        if(!initialized){
            minimum = expandedMinimum;
            maximum = expandedMaximum;
            initialized = true;
        }
        else{
            minimum = VectorMin(minimum, expandedMinimum);
            maximum = VectorMax(maximum, expandedMaximum);
        }
    }

    if(!initialized)
        return false;

    const SIMDVector center = VectorScale(VectorAdd(minimum, maximum), 0.5f);
    if(!FiniteVector(center, 0x7u))
        return false;

    f32 radius = 0.0f;
    for(u32 index = 0u; index < meshlet.vertexCount; ++index){
        const u32 vertex = meshletVertexIndices[meshlet.firstVertex + index];
        f32 expansionRadius = uniformExpansionRadius;
        if(hasVertexExpansion)
            expansionRadius += vertexExpansionRadii[vertex];

        const SIMDVector offset = VectorSubtract(LoadFloat(positions[vertex]), center);
        const f32 offsetLengthSquared = VectorGetX(Vector3LengthSq(offset));
        if(!IsFinite(offsetLengthSquared))
            return false;
        const f32 vertexRadius = Sqrt(offsetLengthSquared) + expansionRadius;
        if(!IsFinite(vertexRadius))
            return false;
        radius = Max(radius, vertexRadius);
    }

    StoreFloat(minimum, &outBounds.minimum);
    StoreFloat(maximum, &outBounds.maximum);
    StoreFloat(center, &outBounds.center);
    outBounds.radius = radius;
    return IsFinite(outBounds.radius);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_GEOMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

