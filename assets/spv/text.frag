#version 450
// Glyph/panel fragment (V159): atlas alpha modulates the vertex colour;
// UV (-1,-1) means "solid panel, no texture".
layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vCol;

layout(set = 0, binding = 0) uniform sampler2D atlas;

layout(location = 0) out vec4 outCol;

void main() {
    if (vUV.x < 0.0) {
        outCol = vCol;               // solid HUD panel
    } else {
        float a = texture(atlas, vUV).r;
        outCol = vec4(vCol.rgb, vCol.a * a);
    }
}
