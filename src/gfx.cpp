#include "gfx.h"

namespace {

const char* LIT_VS = R"(
#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragNormal;
out vec3 fragWorld;
void main() {
    fragTexCoord = vertexTexCoord;
    fragColor    = vertexColor;
    fragNormal   = normalize(vec3(matNormal * vec4(vertexNormal, 0.0)));
    fragWorld    = vec3(matModel * vec4(vertexPosition, 1.0));
    gl_Position  = mvp * vec4(vertexPosition, 1.0);
}
)";

const char* LIT_FS = R"(
#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragNormal;
in vec3 fragWorld;
uniform sampler2D texture0;
uniform sampler2D shadowMap;
uniform mat4 lightVP;
uniform int shadowsOn;
uniform vec4 colDiffuse;
out vec4 finalColor;

float shadowAt(vec3 world, float ndl) {
    if (shadowsOn == 0) return 1.0;
    vec4 ls = lightVP * vec4(world, 1.0);
    vec3 p = ls.xyz / ls.w * 0.5 + 0.5;
    if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0 || p.z > 1.0)
        return 1.0;
    float bias = max(0.0022 * (1.0 - ndl), 0.0008);
    float lit = 0.0;
    vec2 tx = 1.0 / vec2(textureSize(shadowMap, 0));
    for (int dx = -1; dx <= 1; dx++)          // 3x3 PCF: soft edges
        for (int dy = -1; dy <= 1; dy++) {
            float d = texture(shadowMap, p.xy + vec2(dx, dy) * tx).r;
            lit += (p.z - bias <= d) ? 1.0 : 0.0;
        }
    return lit / 9.0;
}

void main() {
    vec4 texel = texture(texture0, fragTexCoord);
    vec3 sun   = normalize(vec3(-0.45, 0.85, -0.30));   // matches the sky's sun
    float ndl  = max(dot(normalize(fragNormal), sun), 0.0);
    float sh   = shadowAt(fragWorld, ndl);
    float light = 0.52 + 0.48 * ndl * mix(0.35, 1.0, sh);   // shadowed sun (V153)
    finalColor = vec4(texel.rgb * fragColor.rgb * colDiffuse.rgb * light,
                      texel.a * fragColor.a * colDiffuse.a);
}
)";

}  // namespace

Shader GetLitShader() {
    static Shader shader = { 0 };
    static bool loaded = false;
    if (!loaded && IsWindowReady()) {
        shader = LoadShaderFromMemory(LIT_VS, LIT_FS);
        loaded = true;
    }
    return shader;
}

// ---------------------------------------------------------------------------
// The filmic post pass (V151): one fullscreen shader between the 3D world
// and the HUD. ACES tone mapping tames the flat raylib colours into a
// graded, cinematic image; a soft vignette pulls the eye centre-frame; a
// whisper of animated grain kills banding. All in one pass — the cost is a
// single fullscreen quad.
// ---------------------------------------------------------------------------
#include "settings.h"

namespace {

const char* POST_FS = R"(
#version 330
in vec2 fragTexCoord;
uniform sampler2D texture0;
uniform sampler2D bloomTex;
uniform float uTime;
out vec4 finalColor;

vec3 aces(vec3 c) {
    return clamp((c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14),
                 0.0, 1.0);
}
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}
void main() {
    vec2 uv = fragTexCoord;
    vec3 c = texture(texture0, uv).rgb;
    c += texture(bloomTex, uv).rgb * 0.55;   // the glow (V154)
    // filmic tone map with a touch of pre-exposure
    c = aces(c * 1.15);
    // grade: cool the shadows toward slate, warm the highlights toward gold
    float lum = dot(c, vec3(0.299, 0.587, 0.114));
    c = mix(c * vec3(0.94, 0.98, 1.06), c * vec3(1.06, 1.01, 0.92),
            smoothstep(0.25, 0.75, lum));
    // gentle saturation lift
    c = mix(vec3(lum), c, 1.12);
    // vignette
    float v = smoothstep(0.95, 0.35, length(uv - 0.5));
    c *= 0.82 + 0.18 * v;
    // living grain, subtle
    c += (hash(uv * vec2(1920.0, 1080.0) + fract(uTime) * 7.0) - 0.5) * 0.015;
    finalColor = vec4(c, 1.0);
}
)";

// Bloom (V154): a half-res bright-pass + tent blur, composited by the
// final post shader — sun glints, gilt roofs and the sky itself glow.
const char* BLOOM_FS = R"(
#version 330
in vec2 fragTexCoord;
uniform sampler2D texture0;
out vec4 finalColor;
void main() {
    vec2 tx = 1.5 / vec2(textureSize(texture0, 0));
    vec3 acc = vec3(0.0);
    float wsum = 0.0;
    for (int dx = -2; dx <= 2; dx++)
        for (int dy = -2; dy <= 2; dy++) {
            float w = 1.0 / (1.0 + float(dx * dx + dy * dy));
            vec3 c = texture(texture0, fragTexCoord + vec2(dx, dy) * tx).rgb;
            float lum = dot(c, vec3(0.299, 0.587, 0.114));
            acc += max(lum - 0.72, 0.0) * c * w;   // bright pass
            wsum += w;
        }
    finalColor = vec4(acc / wsum * 2.2, 1.0);
}
)";

RenderTexture2D g_postTarget = { 0 };
RenderTexture2D g_bloomRT   = { 0 };
Shader          g_postShader = { 0 };
Shader          g_bloomShader = { 0 };
int             g_postTimeLoc = -1, g_postBloomLoc = -1;
bool            g_postLoaded = false, g_postActive = false;

}  // namespace

