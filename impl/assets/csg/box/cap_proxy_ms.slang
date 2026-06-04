// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../../graphics/csg/cap_proxy/common.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


float3 nwbCsgCapProxyBoxCorner(const float3 halfExtents, const uint corner){
    return float3(
        (corner & 1u) != 0u ? halfExtents.x : -halfExtents.x,
        (corner & 2u) != 0u ? halfExtents.y : -halfExtents.y,
        (corner & 4u) != 0u ? halfExtents.z : -halfExtents.z
    );
}

void nwbCsgCapProxyBoxFace(const uint face, out float3 normal, out uint4 corners){
    switch(face){
    case 0u: normal = float3( 1.0,  0.0,  0.0); corners = uint4(1u, 5u, 7u, 3u); return;
    case 1u: normal = float3(-1.0,  0.0,  0.0); corners = uint4(0u, 2u, 6u, 4u); return;
    case 2u: normal = float3( 0.0,  1.0,  0.0); corners = uint4(2u, 3u, 7u, 6u); return;
    case 3u: normal = float3( 0.0, -1.0,  0.0); corners = uint4(0u, 4u, 5u, 1u); return;
    case 4u: normal = float3( 0.0,  0.0,  1.0); corners = uint4(4u, 6u, 7u, 5u); return;
    default: normal = float3( 0.0,  0.0, -1.0); corners = uint4(0u, 1u, 3u, 2u); return;
    }
}

void nwbCsgCapProxyBoxPrimitive(const uint localPrimitive, out uint3 indices){
    const uint face = localPrimitive / 2u;
    const uint baseVertex = face * 4u;
    indices = (localPrimitive & 1u) == 0u
        ? uint3(baseVertex + 0u, baseVertex + 1u, baseVertex + 2u)
        : uint3(baseVertex + 0u, baseVertex + 2u, baseVertex + 3u)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbCsgCapProxyShapeContext{
    float3 halfExtents;
};

NwbCsgCapProxyShapeContext nwbCsgCapProxyBuildShapeContext(const NwbCsgCapProxyGpuData item, const NwbCsgCutter cutter){
    NwbCsgCapProxyShapeContext context;
    context.halfExtents = max(cutter.parameter0.xyz, float3(0.0001));
    return context;
}

uint nwbCsgCapProxyShapeVertexCount(const NwbCsgCapProxyShapeContext context){
    return 24u;
}

uint nwbCsgCapProxyShapePrimitiveCount(const NwbCsgCapProxyShapeContext context){
    return 12u;
}

NwbMeshRasterVertex nwbCsgCapProxyShapeVertex(
    const NwbCsgCapProxyShapeContext context,
    const NwbCsgCapProxyGpuData item,
    const NwbCsgCutter cutter,
    const uint localVertex
){
    const uint face = localVertex / 4u;
    const uint cornerInFace = localVertex - face * 4u;
    float3 shapeNormal;
    uint4 corners;
    nwbCsgCapProxyBoxFace(face, shapeNormal, corners);
    const uint corner = corners[cornerInFace];
    const float3 shapePosition = nwbCsgCapProxyBoxCorner(context.halfExtents, corner);
    return nwbCsgCapProxyBuildVertex(
        nwbCsgTransformShapeToWorld(shapePosition, cutter),
        shapeNormal,
        cutter,
        item.receiverCutterShapePass.x,
        item.receiverCutterShapePass.y,
        item.color
    );
}

void nwbCsgCapProxyShapePrimitive(
    const NwbCsgCapProxyShapeContext context,
    const uint localPrimitive,
    out uint3 indices
){
    nwbCsgCapProxyBoxPrimitive(localPrimitive, indices);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../../graphics/csg/cap_proxy/ms_main.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
