// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "csg_cap_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderCsgCapDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_cap_loop{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool FindNextLoopEdge(
    const CapEdgeVector& edges,
    const CapByteVector& usedEdges,
    const u32 currentPoint,
    const u32 previousPoint,
    usize& outEdgeIndex,
    u32& outNextPoint
){
    outEdgeIndex = 0u;
    outNextPoint = 0u;
    for(usize edgeIndex = 0u; edgeIndex < edges.size(); ++edgeIndex){
        if(usedEdges[edgeIndex])
            continue;

        const CapEdge& edge = edges[edgeIndex];
        if(edge.a == currentPoint){
            outEdgeIndex = edgeIndex;
            outNextPoint = edge.b;
            if(edge.b != previousPoint)
                return true;
        }
        else if(edge.b == currentPoint){
            outEdgeIndex = edgeIndex;
            outNextPoint = edge.a;
            if(edge.a != previousPoint)
                return true;
        }
    }

    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AppendCapLoops(
    CsgCapVertexGpuDataVector& vertices,
    const CapPointVector& points,
    const CapEdgeVector& edges,
    const CapCutterEval& cutterEval,
    const u32 receiverIndex,
    const u32 cutterIndex,
    Core::Alloc::ScratchArena& scratchArena
){
    CapByteVector usedEdges(scratchArena);
    usedEdges.resize(edges.size(), 0u);

    CapIndexVector loop(scratchArena);
    for(usize edgeIndex = 0u; edgeIndex < edges.size(); ++edgeIndex){
        if(usedEdges[edgeIndex])
            continue;

        const CapEdge& edge = edges[edgeIndex];
        loop.clear();
        loop.push_back(edge.a);
        loop.push_back(edge.b);
        usedEdges[edgeIndex] = 1u;

        bool closed = false;
        u32 previousPoint = edge.a;
        u32 currentPoint = edge.b;
        for(usize guard = 0u; guard < edges.size(); ++guard){
            usize nextEdgeIndex = 0u;
            u32 nextPoint = 0u;
            if(!__hidden_csg_cap_loop::FindNextLoopEdge(edges, usedEdges, currentPoint, previousPoint, nextEdgeIndex, nextPoint))
                break;

            usedEdges[nextEdgeIndex] = 1u;
            if(nextPoint == loop[0u]){
                closed = true;
                break;
            }

            loop.push_back(nextPoint);
            previousPoint = currentPoint;
            currentPoint = nextPoint;
        }

        if(!closed || loop.size() < 3u)
            continue;
        if(!AppendEarClippedTriangulation(vertices, points, loop, cutterEval, receiverIndex, cutterIndex, scratchArena))
            return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

