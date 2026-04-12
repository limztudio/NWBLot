#version 460

layout(location = 0) out vec2 outUv;

void main(){
    const vec2 positions[3] = vec2[3](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );

    const vec2 position = positions[gl_VertexIndex];
    gl_Position = vec4(position, 0.0, 1.0);
    outUv = position * vec2(0.5, -0.5) + vec2(0.5);
}
