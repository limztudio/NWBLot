struct VSInput{
    float3 position : POSITION;
    float3 color : COLOR0;
};

struct VSOutput{
    float4 position : SV_Position;
    float3 color : COLOR0;
};

float3x3 BuildRotationY(float angle){
    const float c = cos(angle);
    const float s = sin(angle);
    return float3x3(
        c,   0.0, s,
        0.0, 1.0, 0.0,
       -s,   0.0, c
    );
}

float3x3 BuildRotationX(float angle){
    const float c = cos(angle);
    const float s = sin(angle);
    return float3x3(
        1.0, 0.0, 0.0,
        0.0, c,   s,
        0.0, -s,  c
    );
}

float4 BuildClipPosition(float3 worldPosition){
    const float3x3 rotY = BuildRotationY(0.82);
    const float3x3 rotX = BuildRotationX(0.94);

    float3 p = mul(rotY, mul(rotX, worldPosition));
    p.z += 2.2;

    const float invZ = 1.0 / p.z;
    const float ndcX = p.x * invZ;
    const float ndcY = p.y * invZ;
    const float ndcZ = (p.z - 1.0) * 0.5;
    return float4(ndcX, ndcY, ndcZ, 1.0);
}

VSOutput main(VSInput input){
    VSOutput output;
    output.color = nwbProjectShadeVertex(input.color);
    output.position = BuildClipPosition(input.position);
    return output;
}
