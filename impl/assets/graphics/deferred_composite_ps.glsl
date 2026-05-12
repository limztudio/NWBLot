// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#version 460


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


layout(set = 0, binding = 0) uniform texture2D g_OpaqueColor;
layout(set = 0, binding = 1) uniform texture2D g_AvboitAccumColor;
layout(set = 0, binding = 2) uniform texture2D g_AvboitAccumExtinction;
layout(set = 0, binding = 3) uniform sampler g_DeferredSampler;

layout(location = 0) in vec2 inUv;
layout(location = 0) out vec4 outColor;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void main(){
    const vec3 opaqueColor = texture(sampler2D(g_OpaqueColor, g_DeferredSampler), inUv).rgb;
    const vec4 transparentColorAccum = texture(sampler2D(g_AvboitAccumColor, g_DeferredSampler), inUv);
    const float transparentExtinction = texture(sampler2D(g_AvboitAccumExtinction, g_DeferredSampler), inUv).r;
    const float totalTransmittance = exp(-min(transparentExtinction, 16.0));

    outColor = vec4(transparentColorAccum.rgb + opaqueColor * totalTransmittance, 1.0);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

