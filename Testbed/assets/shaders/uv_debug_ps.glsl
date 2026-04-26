// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#version 460

#include "bxdf.glsli"

layout(location = 0) in vec4 inColor;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inUv0;
layout(location = 4) in vec3 inWorldPosition;
layout(location = 0) out vec4 outColor;

vec3 nwbProjectUvDebug(const vec2 uv0){
    const vec2 wrappedUv = fract(uv0);
    const vec2 checkerCell = floor(wrappedUv * 8.0);
    const float checker = mod(checkerCell.x + checkerCell.y, 2.0);
    const vec3 uvColor = vec3(wrappedUv, 1.0 - wrappedUv.x);
    return mix(uvColor * 0.55, uvColor, checker);
}

void main(){
    const vec3 baseColor = clamp(nwbProjectUvDebug(inUv0) * inColor.rgb, vec3(0.0), vec3(1.0));
    outColor = vec4(
        clamp(nwbProjectApplyDirectionalShading(baseColor, inNormal, inTangent, inWorldPosition), vec3(0.0), vec3(1.0)),
        inColor.a
    );
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