void PostBegin() {
    if (!IsWindowReady() || !GetSettings().postFx) { g_postActive = false; return; }
    if (!g_postLoaded) {
        g_postShader   = LoadShaderFromMemory(nullptr, POST_FS);
        g_postTimeLoc  = GetShaderLocation(g_postShader, "uTime");
        g_postBloomLoc = GetShaderLocation(g_postShader, "bloomTex");
        g_bloomShader  = LoadShaderFromMemory(nullptr, BLOOM_FS);
        g_postLoaded   = true;
    }
    if (g_postShader.id == 0) { g_postActive = false; return; }
    const int w = GetScreenWidth(), h = GetScreenHeight();
    if (g_postTarget.texture.width != w || g_postTarget.texture.height != h) {
        if (g_postTarget.id) UnloadRenderTexture(g_postTarget);
        g_postTarget = LoadRenderTexture(w, h);   // follows window resizes
        if (g_bloomRT.id) UnloadRenderTexture(g_bloomRT);
        g_bloomRT = LoadRenderTexture(w / 2, h / 2);   // half-res glow (V154)
    }
    BeginTextureMode(g_postTarget);
    g_postActive = true;
}

void PostEnd() {
    if (!g_postActive) return;
    EndTextureMode();

    // Bloom pass (V154): bright-extract + blur the frame at half res.
    if (g_bloomShader.id != 0 && g_bloomRT.id != 0) {
        BeginTextureMode(g_bloomRT);
        BeginShaderMode(g_bloomShader);
        DrawTexturePro(g_postTarget.texture,
                       { 0, 0, (float)g_postTarget.texture.width,
                         (float)-g_postTarget.texture.height },
                       { 0, 0, (float)g_bloomRT.texture.width,
                         (float)g_bloomRT.texture.height },
                       { 0, 0 }, 0.0f, WHITE);
        EndShaderMode();
        EndTextureMode();
    }

    const float t = (float)GetTime();
    SetShaderValue(g_postShader, g_postTimeLoc, &t, SHADER_UNIFORM_FLOAT);
    BeginShaderMode(g_postShader);
    SetShaderValueTexture(g_postShader, g_postBloomLoc, g_bloomRT.texture);
    DrawTextureRec(g_postTarget.texture,
                   { 0, 0, (float)g_postTarget.texture.width,
                     (float)-g_postTarget.texture.height },
                   { 0, 0 }, WHITE);
    EndShaderMode();
    g_postActive = false;
}


// ---------------------------------------------------------------------------
// The shadow pass (V153): a depth-only render from the sun. rlgl gives us a
// depth-texture framebuffer raylib's LoadRenderTexture doesn't expose.
// ---------------------------------------------------------------------------
#include "rlgl.h"
#include "raymath.h"

namespace {

constexpr int SHADOW_RES = 2048;
RenderTexture2D g_shadowRT = { 0 };
bool g_shadowLoaded = false, g_shadowActive = false;

RenderTexture2D LoadShadowTarget() {
    RenderTexture2D rt = { 0 };
    rt.id = rlLoadFramebuffer();
    rlEnableFramebuffer(rt.id);
    rt.depth.id = rlLoadTextureDepth(SHADOW_RES, SHADOW_RES, false);
    rt.depth.width = rt.depth.height = SHADOW_RES;
    rt.depth.format = 19;   // DEPTH component
    rt.depth.mipmaps = 1;
    rlFramebufferAttach(rt.id, rt.depth.id, RL_ATTACHMENT_DEPTH,
                        RL_ATTACHMENT_TEXTURE2D, 0);
    rlDisableFramebuffer();
    return rt;
}

}  // namespace

bool ShadowsOn() { return IsWindowReady() && GetSettings().shadows; }

Matrix ShadowBegin(Vector3 sunDir, Vector3 center) {
    if (!ShadowsOn()) return MatrixIdentity();
    if (!g_shadowLoaded) { g_shadowRT = LoadShadowTarget(); g_shadowLoaded = true; }
    if (g_shadowRT.id == 0) return MatrixIdentity();

    const float S = 110.0f;   // ortho half-size covers the battlefield
    const Vector3 eye = Vector3Add(center, Vector3Scale(sunDir, -160.0f));
    const Matrix view = MatrixLookAt(eye, center, { 0.0f, 1.0f, 0.0f });
    const Matrix proj = MatrixOrtho(-S, S, -S, S, 5.0, 400.0);

    rlEnableFramebuffer(g_shadowRT.id);
    rlViewport(0, 0, SHADOW_RES, SHADOW_RES);
    rlClearScreenBuffers();
    rlMatrixMode(RL_PROJECTION);
    rlPushMatrix();
    rlLoadIdentity();
    rlMultMatrixf(MatrixToFloat(proj));
    rlMatrixMode(RL_MODELVIEW);
    rlLoadIdentity();
    rlMultMatrixf(MatrixToFloat(view));
    rlEnableDepthTest();
    g_shadowActive = true;
    return MatrixMultiply(view, proj);
}

void ShadowEnd() {
    if (!g_shadowActive) return;
    rlDrawRenderBatchActive();
    rlMatrixMode(RL_PROJECTION);
    rlPopMatrix();
    rlMatrixMode(RL_MODELVIEW);
    rlLoadIdentity();
    rlDisableFramebuffer();
    rlViewport(0, 0, GetScreenWidth(), GetScreenHeight());
    g_shadowActive = false;
}

void ShadowBind(Shader sh, int lightVpLoc, int mapLoc, Matrix lightVP) {
    if (!g_shadowLoaded || g_shadowRT.id == 0) return;
    SetShaderValueMatrix(sh, lightVpLoc, lightVP);
    SetShaderValueTexture(sh, mapLoc, g_shadowRT.depth);
}
