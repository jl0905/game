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
