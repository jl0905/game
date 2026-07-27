#version 450
// Lambert fragment stage (V157): the same 0.55 + 0.45*ndl the raylib
// instanced army uses, so the Vulkan frame matches the GL one.
layout(location = 0) in vec3 vNrm;
layout(location = 1) in vec4 vCol;

layout(push_constant) uniform PC {
    mat4 viewProj;
    vec4 sunDir;
} pc;

layout(location = 0) out vec4 outCol;

void main() {
    float ndl = max(dot(normalize(vNrm), -pc.sunDir.xyz), 0.0);
    float shade = 0.55 + 0.45 * ndl;
    outCol = vec4(vCol.rgb * shade, vCol.a);
}
