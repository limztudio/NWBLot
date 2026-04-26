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

void main(){
    const vec3 baseColor = clamp(nwbProjectBxdfPixel(inColor.rgb), vec3(0.0), vec3(1.0));
    outColor = vec4(
        clamp(nwbProjectApplyDirectionalShading(baseColor, inNormal, inTangent, inWorldPosition), vec3(0.0), vec3(1.0)),
        inColor.a
    );
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

