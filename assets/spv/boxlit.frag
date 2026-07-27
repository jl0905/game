#version 450
// Shadowed Lambert fragment (V178): the same 3x3 PCF and shadowed-sun mix
// the GL road uses (gfx.cpp LIT_FS / the V153 instancing shader), so the
// Vulkan frame matches the GL one with shadows on. sunShadow.w gates the
// lookup so `shadows off` reproduces the V157 box.frag shading exactly.
layout(location = 0) in vec3 vNrm;
layout(location = 1) in vec4 vCol;
layout(location = 2) in vec3 vWorld;
layout(push_constant) uniform PC {
    mat4 viewProj;
    mat4 lightVP;
    vec4 sunShadow;
} pc;
layout(set = 0, binding = 0) uniform sampler2D shadowMap;
layout(location = 0) out vec4 outCol;

float shadowAt(float ndl) {
    if (pc.sunShadow.w < 0.5) return 1.0;
    vec4 ls = pc.lightVP * vec4(vWorld, 1.0);
    vec3 p = ls.xyz / ls.w;
    // Negative-viewport shadow pass keeps GL clip conventions; Vulkan
    // texture v runs top-down, hence the flipped v here.
    vec2 uv = vec2(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5);
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || p.z > 1.0)
        return 1.0;
    float bias = max(0.0022 * (1.0 - ndl), 0.0008);
    float lit = 0.0;
    vec2 tx = 1.0 / vec2(textureSize(shadowMap, 0));
    for (int dx = -1; dx <= 1; dx++)
        for (int dy = -1; dy <= 1; dy++) {
            float d = texture(shadowMap, uv + vec2(dx, dy) * tx).r;
            lit += (p.z - bias <= d) ? 1.0 : 0.0;
        }
    return lit / 9.0;
}

void main() {
    float ndl = max(dot(normalize(vNrm), -pc.sunShadow.xyz), 0.0);
    float sh = shadowAt(ndl);
    float shade = 0.55 + 0.45 * ndl * mix(0.35, 1.0, sh);
    outCol = vec4(vCol.rgb * shade, vCol.a);
}
