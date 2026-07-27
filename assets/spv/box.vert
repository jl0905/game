#version 450
// The instanced-box vertex stage (V157): one cube mesh, per-instance
// transform rows + colour — the same transform-list shape the game's
// V126/V128 batcher already produces, now on Vulkan.
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNrm;
layout(location = 2) in vec4 i0;
layout(location = 3) in vec4 i1;
layout(location = 4) in vec4 i2;
layout(location = 5) in vec4 i3;
layout(location = 6) in vec4 iCol;

layout(push_constant) uniform PC {
    mat4 viewProj;
    vec4 sunDir;   // xyz = direction, w unused
} pc;

layout(location = 0) out vec3 vNrm;
layout(location = 1) out vec4 vCol;

void main() {
    mat4 M = mat4(i0, i1, i2, i3);
    vec4 world = M * vec4(inPos, 1.0);
    vNrm = mat3(M) * inNrm;
    vCol = iCol;
    gl_Position = pc.viewProj * world;
}
