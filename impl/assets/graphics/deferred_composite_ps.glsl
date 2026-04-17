// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#version 460

layout(set = 0, binding = 0) uniform texture2D g_DeferredAlbedo;
layout(set = 0, binding = 1) uniform texture2D g_AvboitAccumColor;
layout(set = 0, binding = 2) uniform texture2D g_AvboitAccumExtinction;
layout(set = 0, binding = 3) uniform sampler g_DeferredSampler;

layout(location = 0) in vec2 inUv;
layout(location = 0) out vec4 outColor;

void main(){
    const vec3 opaqueColor = texture(sampler2D(g_DeferredAlbedo, g_DeferredSampler), inUv).rgb;
    const vec4 transparentColorAccum = texture(sampler2D(g_AvboitAccumColor, g_DeferredSampler), inUv);
    const float transparentExtinction = texture(sampler2D(g_AvboitAccumExtinction, g_DeferredSampler), inUv).r;

    const vec3 transparentColor = transparentColorAccum.a > 0.00001
        ? transparentColorAccum.rgb / transparentColorAccum.a
        : vec3(0.0)
    ;
    const float transparentTransmission = exp(-min(transparentExtinction, 16.0));
    outColor = vec4(transparentColor * (1.0 - transparentTransmission) + opaqueColor * transparentTransmission, 1.0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
