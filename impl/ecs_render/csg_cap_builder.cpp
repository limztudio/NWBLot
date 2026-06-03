// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "csg_cap_builder.h"

#include "csg_cap_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_cap_builder{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool AppendCapGeometry(
    const CsgCapMeshTriangleVector& triangles,
    const Scene::TransformComponent* transform,
    const CsgCutterGpuData& cutter,
    const CsgShapeTypeInfo* shapeType,
    const u8* parameterBytes,
    const usize parameterByteSize,
    const u32 receiverIndex,
    const u32 cutterIndex,
    CsgCapVertexGpuDataVector& vertices,
    Core::Alloc::ScratchArena& scratchArena
){
    const ECSRenderCsgCapDetail::CapCutterEval cutterEval{
        cutter,
        shapeType,
        parameterBytes,
        parameterByteSize,
        LoadFloat(cutter.worldToShape),
        LoadFloat(cutter.shapeToWorld),
    };
    if(!ECSRenderCsgCapDetail::CutterSupportsCap(cutterEval))
        return true;

    ECSRenderCsgCapDetail::CapPointVector points(scratchArena);
    ECSRenderCsgCapDetail::CapEdgeVector edges(scratchArena);
    if(!ECSRenderCsgCapDetail::BuildCapSegments(triangles, transform, cutterEval, points, edges))
        return false;
    if(points.empty() || edges.empty())
        return true;

    return ECSRenderCsgCapDetail::AppendCapLoops(vertices, points, edges, cutterEval, receiverIndex, cutterIndex, scratchArena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderCsgCapBuilder{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AppendCapGeometry(
    const CsgCapMeshTriangleVector& triangles,
    const Scene::TransformComponent* transform,
    const u32 receiverIndex,
    const CsgCutterGpuData& cutter,
    const CsgShapeTypeInfo* shapeType,
    const u8* parameterBytes,
    const usize parameterByteSize,
    const u32 cutterIndex,
    CsgCapVertexGpuDataVector& vertices,
    Core::Alloc::ScratchArena& scratchArena
){
    return __hidden_csg_cap_builder::AppendCapGeometry(
        triangles,
        transform,
        cutter,
        shapeType,
        parameterBytes,
        parameterByteSize,
        receiverIndex,
        cutterIndex,
        vertices,
        scratchArena
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

