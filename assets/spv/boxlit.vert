#version 450
// Shadowed instanced-box vertex stage (V178): box.vert plus the world
// position the fragment stage needs for the shadow-map lookup.
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNrm;
layout(location = 2) in vec4 i0;
layout(location = 3) in vec4 i1;
layout(location = 4) in vec4 i2;
layout(location = 5) in vec4 i3;
layout(location = 6) in vec4 iCol;
layout(push_constant) uniform PC {
    mat4 viewProj;
    mat4 lightVP;
    vec4 sunShadow;   // xyz = sun direction, w = shadows on/off
} pc;
layout(location = 0) out vec3 vNrm;
layout(location = 1) out vec4 vCol;
layout(location = 2) out vec3 vWorld;
void main() {
    mat4 M = mat4(i0, i1, i2, i3);
    vec4 world = M * vec4(inPos, 1.0);
    vNrm = mat3(M) * inNrm;
    vCol = iCol;
    vWorld = world.xyz;
    gl_Position = pc.viewProj * world;
}
