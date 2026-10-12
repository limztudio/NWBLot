// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_topology.h"

#include <global/simd/vector.h>
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

static void JoinComponents(Vector<u32, Core::Alloc::ScratchArena>& parents, Vector<u8, Core::Alloc::ScratchArena>& ranks, const u32 first, const u32 second)noexcept{
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

[[nodiscard]] static bool DegenerateTriangle(const SIMDVectorDouble a, const SIMDVectorDouble b, const SIMDVectorDouble c)noexcept{
    const SIMDVectorDouble ab = VectorDoubleSubtract(b, a);
    const SIMDVectorDouble ac = VectorDoubleSubtract(c, a);
    const auto equalProducts = [](const SIMDVectorDouble lhs, const SIMDVectorDouble rhs)noexcept{
        const SIMDVectorDouble products = VectorDoubleMultiply(lhs, rhs);
        return VectorDoubleGetX(products) == VectorDoubleGetY(products);
    };
    // Each pair is evaluated only after the preceding cross component matched.
    return
        equalProducts(VectorDoubleSwizzle<1, 2, 3, 3>(ab), VectorDoubleSwizzle<2, 1, 3, 3>(ac))
        && equalProducts(VectorDoubleSwizzle<2, 0, 3, 3>(ab), VectorDoubleSwizzle<0, 2, 3, 3>(ac))
        && equalProducts(VectorDoubleSwizzle<0, 1, 3, 3>(ab), VectorDoubleSwizzle<1, 0, 3, 3>(ac))
    ;
}

[[nodiscard]] static SIMDVectorDouble RelativeTriangleVolume(const SIMDVectorDouble a, const SIMDVectorDouble b, const SIMDVectorDouble c, const SIMDVectorDouble origin)noexcept{
    const SIMDVectorDouble relativeA = VectorDoubleSubtract(a, origin);
    const SIMDVectorDouble relativeB = VectorDoubleSubtract(b, origin);
    const SIMDVectorDouble relativeC = VectorDoubleSubtract(c, origin);
    const SIMDVectorDouble ax = VectorDoubleSplatX(relativeA), ay = VectorDoubleSplatY(relativeA), az = VectorDoubleSplatZ(relativeA);
    const SIMDVectorDouble bx = VectorDoubleSplatX(relativeB), by = VectorDoubleSplatY(relativeB), bz = VectorDoubleSplatZ(relativeB);
    const SIMDVectorDouble cx = VectorDoubleSplatX(relativeC), cy = VectorDoubleSplatY(relativeC), cz = VectorDoubleSplatZ(relativeC);
    // Keep the full determinant expression and left-associated sum under the caller's contraction policy.
    return ax * (by * cz - bz * cy) + ay * (bz * cx - bx * cz) + az * (bx * cy - by * cx);
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
        if(a == b || b == c || c == a)
            invalidTriangles[triangle] = 1u;
        else{
            const SIMDVectorDouble aValue = ConvertVectorSingleToDouble(LoadFloat(positions[a]));
            const SIMDVectorDouble bValue = ConvertVectorSingleToDouble(LoadFloat(positions[b]));
            const SIMDVectorDouble cValue = ConvertVectorSingleToDouble(LoadFloat(positions[c]));
            if(DegenerateTriangle(aValue, bValue, cValue))
                invalidTriangles[triangle] = 1u;
        }
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
    u32 originComponent = Limit<u32>::s_Max;
    SIMDVectorDouble origin = VectorDoubleZero();
    for(u32 triangle = 0u; triangle < static_cast<u32>(triangleCount); ++triangle){
        const u32 component = FindComponent(parents, triangle);
        if(invalidComponents[component] != 0u)
            continue;
        const usize base = static_cast<usize>(triangle) * s_MeshletTriangleIndexCount;
        if(component != originComponent){
            origin = ConvertVectorSingleToDouble(LoadFloat(positions[triangleIndices[static_cast<usize>(component) * s_MeshletTriangleIndexCount]]));
            originComponent = component;
        }
        const SIMDVectorDouble aValue = ConvertVectorSingleToDouble(LoadFloat(positions[triangleIndices[base]]));
        const SIMDVectorDouble bValue = ConvertVectorSingleToDouble(LoadFloat(positions[triangleIndices[base + 1u]]));
        const SIMDVectorDouble cValue = ConvertVectorSingleToDouble(LoadFloat(positions[triangleIndices[base + 2u]]));
        componentVolumes[component] += VectorDoubleGetX(RelativeTriangleVolume(aValue, bValue, cValue, origin));
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

