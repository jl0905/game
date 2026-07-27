#version 450
// Static mesh stage (V158): per-vertex position/normal/colour — the
// terrain path of the Vulkan backend.
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNrm;
layout(location = 2) in vec4 inCol;

layout(push_constant) uniform PC {
    mat4 viewProj;
    vec4 sunDir;
} pc;

layout(location = 0) out vec3 vNrm;
layout(location = 1) out vec4 vCol;

void main() {
    vNrm = inNrm;
    vCol = inCol;
    gl_Position = pc.viewProj * vec4(inPos, 1.0);
}
