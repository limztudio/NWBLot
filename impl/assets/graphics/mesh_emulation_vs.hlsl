struct NwbProjectEmulationVertexInput{
    float4 position : POSITION;
    float3 color : COLOR0;
};

struct NwbProjectPixelInput{
    float4 position : SV_Position;
    float3 color : COLOR0;
};

NwbProjectPixelInput main(NwbProjectEmulationVertexInput input){
    NwbProjectPixelInput output;
    output.position = input.position;
    output.color = input.color;
    return output;
}
