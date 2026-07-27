#version 450
// 2D overlay stage (V159): screen-space quads — text glyphs and HUD
// panels. Position in pixels, ortho via push constant screen size.
layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inCol;

layout(push_constant) uniform PC {
    vec4 screen;   // xy = size in pixels
} pc;

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec4 vCol;

void main() {
    vUV = inUV;
    vCol = inCol;
    vec2 ndc = inPos / pc.screen.xy * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
