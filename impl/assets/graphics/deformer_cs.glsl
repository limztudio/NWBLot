// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#version 460

layout(local_size_x = 64) in;

struct NwbDeformerMorphRange{
    uint firstDelta;
    uint deltaCount;
    float weight;
    uint padding;
};

struct NwbDeformerMorphDelta{
    uvec4 vertex;
    vec4 deltaPosition;
    vec4 deltaNormal;
    vec4 deltaTangent;
};

layout(std430, binding = 0) readonly buffer NwbDeformerRestVerticesBuffer{
    float nwbDeformerRestVertexScalars[];
};

layout(std430, binding = 1) buffer NwbDeformerDeformedVerticesBuffer{
    float nwbDeformerDeformedVertexScalars[];
};

layout(std430, binding = 2) readonly buffer NwbDeformerMorphRangesBuffer{
    NwbDeformerMorphRange nwbDeformerMorphRanges[];
};

layout(std430, binding = 3) readonly buffer NwbDeformerMorphDeltasBuffer{
    NwbDeformerMorphDelta nwbDeformerMorphDeltas[];
};

layout(push_constant) uniform NwbDeformerPushConstants{
    uvec4 payload;
} g_NwbDeformerPushConstants;

uint nwbDeformerVertexCount(){
    return g_NwbDeformerPushConstants.payload.x;
}

uint nwbDeformerMorphCount(){
    return g_NwbDeformerPushConstants.payload.y;
}

uint nwbDeformerRestScalarStride(){
    return g_NwbDeformerPushConstants.payload.z;
}

uint nwbDeformerDeformedScalarStride(){
    return g_NwbDeformerPushConstants.payload.w;
}

void nwbDeformerCopyRestPayload(const uint vertexId, const uint restBase, const uint deformedBase){
    for(uint i = 0u; i < nwbDeformerRestScalarStride(); ++i)
        nwbDeformerDeformedVertexScalars[deformedBase + i] = nwbDeformerRestVertexScalars[restBase + i];
}

void main(){
    const uint vertexId = gl_GlobalInvocationID.x;
    if(vertexId >= nwbDeformerVertexCount())
        return;

    const uint restBase = vertexId * nwbDeformerRestScalarStride();
    const uint deformedBase = vertexId * nwbDeformerDeformedScalarStride();

    vec3 position = vec3(
        nwbDeformerRestVertexScalars[restBase + 0u],
        nwbDeformerRestVertexScalars[restBase + 1u],
        nwbDeformerRestVertexScalars[restBase + 2u]
    );
    vec3 normal = vec3(
        nwbDeformerRestVertexScalars[restBase + 3u],
        nwbDeformerRestVertexScalars[restBase + 4u],
        nwbDeformerRestVertexScalars[restBase + 5u]
    );
    vec4 tangent = vec4(
        nwbDeformerRestVertexScalars[restBase + 6u],
        nwbDeformerRestVertexScalars[restBase + 7u],
        nwbDeformerRestVertexScalars[restBase + 8u],
        nwbDeformerRestVertexScalars[restBase + 9u]
    );

    for(uint morphIndex = 0u; morphIndex < nwbDeformerMorphCount(); ++morphIndex){
        const NwbDeformerMorphRange morph = nwbDeformerMorphRanges[morphIndex];
        for(uint deltaIndex = 0u; deltaIndex < morph.deltaCount; ++deltaIndex){
            const NwbDeformerMorphDelta delta = nwbDeformerMorphDeltas[morph.firstDelta + deltaIndex];
            if(delta.vertex.x != vertexId)
                continue;

            position += morph.weight * delta.deltaPosition.xyz;
            normal += morph.weight * delta.deltaNormal.xyz;
            tangent += morph.weight * delta.deltaTangent;
        }
    }

    nwbDeformerCopyRestPayload(vertexId, restBase, deformedBase);
    nwbDeformerDeformedVertexScalars[deformedBase + 0u] = position.x;
    nwbDeformerDeformedVertexScalars[deformedBase + 1u] = position.y;
    nwbDeformerDeformedVertexScalars[deformedBase + 2u] = position.z;
    nwbDeformerDeformedVertexScalars[deformedBase + 3u] = normal.x;
    nwbDeformerDeformedVertexScalars[deformedBase + 4u] = normal.y;
    nwbDeformerDeformedVertexScalars[deformedBase + 5u] = normal.z;
    nwbDeformerDeformedVertexScalars[deformedBase + 6u] = tangent.x;
    nwbDeformerDeformedVertexScalars[deformedBase + 7u] = tangent.y;
    nwbDeformerDeformedVertexScalars[deformedBase + 8u] = tangent.z;
    nwbDeformerDeformedVertexScalars[deformedBase + 9u] = tangent.w;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
