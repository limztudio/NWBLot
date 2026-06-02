// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "csg_cap_source.h"

#include <core/common/log.h>
#include <impl/assets_mesh/asset.h>
#include <impl/assets_mesh/meshlet_payload_packing.h>
#include <impl/assets_mesh/meshlet_ref_decode.h>

#include <global/basic_string.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_cap_source{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool DecodeMeshletObjectVertex(
    const Mesh& mesh,
    const MeshletDesc& meshlet,
    const u32 localVertexIndex,
    CsgCapMeshVertex& outVertex
){
    outVertex = CsgCapMeshVertex{};
    if(localVertexIndex >= MeshletVertexCount(meshlet))
        return false;

    const usize localVertexOffset = static_cast<usize>(meshlet.localVertexOffset) + static_cast<usize>(localVertexIndex);
    if(localVertexOffset >= mesh.meshletLocalVertexRefs().size())
        return false;

    const MeshletLocalVertexRef& localVertexRef = mesh.meshletLocalVertexRefs()[localVertexOffset];
    MeshletPositionStreamRef positionRef;
    if(!DecodeMeshletPositionRef(
        mesh.meshletPositionRefDeltas().data(),
        mesh.meshletPositionRefDeltas().size(),
        meshlet,
        localVertexRef.localDeformedPosition,
        false,
        positionRef
    ))
        return false;
    if(positionRef.position >= mesh.positionStream().size())
        return false;

    MeshletAttributeStreamRef attributeRef;
    if(!DecodeMeshletAttributeRef(
        mesh.meshletAttributeRefDeltas().data(),
        mesh.meshletAttributeRefDeltas().size(),
        meshlet,
        localVertexRef.localAttribute,
        attributeRef
    ))
        return false;
    if(
        attributeRef.normal >= mesh.normalStream().size()
        || attributeRef.tangent >= mesh.tangentStream().size()
        || attributeRef.uv0 >= mesh.uv0Stream().size()
        || attributeRef.color >= mesh.colorStream().size()
    )
        return false;

    StoreFloat(VectorSetW(LoadFloat(mesh.positionStream()[positionRef.position]), 0.0f), &outVertex.position);
    StoreFloat(VectorSetW(LoadFloat(LoadHalf4U(mesh.normalStream()[attributeRef.normal])), 0.0f), &outVertex.normal);
    StoreFloat(LoadFloat(LoadHalf4U(mesh.tangentStream()[attributeRef.tangent])), &outVertex.tangent);
    StoreFloat(VectorSetW(LoadFloat(mesh.uv0Stream()[attributeRef.uv0]), 0.0f), &outVertex.uv0);
    StoreFloat(LoadFloat(LoadHalf4U(mesh.colorStream()[attributeRef.color])), &outVertex.color);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ECSRenderCsgCapSource::BuildCapTriangles(
    const Name& meshName,
    const Mesh& mesh,
    CsgCapMeshTriangleVector& outTriangles
){
    outTriangles.clear();

    usize triangleCapacity = 0u;
    for(const MeshletDesc& meshlet : mesh.meshlets()){
        const usize primitiveCount = static_cast<usize>(MeshletPrimitiveCount(meshlet));
        if(primitiveCount > Limit<usize>::s_Max - triangleCapacity){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh '{}' CSG cap triangle count overflows")
                , StringConvert(meshName.c_str())
            );
            return false;
        }
        triangleCapacity += primitiveCount;
    }
    outTriangles.reserve(triangleCapacity);

    for(const MeshletDesc& meshlet : mesh.meshlets()){
        const usize primitiveCount = static_cast<usize>(MeshletPrimitiveCount(meshlet));
        const usize primitiveByteBegin = static_cast<usize>(meshlet.primitiveOffset);
        const usize primitiveByteCount = primitiveCount * 3u;
        if(primitiveByteBegin > mesh.meshletPrimitiveIndices().size() || primitiveByteCount > mesh.meshletPrimitiveIndices().size() - primitiveByteBegin){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh '{}' has invalid CSG cap primitive index range")
                , StringConvert(meshName.c_str())
            );
            return false;
        }

        for(usize primitiveIndex = 0u; primitiveIndex < primitiveCount; ++primitiveIndex){
            CsgCapMeshTriangle triangle;
            for(u32 corner = 0u; corner < 3u; ++corner){
                const usize primitiveByteOffset = primitiveByteBegin + primitiveIndex * 3u + static_cast<usize>(corner);
                const u32 localVertexIndex = static_cast<u32>(mesh.meshletPrimitiveIndices()[primitiveByteOffset]);
                if(!__hidden_csg_cap_source::DecodeMeshletObjectVertex(mesh, meshlet, localVertexIndex, triangle.vertices[corner])){
                    NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh '{}' failed to decode CSG cap source vertex")
                        , StringConvert(meshName.c_str())
                    );
                    return false;
                }
            }
            outTriangles.push_back(triangle);
        }
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

