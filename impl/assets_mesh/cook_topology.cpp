// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_topology.h"

#include <global/math/vector_double.h>
#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_mesh_cook_topology{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct TriangleEdge{
    u64 positions = 0u;
    u32 triangle = 0u;
    bool forward = false;
};

[[nodiscard]] static u32 FindComponent(Vector<u32, Core::Alloc::ScratchArena>& parents, u32 triangle)noexcept{
    while(parents[triangle] != triangle){
        parents[triangle] = parents[parents[triangle]];
        triangle = parents[triangle];
    }
    return triangle;
}

static void JoinComponents(
    Vector<u32, Core::Alloc::ScratchArena>& parents,
    Vector<u8, Core::Alloc::ScratchArena>& ranks,
    const u32 first,
    const u32 second
)noexcept{
    u32 firstRoot = FindComponent(parents, first);
    u32 secondRoot = FindComponent(parents, second);
    if(firstRoot == secondRoot)
        return;
    if(ranks[firstRoot] < ranks[secondRoot])
        Swap(firstRoot, secondRoot);
    parents[secondRoot] = firstRoot;
    if(ranks[firstRoot] == ranks[secondRoot])
        ++ranks[firstRoot];
}

[[nodiscard]] static bool DegenerateTriangle(const Float3U& a, const Float3U& b, const Float3U& c)noexcept{
    const SIMDVectorDouble abXAbYValue = (SIMDVectorDouble{ static_cast<f64>(b.x), static_cast<f64>(b.y) } - SIMDVectorDouble{ a.x, a.y });
    const f64 abX = abXAbYValue.x;
    const f64 abY = abXAbYValue.y;
    const SIMDVectorDouble abZAcXValue = (SIMDVectorDouble{ static_cast<f64>(b.z), static_cast<f64>(c.x) } - SIMDVectorDouble{ a.z, a.x });
    const f64 abZ = abZAcXValue.x;
    const f64 acX = abZAcXValue.y;
    const SIMDVectorDouble acYAcZValue = (SIMDVectorDouble{ static_cast<f64>(c.y), static_cast<f64>(c.z) } - SIMDVectorDouble{ a.y, a.z });
    const f64 acY = acYAcZValue.x;
    const f64 acZ = acYAcZValue.y;
    return abY * acZ == abZ * acY && abZ * acX == abX * acZ && abX * acY == abY * acX;
}

[[nodiscard]] static f64 RelativeTriangleVolume(
    const Float3U& a,
    const Float3U& b,
    const Float3U& c,
    const Float3U& origin
)noexcept{
    const SIMDVectorDouble aXAYValue = (SIMDVectorDouble{ static_cast<f64>(a.x), static_cast<f64>(a.y) } - SIMDVectorDouble{ origin.x, origin.y });
    const f64 aX = aXAYValue.x;
    const f64 aY = aXAYValue.y;
    const SIMDVectorDouble aZBXValue = (SIMDVectorDouble{ static_cast<f64>(a.z), static_cast<f64>(b.x) } - SIMDVectorDouble{ origin.z, origin.x });
    const f64 aZ = aZBXValue.x;
    const f64 bX = aZBXValue.y;
    const SIMDVectorDouble bYBZValue = (SIMDVectorDouble{ static_cast<f64>(b.y), static_cast<f64>(b.z) } - SIMDVectorDouble{ origin.y, origin.z });
    const f64 bY = bYBZValue.x;
    const f64 bZ = bYBZValue.y;
    const SIMDVectorDouble cXCYValue = (SIMDVectorDouble{ static_cast<f64>(c.x), static_cast<f64>(c.y) } - SIMDVectorDouble{ origin.x, origin.y });
    const f64 cX = cXCYValue.x;
    const f64 cY = cXCYValue.y;
    const f64 cZ = static_cast<f64>(c.z) - origin.z;
    return aX * (bY * cZ - bZ * cY) + aY * (bZ * cX - bX * cZ) + aZ * (bX * cY - bY * cX);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Vector<u32, Core::Alloc::ScratchArena>> BuildSolidTriangleWords(
    const Span<const Float3U> positions,
    const Span<const u32> triangleIndices,
    Core::Alloc::ScratchArena& scratchArena
){
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

    Vector<u32, Core::Alloc::ScratchArena> words(DivideUp(triangleCount, usize(32u)), 0u, scratchArena);
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
        if(a == b || b == c || c == a || DegenerateTriangle(positions[a], positions[b], positions[c]))
            invalidTriangles[triangle] = 1u;
        for(u32 corner = 0u; corner < s_MeshletTriangleIndexCount; ++corner){
            const u32 first = triangleIndices[base + corner];
            const u32 second = triangleIndices[base + (corner + 1u) % s_MeshletTriangleIndexCount];
            edges.push_back({(static_cast<u64>(Min(first, second)) << 32u) | Max(first, second), triangle, first < second});
        }
    }
    Sort(edges.begin(), edges.end(), [](const TriangleEdge& first, const TriangleEdge& second)noexcept{ return first.positions < second.positions; });
    for(usize first = 0u; first < edges.size();){
        usize end = first + 1u;
        while(end < edges.size() && edges[end].positions == edges[first].positions)
            ++end;
        const bool validEdge = end - first == 2u && edges[first].forward != edges[first + 1u].forward;
        for(usize index = first; index < end; ++index){
            JoinComponents(parents, ranks, edges[first].triangle, edges[index].triangle);
            if(!validEdge)
                invalidTriangles[edges[index].triangle] = 1u;
        }
        first = end;
    }

    Vector<u8, Core::Alloc::ScratchArena> invalidComponents(triangleCount, 0u, scratchArena);
    for(u32 triangle = 0u; triangle < static_cast<u32>(triangleCount); ++triangle){
        if(invalidTriangles[triangle] != 0u)
            invalidComponents[FindComponent(parents, triangle)] = 1u;
    }
    Vector<f64, Core::Alloc::ScratchArena> componentVolumes(triangleCount, 0.0, scratchArena);
    for(u32 triangle = 0u; triangle < static_cast<u32>(triangleCount); ++triangle){
        const u32 component = FindComponent(parents, triangle);
        if(invalidComponents[component] != 0u)
            continue;
        const usize base = static_cast<usize>(triangle) * s_MeshletTriangleIndexCount;
        const Float3U& origin = positions[triangleIndices[static_cast<usize>(component) * s_MeshletTriangleIndexCount]];
        componentVolumes[component] += RelativeTriangleVolume(
            positions[triangleIndices[base]], positions[triangleIndices[base + 1u]], positions[triangleIndices[base + 2u]], origin
        );
    }
    for(u32 triangle = 0u; triangle < static_cast<u32>(triangleCount); ++triangle){
        const u32 component = FindComponent(parents, triangle);
        if(invalidComponents[component] == 0u && componentVolumes[component] != 0.0)
            words[triangle / 32u] |= 1u << (triangle % 32u);
    }
    return words;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

