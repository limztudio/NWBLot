// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "geometry_payload.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Visitor>
[[nodiscard]] b8 ForEachMeshletTriangleCorner(const Span<const MeshletDesc> meshlets, const Span<const MeshletLocalVertexRef> localVertexRefs, const Span<const u8> primitiveIndices, Visitor&& visitor)noexcept(
    noexcept(static_cast<b8>(!visitor(DeclVal<const MeshletDesc&>(), DeclVal<const usize&>(), DeclVal<const MeshletLocalVertexRef&>())))
){
    const usize localVertexRefCount = localVertexRefs.size();
    const usize primitiveIndexCount = primitiveIndices.size();

    for(usize meshletIndex = 0u; meshletIndex < meshlets.size(); ++meshletIndex){
        const MeshletDesc& meshlet = meshlets[meshletIndex];
        const u32 primitiveCount = MeshletPrimitiveCount(meshlet);

        for(u32 primitive = 0u; primitive < primitiveCount; ++primitive){
            const usize primitiveBase = static_cast<usize>(meshlet.primitiveOffset) + static_cast<usize>(primitive) * s_MeshletTriangleIndexCount;

            for(u32 corner = 0u; corner < s_MeshletTriangleIndexCount; ++corner){
                const usize primitiveByte = primitiveBase + corner;
                if(primitiveByte >= primitiveIndexCount)
                    return false;

                const u32 localVertexIndex = static_cast<u32>(primitiveIndices[primitiveByte]);
                const usize localVertexRefIndex = static_cast<usize>(meshlet.localVertexOffset) + localVertexIndex;
                if(localVertexRefIndex >= localVertexRefCount)
                    return false;

                if(!visitor(meshlet, primitiveByte, localVertexRefs[localVertexRefIndex]))
                    return false;
            }
        }
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

