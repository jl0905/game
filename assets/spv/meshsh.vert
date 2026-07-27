#version 450
// Shadow-pass vertex stage for the static terrain mesh (V178).
layout(location = 0) in vec3 inPos;
layout(push_constant) uniform PC { mat4 lightVP; } pc;
void main() { gl_Position = pc.lightVP * vec4(inPos, 1.0); }
