// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#version 460

#include "bxdf.glsli"
#include "mesh_pixel_io.glsli"

void main(){
    const mediump vec3 baseColor = clamp(nwbProjectBxdfPixel(inColor.rgb), vec3(0.0), vec3(1.0));
    const mediump vec3 shadedColor = clamp(
        nwbProjectApplyDirectionalShading(baseColor, inNormal, inTangent, inWorldPosition),
        vec3(0.0),
        vec3(1.0)
    );
    outColor = vec4(vec3(1.0) - shadedColor, inColor.a);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

