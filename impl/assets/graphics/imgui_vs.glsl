// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#version 460

layout(push_constant) uniform PushConstants{
    vec4 scaleTranslate;
} pc;

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec2 outUv;
layout(location = 1) out vec4 outColor;

void main(){
    outUv = inUv;
    outColor = inColor;
    gl_Position = vec4(inPosition * pc.scaleTranslate.xy + pc.scaleTranslate.zw, 0.0, 1.0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

