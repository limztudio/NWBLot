// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../bindless/bindless_heap.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbDeferredDebugLinePushConstants{
    row_major float4x4 viewProj;
    uint vertexBufferSlot;
    uint pad0;
    uint pad1;
    uint pad2;
};

[[vk::push_constant]] ConstantBuffer<NwbDeferredDebugLinePushConstants> g_NwbDeferredDebugLinePushConstants;

struct NwbDeferredDebugLineVertexOutput{
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color : COLOR0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NwbDeferredDebugLineVertexOutput main(uint vertexIndex : SV_VertexID){
    const uint baseByte = vertexIndex * 32u;
    const ByteAddressBuffer buf = nwbHeapRawBufferUniform(g_NwbDeferredDebugLinePushConstants.vertexBufferSlot);
    const float4 posPad = asfloat(buf.Load4(baseByte));
    const float4 vertColor = asfloat(buf.Load4(baseByte + 16u));

    NwbDeferredDebugLineVertexOutput output;
    output.position = mul(g_NwbDeferredDebugLinePushConstants.viewProj, float4(posPad.xyz, 1.0f));
    output.color = vertColor;
    return output;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

