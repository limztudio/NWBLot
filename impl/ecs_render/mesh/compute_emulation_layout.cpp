// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compute_emulation_layout.h"

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ResolveComputeEmulationLayout(
    const u64 localVertexRefByteSize,
    const u32 primitiveIndexCount,
    ComputeEmulationLayout& layout)noexcept{
    layout = {};
    if(localVertexRefByteSize == 0u || localVertexRefByteSize % sizeof(MeshletLocalVertexRef) != 0u || primitiveIndexCount == 0u)
        return false;
    constexpr u64 s_Stride = NWB_MESH_EMULATION_VERTEX_BYTE_SIZE;
    constexpr u64 s_AddressableBytes = static_cast<u64>(Limit<u32>::s_Max) + 1u;
    const u64 vertexCount = localVertexRefByteSize / sizeof(MeshletLocalVertexRef) + NWB_MESH_EMULATION_FIRST_VERTEX_INDEX;
    if(vertexCount >= s_AddressableBytes / s_Stride)
        return false;
    const u64 indexByteOffset = vertexCount * s_Stride;
    const u64 indexedByteSize = indexByteOffset + static_cast<u64>(primitiveIndexCount) * sizeof(u32);
    // Every raw index store must fit its uint byte address, including the final four-byte element.
    if(indexedByteSize > s_AddressableBytes)
        return false;
    const u64 alignedIndexedByteSize = AlignUp(indexedByteSize, s_Stride);
    const u64 expandedByteSize = static_cast<u64>(primitiveIndexCount) * s_Stride;
    layout.bufferByteSize = expandedByteSize > alignedIndexedByteSize ? expandedByteSize : alignedIndexedByteSize;
    layout.indexByteOffset = static_cast<u32>(indexByteOffset);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

