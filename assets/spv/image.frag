#version 450
// Textured 2D quad stage (V177): full-colour texture modulated by the
// vertex colour - the map blit and icon path of the Vulkan UI layer.
layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vCol;
layout(set = 0, binding = 0) uniform sampler2D img;
layout(location = 0) out vec4 outCol;
void main() {
    outCol = vCol * texture(img, vUV);
}
