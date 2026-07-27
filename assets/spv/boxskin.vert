#version 450
// Skinned lit vertex stage (V180): boxlit.vert plus an object-space UV so
// armour reads as TEXTURE, not geometry. Triplanar-lite: the dominant
// normal axis picks which two object axes wrap the pattern; the vertical
// body axis always runs along v so armour bands stay horizontal.
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNrm;
layout(location = 2) in vec4 i0;
layout(location = 3) in vec4 i1;
layout(location = 4) in vec4 i2;
layout(location = 5) in vec4 i3;
layout(location = 6) in vec4 iCol;
layout(push_constant) uniform PC {
    mat4 viewProj;
    mat4 lightVP;
    vec4 sunShadow;   // xyz = sun direction, w = shadows on/off
    vec4 skinRect;    // atlas sub-rect: xy origin, zw size
} pc;
layout(location = 0) out vec3 vNrm;
layout(location = 1) out vec4 vCol;
layout(location = 2) out vec3 vWorld;
layout(location = 3) out vec2 vUV;
void main() {
    mat4 M = mat4(i0, i1, i2, i3);
    vec4 world = M * vec4(inPos, 1.0);
    vNrm = mat3(M) * inNrm;
    vCol = iCol;
    vWorld = world.xyz;
    vec3 an = abs(inNrm);
    vec2 uv;
    if (an.y >= an.x && an.y >= an.z)
        uv = inPos.xz + 0.5;
    else if (an.x >= an.z)
        uv = vec2(inPos.z + 0.5, 0.5 - inPos.y);
    else
        uv = vec2(inPos.x + 0.5, 0.5 - inPos.y);
    vUV = pc.skinRect.xy + uv * pc.skinRect.zw;
    gl_Position = pc.viewProj * world;
}
