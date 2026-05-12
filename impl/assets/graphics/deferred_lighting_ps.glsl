// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#version 460

#define NWB_SCENE_SHADING_SET 0
#define NWB_SCENE_SHADING_BINDING 5

#include "bxdf_lighting.glsli"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


layout(set = 0, binding = 0) uniform texture2D g_GBufferBaseColor;
layout(set = 0, binding = 1) uniform texture2D g_GBufferNormal;
layout(set = 0, binding = 2) uniform texture2D g_GBufferWorldPosition;
layout(set = 0, binding = 3) uniform texture2D g_GBufferDepth;
layout(set = 0, binding = 4) uniform sampler g_DeferredSampler;

layout(location = 0) in vec2 inUv;
layout(location = 0) out vec4 outColor;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


vec3 nwbDeferredDecodeNormal(vec3 value){
    return nwbProjectSafeNormalize(value * 2.0 - 1.0, vec3(0.0, 0.0, 1.0));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void main(){
    const vec3 baseColor = texture(sampler2D(g_GBufferBaseColor, g_DeferredSampler), inUv).rgb;
    const float depth = texture(sampler2D(g_GBufferDepth, g_DeferredSampler), inUv).r;
    if(depth >= 0.999999){
        outColor = vec4(baseColor, 1.0);
        return;
    }

    const vec3 normal = nwbDeferredDecodeNormal(texture(sampler2D(g_GBufferNormal, g_DeferredSampler), inUv).rgb);
    const vec3 worldPosition = texture(sampler2D(g_GBufferWorldPosition, g_DeferredSampler), inUv).xyz;
    const vec3 shadedColor = clamp(
        nwbProjectApplyDirectionalShading(baseColor, normal, vec4(0.0, 0.0, 0.0, 1.0), worldPosition),
        vec3(0.0),
        vec3(1.0)
    );

    outColor = vec4(shadedColor, 1.0);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

