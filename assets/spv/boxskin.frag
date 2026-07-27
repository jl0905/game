#version 450
// Skinned shadowed Lambert (V180): boxlit.frag with the armour atlas
// modulating the lit colour. The atlas is grayscale, so team and armour
// tints keep reading through the weave/mail/plate pattern.
layout(location = 0) in vec3 vNrm;
layout(location = 1) in vec4 vCol;
layout(location = 2) in vec3 vWorld;
layout(location = 3) in vec2 vUV;
layout(push_constant) uniform PC {
    mat4 viewProj;
    mat4 lightVP;
    vec4 sunShadow;
    vec4 skinRect;
} pc;
layout(set = 0, binding = 0) uniform sampler2D shadowMap;
layout(set = 0, binding = 1) uniform sampler2D skinAtlas;
layout(location = 0) out vec4 outCol;

float shadowAt(float ndl) {
    if (pc.sunShadow.w < 0.5) return 1.0;
    vec4 ls = pc.lightVP * vec4(vWorld, 1.0);
    vec3 p = ls.xyz / ls.w;
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
    vec3 skin = texture(skinAtlas, vUV).rgb;
    outCol = vec4(vCol.rgb * shade * skin, vCol.a);
}
