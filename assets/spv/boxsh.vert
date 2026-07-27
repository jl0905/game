#version 450
// Shadow-pass vertex stage for instanced boxes (V178): depth only, from
// the sun ortho matrix. No fragment stage - the render pass has no colour.
layout(location = 0) in vec3 inPos;
layout(location = 2) in vec4 i0;
layout(location = 3) in vec4 i1;
layout(location = 4) in vec4 i2;
layout(location = 5) in vec4 i3;
layout(push_constant) uniform PC { mat4 lightVP; } pc;
void main() {
    mat4 M = mat4(i0, i1, i2, i3);
    gl_Position = pc.lightVP * (M * vec4(inPos, 1.0));
}
