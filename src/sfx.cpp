#include "sfx.h"
#include "raylib.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

namespace {

constexpr int   RATE   = 22050;
constexpr int   NSFX   = 5;
Sound  g_sounds[NSFX] = {};
double g_lastPlay[NSFX] = {};
bool   g_ready = false;

// Build a 16-bit mono wave from a generator f(t seconds) in [-1, 1].
template <typename F>
Sound Synth(float seconds, F f) {
    const int n = (int)(seconds * RATE);
    short* samples = (short*)RL_MALLOC((size_t)n * sizeof(short));
    for (int i = 0; i < n; ++i) {
        const float t = (float)i / RATE;
        float v = f(t);
        if (v > 1.0f) v = 1.0f;
        if (v < -1.0f) v = -1.0f;
        samples[i] = (short)(v * 32000.0f);
    }
    Wave w{};
    w.frameCount = (unsigned)n;
    w.sampleRate = RATE;
    w.sampleSize = 16;
    w.channels = 1;
    w.data = samples;
    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);   // sound keeps its own copy
    return s;
}

float Noise() { return (float)rand() / RAND_MAX * 2.0f - 1.0f; }

}  // namespace

void SfxInit() {
    if (g_ready) return;
    InitAudioDevice();
    if (!IsAudioDeviceReady()) return;
    srand(1234);   // deterministic waves

    // Thud: a low sine sweep with a fast decay and some body noise.
    g_sounds[(int)Sfx::Thud] = Synth(0.18f, [](float t) {
        const float env = expf(-t * 26.0f);
        return (sinf(2 * PI * (110.0f - 260.0f * t) * t) + Noise() * 0.35f) * env * 0.8f;
    });
    // Clang: inharmonic metallic partials ringing down.
    g_sounds[(int)Sfx::Clang] = Synth(0.30f, [](float t) {
        const float env = expf(-t * 14.0f);
        return (sinf(2 * PI * 1180.0f * t) * 0.5f + sinf(2 * PI * 1783.0f * t) * 0.3f +
                sinf(2 * PI * 2510.0f * t) * 0.2f + Noise() * 0.1f) * env * 0.6f;
    });
    // Loose: a short breathy snap (bowstring + shaft).
    g_sounds[(int)Sfx::Loose] = Synth(0.12f, [](float t) {
        const float env = expf(-t * 45.0f);
        return (Noise() * 0.7f + sinf(2 * PI * 400.0f * t) * 0.3f) * env * 0.55f;
    });
    // Swing: filtered-ish whoosh (noise with a moving emphasis).
    g_sounds[(int)Sfx::Swing] = Synth(0.22f, [](float t) {
        const float env = sinf(PI * t / 0.22f);            // swell and fade
        return Noise() * env * env * 0.35f;
    });
    // Gallop: three soft ground strikes.
    g_sounds[(int)Sfx::Gallop] = Synth(0.30f, [](float t) {
        float v = 0;
        for (const float st : { 0.0f, 0.11f, 0.20f }) {
            const float lt = t - st;
            if (lt > 0) v += (sinf(2 * PI * 70.0f * lt) + Noise() * 0.2f) * expf(-lt * 40.0f);
        }
        return v * 0.5f;
    });
    g_ready = true;
}

void SfxShutdown() {
    if (!g_ready) return;
    for (Sound& s : g_sounds) UnloadSound(s);
    CloseAudioDevice();
    g_ready = false;
}

void SfxPlay(Sfx s, float volume) {
    if (!g_ready) return;
    const int i = (int)s;
    const double now = GetTime();
    if (now - g_lastPlay[i] < 0.05) return;   // rate limit per effect
    g_lastPlay[i] = now;
    SetSoundVolume(g_sounds[i], volume);
    PlaySound(g_sounds[i]);
}
