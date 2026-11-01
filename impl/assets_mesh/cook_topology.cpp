// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_topology.h"
#include <impl/assets_mesh/cook_types.adhd.h>

#include <global/simd/vector.h>
#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_mesh_cook_topology{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static u32 FindComponent(u32 triangle, const Span<u32> parents)noexcept{
    while(parents[triangle] != triangle){
        parents[triangle] = parents[parents[triangle]];
        triangle = parents[triangle];
    }
    return triangle;
}

static void JoinComponents(const u32 first, const u32 second, const Span<u32> parents, const Span<u8> ranks)noexcept{
    u32 firstRoot = FindComponent(first, parents);
    u32 secondRoot = FindComponent(second, parents);
    if(firstRoot == secondRoot)
        return;
    if(ranks[firstRoot] < ranks[secondRoot])
        Swap(firstRoot, secondRoot);
    parents[secondRoot] = firstRoot;
    if(ranks[firstRoot] == ranks[secondRoot])
        ++ranks[firstRoot];
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Vector<u32, Core::Alloc::ScratchArena>> BuildSolidTriangleWords(const Span<const Float3U> positions, const Span<const u32> triangleIndices, Core::Alloc::ScratchArena& scratchArena){
    using namespace __hidden_mesh_cook_topology;
    if(triangleIndices.empty() || triangleIndices.size() % s_MeshletTriangleIndexCount != 0u)
        return MakeUnexpected(Failure{});
    const usize triangleCount = triangleIndices.size() / s_MeshletTriangleIndexCount;
    if(triangleCount > Limit<u32>::s_Max)
        return MakeUnexpected(Failure{});
    for(const Float3U& position : positions){
        if(!VectorIsFinite(LoadFloat(position), VectorComponentMask::s_XYZ))
            return MakeUnexpected(Failure{});
    }

    Vector<u32, Core::Alloc::ScratchArena> words(DivideUp(triangleCount, usize(s_MeshletSolidTriangleWordBits)), 0u, scratchArena);
    Vector<u32, Core::Alloc::ScratchArena> parents(triangleCount, scratchArena);
    Vector<u8, Core::Alloc::ScratchArena> ranks(triangleCount, 0u, scratchArena);
    Vector<u8, Core::Alloc::ScratchArena> invalidTriangles(triangleCount, 0u, scratchArena);
    Vector<TriangleEdge, Core::Alloc::ScratchArena> edges{scratchArena};
    edges.reserve(triangleIndices.size());
    Iota(parents.begin(), parents.end(), 0u);
    for(u32 triangle = 0u; triangle < static_cast<u32>(triangleCount); ++triangle){
        const usize base = static_cast<usize>(triangle) * s_MeshletTriangleIndexCount;
        const u32 a = triangleIndices[base];
        const u32 b = triangleIndices[base + 1u];
        const u32 c = triangleIndices[base + 2u];
        if(a >= positions.size() || b >= positions.size() || c >= positions.size())
            return MakeUnexpected(Failure{});
        if(a == b || b == c || c == a)
            invalidTriangles[triangle] = 1u;
        else{
            const Double4 aValue = LoadTrianglePositionDouble(positions[a]);
            const Double4 bValue = LoadTrianglePositionDouble(positions[b]);
            const Double4 cValue = LoadTrianglePositionDouble(positions[c]);
            if(DegenerateTriangle(aValue, bValue, cValue))
                invalidTriangles[triangle] = 1u;
        }
        for(u32 corner = 0u; corner < s_MeshletTriangleIndexCount; ++corner){
            const u32 first = triangleIndices[base + corner];
            const u32 second = triangleIndices[base + (corner + 1u) % s_MeshletTriangleIndexCount];
            edges.push_back(MakeTriangleEdge(first, second, triangle));
        }
    }
    Sort(edges.begin(), edges.end(), [](const TriangleEdge& first, const TriangleEdge& second)noexcept{ return first.positions < second.positions; });
    for(usize first = 0u; first < edges.size();){
        usize end = first + 1u;
        while(end < edges.size() && edges[end].positions == edges[first].positions)
            ++end;
        const b8 validEdge = end - first == s_ManifoldEdgeTriangleCount &&edges[first].forward != edges[first + 1u].forward;
        for(usize index = first; index < end; ++index){
            JoinComponents(edges[first].triangle, edges[index].triangle, parents, ranks);
            if(!validEdge)
                invalidTriangles[edges[index].triangle] = 1u;
        }
        first = end;
    }

    Vector<u8, Core::Alloc::ScratchArena> invalidComponents(triangleCount, 0u, scratchArena);
    for(u32 triangle = 0u; triangle < static_cast<u32>(triangleCount); ++triangle){
        if(invalidTriangles[triangle] != 0u)
            invalidComponents[FindComponent(triangle, parents)] = 1u;
    }
    Vector<f64, Core::Alloc::ScratchArena> componentVolumes(triangleCount, 0.0, scratchArena);
    u32 originComponent = Limit<u32>::s_Max;
    Double4 origin{};
    for(u32 triangle = 0u; triangle < static_cast<u32>(triangleCount); ++triangle){
        const u32 component = FindComponent(triangle, parents);
        if(invalidComponents[component] != 0u)
            continue;
        const usize base = static_cast<usize>(triangle) * s_MeshletTriangleIndexCount;
        if(component != originComponent){
            origin = LoadTrianglePositionDouble(positions[triangleIndices[static_cast<usize>(component) * s_MeshletTriangleIndexCount]]);
            originComponent = component;
        }
        const Double4 aValue = LoadTrianglePositionDouble(positions[triangleIndices[base]]);
        const Double4 bValue = LoadTrianglePositionDouble(positions[triangleIndices[base + 1u]]);
        const Double4 cValue = LoadTrianglePositionDouble(positions[triangleIndices[base + 2u]]);
        componentVolumes[component] += RelativeTriangleVolume(aValue, bValue, cValue, origin).x;
    }
    for(u32 triangle = 0u; triangle < static_cast<u32>(triangleCount); ++triangle){
        const u32 component = FindComponent(triangle, parents);
        if(invalidComponents[component] == 0u && componentVolumes[component] != 0.0)
            words[triangle / s_MeshletSolidTriangleWordBits] |= 1u << (triangle % s_MeshletSolidTriangleWordBits);
    }
    return words;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

