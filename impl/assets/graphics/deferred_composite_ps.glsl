#version 460

layout(set = 0, binding = 0) uniform texture2D g_DeferredAlbedo;
layout(set = 0, binding = 1) uniform sampler g_DeferredSampler;

layout(location = 0) in vec2 inUv;
layout(location = 0) out vec4 outColor;

void main(){
    outColor = texture(sampler2D(g_DeferredAlbedo, g_DeferredSampler), inUv);
}
