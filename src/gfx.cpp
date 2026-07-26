#include "gfx.h"

namespace {

const char* LIT_VS = R"(
#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;
uniform mat4 mvp;
uniform mat4 matNormal;
out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragNormal;
void main() {
    fragTexCoord = vertexTexCoord;
    fragColor    = vertexColor;
    fragNormal   = normalize(vec3(matNormal * vec4(vertexNormal, 0.0)));
    gl_Position  = mvp * vec4(vertexPosition, 1.0);
}
)";

const char* LIT_FS = R"(
#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragNormal;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
out vec4 finalColor;
void main() {
    vec4 texel = texture(texture0, fragTexCoord);
    vec3 sun   = normalize(vec3(-0.45, 0.85, -0.30));   // matches the sky's sun
    float ndl  = max(dot(normalize(fragNormal), sun), 0.0);
    float light = 0.62 + 0.38 * ndl;                    // ambient floor keeps it safe
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

RenderTexture2D g_postTarget = { 0 };
Shader          g_postShader = { 0 };
int             g_postTimeLoc = -1;
bool            g_postLoaded = false, g_postActive = false;

}  // namespace

void PostBegin() {
    if (!IsWindowReady() || !GetSettings().postFx) { g_postActive = false; return; }
    if (!g_postLoaded) {
        g_postShader  = LoadShaderFromMemory(nullptr, POST_FS);
        g_postTimeLoc = GetShaderLocation(g_postShader, "uTime");
        g_postLoaded  = true;
    }
    if (g_postShader.id == 0) { g_postActive = false; return; }
    const int w = GetScreenWidth(), h = GetScreenHeight();
    if (g_postTarget.texture.width != w || g_postTarget.texture.height != h) {
        if (g_postTarget.id) UnloadRenderTexture(g_postTarget);
        g_postTarget = LoadRenderTexture(w, h);   // follows window resizes
    }
    BeginTextureMode(g_postTarget);
    g_postActive = true;
}

void PostEnd() {
    if (!g_postActive) return;
    EndTextureMode();
    const float t = (float)GetTime();
    SetShaderValue(g_postShader, g_postTimeLoc, &t, SHADER_UNIFORM_FLOAT);
    BeginShaderMode(g_postShader);
    DrawTextureRec(g_postTarget.texture,
                   { 0, 0, (float)g_postTarget.texture.width,
                     (float)-g_postTarget.texture.height },
                   { 0, 0 }, WHITE);
    EndShaderMode();
    g_postActive = false;
}
