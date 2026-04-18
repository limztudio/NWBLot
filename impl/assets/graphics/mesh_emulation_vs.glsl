// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#version 460

layout(location = 0) in vec4 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inUv0;
layout(location = 4) in vec4 inColor;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec4 outTangent;
layout(location = 3) out vec2 outUv0;

void main(){
    gl_Position = inPosition;
    outColor = inColor;
    outNormal = inNormal;
    outTangent = inTangent;
    outUv0 = inUv0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
