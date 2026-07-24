#include "sfx.h"
#include "raylib.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

// ---------------------------------------------------------------------------
// Procedural audio, redesigned (V145, user direction): muted, semi-realistic,
// with a faint bitcrushed grain — and never harsh. Every buffer passes one
// mastering chain at synth time (zero runtime cost):
//   1. one-pole low-pass         -> the muted, non-digital body
//   2. tanh soft clip            -> edges round off; nothing ever hard-clips
//   3. 7-bit / half-rate crush   -> mixed in at 25% for the requested grain
//   4. peak normalise to 0.6     -> uniform headroom across all voices
//   5. 2 ms edge fades           -> no boundary clicks
// Runtime keeps layering in check: per-effect cooldowns plus a global voice
// budget that first DUCKS extra hits in a busy melee, then drops them.
// ---------------------------------------------------------------------------

namespace {

constexpr int   RATE   = 22050;
constexpr int   NSFX   = 9;
Sound  g_sounds[NSFX] = {};
double g_lastPlay[NSFX] = {};
Sound  g_wind = {};
Sound  g_drums = {};
Sound  g_rain = {};
Sound  g_lute = {};       // tavern minstrel loop (N5)
Sound  g_music = {};      // campaign drone bed (N5)
Sound  g_thudVar[2] = {}; // extra impact voices — variety in the melee (N5)
int    g_thudNext = 0;
bool   g_ready = false;

// Per-effect minimum seconds between plays (V145): melee spam is the main
// source of self-layering; impacts repeat no faster than the ear resolves.
constexpr double MIN_GAP[NSFX] = {
    /*Thud*/ 0.07, /*Clang*/ 0.08, /*Loose*/ 0.06, /*Swing*/ 0.09,
    /*Gallop*/ 0.25, /*Click*/ 0.08, /*Fanfare*/ 0.80, /*Knell*/ 0.50,
    /*WarCry*/ 0.60,
};

// Global voice budget (V145): a ring of recent play timestamps. Past
// DUCK_AT voices in the last 120 ms, new hits play quieter; past DROP_AT
// they are skipped outright — a hundred simultaneous swings reads as a
// battle, not white noise.
constexpr int    VOICE_RING = 24;
constexpr double VOICE_WINDOW = 0.12;
constexpr int    DUCK_AT = 6, DROP_AT = 14;
double g_voiceTimes[VOICE_RING] = {};
int    g_voiceNext = 0;

int RecentVoices(double now) {
    int c = 0;
    for (double t : g_voiceTimes)
        if (now - t < VOICE_WINDOW) c++;
    return c;
}

float Noise() { return (float)rand() / RAND_MAX * 2.0f - 1.0f; }

// Build a 16-bit mono wave from a generator f(t seconds), then run the
// mastering chain described above. `cutoff` is the low-pass corner in Hz —
// lower = more muted; most effects sit far below the old bright synths.
template <typename F>
Sound Synth(float seconds, float cutoff, F f) {
    const int n = (int)(seconds * RATE);
    float* buf = (float*)RL_MALLOC((size_t)n * sizeof(float));

    // 1) raw generator + one-pole low-pass (the muted body).
    const float k = 1.0f - expf(-2.0f * PI * cutoff / RATE);
    float lp = 0.0f;
    for (int i = 0; i < n; ++i) {
        const float t = (float)i / RATE;
        lp += (f(t) - lp) * k;
        buf[i] = lp;
    }

    // 2) soft clip — gentle tanh saturation instead of digital edges.
    for (int i = 0; i < n; ++i)
        buf[i] = tanhf(buf[i] * 1.4f) * 0.83f;

    // 3) bitcrush grain, mixed low: 7-bit depth at half the sample rate.
    float held = 0.0f;
    for (int i = 0; i < n; ++i) {
        if ((i & 1) == 0)
            held = roundf(buf[i] * 64.0f) / 64.0f;   // 7-bit hold
        buf[i] = buf[i] * 0.75f + held * 0.25f;
    }

    // 4) peak normalise to a fixed headroom so mixes can't clip.
    float peak = 1e-6f;
    for (int i = 0; i < n; ++i) peak = fmaxf(peak, fabsf(buf[i]));
    const float norm = 0.60f / peak;

    // 5) 2 ms fades kill boundary clicks (a big part of the "digital" feel).
    const int fade = RATE / 500;
    short* samples = (short*)RL_MALLOC((size_t)n * sizeof(short));
    for (int i = 0; i < n; ++i) {
        float v = buf[i] * norm;
        if (i < fade)          v *= (float)i / fade;
        else if (i >= n - fade) v *= (float)(n - 1 - i) / fade;
        samples[i] = (short)(v * 32000.0f);
    }
    RL_FREE(buf);

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

}  // namespace

void SfxInit() {
    if (g_ready) return;
    InitAudioDevice();
    if (!IsAudioDeviceReady()) return;
    srand(1234);   // deterministic waves
    SetMasterVolume(0.85f);   // headroom under the mix (V145)

    // Thud: a dull body blow — low noise knock over a 70 Hz bump. No sine
    // sweep whistle any more; it reads as flesh and padding, not a synth.
    g_sounds[(int)Sfx::Thud] = Synth(0.16f, 420.0f, [](float t) {
        const float env = expf(-t * 30.0f);
        return (Noise() * 0.85f + sinf(2 * PI * 70.0f * t) * 0.5f) * env;
    });
    // Clang: dull struck steel — slightly beating partials under a heavy
    // low-pass, with a noise scrape only in the first 10 ms.
    g_sounds[(int)Sfx::Clang] = Synth(0.34f, 2100.0f, [](float t) {
        const float env = expf(-t * 12.0f);
        const float scrape = t < 0.010f ? Noise() * 0.8f : 0.0f;
        return (sinf(2 * PI * 861.0f * t) * 0.50f +
                sinf(2 * PI * 1293.0f * t) * 0.28f +
                sinf(2 * PI * 1307.0f * t) * 0.22f +   // beats against 1293
                scrape) * env;
    });
    // Loose: breath and string — a noise whip, no beep.
    g_sounds[(int)Sfx::Loose] = Synth(0.11f, 1600.0f, [](float t) {
        const float env = expf(-t * 48.0f);
        return Noise() * env;
    });
    // Swing: air over a blade — noise under a swelling envelope; the synth
    // chain's low-pass rounds it into a real whoosh.
    g_sounds[(int)Sfx::Swing] = Synth(0.24f, 900.0f, [](float t) {
        const float env = sinf(PI * t / 0.24f);
        return Noise() * env * env;
    });
    // Gallop: three soft, slightly uneven ground strikes.
    g_sounds[(int)Sfx::Gallop] = Synth(0.32f, 300.0f, [](float t) {
        float v = 0;
        int   i = 0;
        for (const float st : { 0.0f, 0.12f, 0.21f }) {
            const float lt = t - st;
            const float g  = 1.0f - 0.15f * i++;
            if (lt > 0)
                v += (Noise() * 0.6f + sinf(2 * PI * 64.0f * lt) * 0.7f) *
                     expf(-lt * 42.0f) * g;
        }
        return v;
    });
    // Click: a dry wooden tick — a 5 ms noise grain, nothing tonal.
    g_sounds[(int)Sfx::Click] = Synth(0.04f, 1400.0f, [](float t) {
        return t < 0.006f ? Noise() : Noise() * expf(-t * 120.0f) * 0.4f;
    });
    // Fanfare: the same three rising calls, but on twin detuned voices with
    // a slow attack — a horn across a field, not a chiptune.
    g_sounds[(int)Sfx::Fanfare] = Synth(0.95f, 1500.0f, [](float t) {
        const float freqs[3] = { 392.0f, 494.0f, 587.0f };
        const int   note = t < 0.27f ? 0 : (t < 0.54f ? 1 : 2);
        const float lt = t - note * 0.27f;
        const float f = freqs[note];
        const float env = fminf(lt * 9.0f, 1.0f) * expf(-lt * (note == 2 ? 2.6f : 5.0f));
        return (sinf(2 * PI * f * t) * 0.5f +
                sinf(2 * PI * (f * 1.005f) * t) * 0.35f +   // detune = breath
                sinf(2 * PI * f * 2.0f * t) * 0.12f) * env;
    });
    // Knell: a low mourning bell with slow beats, longer and duller.
    g_sounds[(int)Sfx::Knell] = Synth(1.5f, 900.0f, [](float t) {
        const float env = expf(-t * 2.0f);
        return (sinf(2 * PI * 196.0f * t) * 0.6f +
                sinf(2 * PI * 197.6f * t) * 0.3f +    // beating pair
                sinf(2 * PI * 293.0f * t) * 0.2f) * env;
    });
    // War cry: a massed roar — throat rumble with a shallow waver.
    g_sounds[(int)Sfx::WarCry] = Synth(1.3f, 700.0f, [](float t) {
        const float env = fminf(t * 8.0f, 1.0f) * expf(-t * 2.2f);
        static float lp = 0;
        lp += (Noise() - lp) * 0.12f;
        const float voice = sinf(2 * PI * (95.0f + 9.0f * sinf(2 * PI * 5.0f * t)) * t);
        return lp * 1.0f + voice * 0.35f * env;
    });
    // Wind bed: slow-breathing low noise.
    g_wind = Synth(2.0f, 500.0f, [](float t) {
        const float breath = 0.6f + 0.4f * sinf(2 * PI * 0.5f * t);
        return Noise() * breath;
    });

    // Rain bed: dense patter, softened — presence, not hiss.
    g_rain = Synth(2.0f, 2400.0f, [](float t) {
        const float flutter = 0.85f + 0.15f * sinf(2 * PI * 3.3f * t);
        return Noise() * flutter * 0.8f;
    });

    // Impact variety (N5): two sibling thuds, same dull family.
    g_thudVar[0] = Synth(0.14f, 380.0f, [](float t) {
        const float env = expf(-t * 34.0f);
        return (Noise() * 0.9f + sinf(2 * PI * 62.0f * t) * 0.45f) * env;
    });
    g_thudVar[1] = Synth(0.19f, 480.0f, [](float t) {
        const float env = expf(-t * 24.0f);
        return (Noise() * 0.75f + sinf(2 * PI * 82.0f * t) * 0.55f) * env;
    });

    // Minstrel (N5): the same A-minor figure, warmer — detuned strings and
    // a felt-pick attack instead of a glassy pluck.
    g_lute = Synth(4.0f, 1700.0f, [](float t) {
        constexpr float NOTES[8] = { 220.0f, 261.6f, 329.6f, 261.6f,
                                     293.7f, 329.6f, 261.6f, 246.9f };
        const int   step = (int)(t * 2.0f) % 8;
        const float lt   = t - (float)((int)(t * 2.0f)) * 0.5f;
        const float f    = NOTES[step];
        const float env  = expf(-lt * 5.0f);
        return (sinf(2 * PI * f * lt) * 0.5f +
                sinf(2 * PI * (f * 1.004f) * lt) * 0.3f +
                sinf(2 * PI * f * 2.0f * lt) * 0.12f) * env;
    });

    // Campaign bed (N5): the low fifth, breathing slower and rounder.
    g_music = Synth(8.0f, 400.0f, [](float t) {
        const float breath = 0.55f + 0.45f * sinf(2 * PI * t / 8.0f);
        return (sinf(2 * PI * 98.0f * t) * 0.5f +
                sinf(2 * PI * 98.4f * t) * 0.2f +     // slow chorus beat
                sinf(2 * PI * 146.8f * t) * 0.3f) * breath;
    });

    // War drums (V114): deep skins, felt more than heard.
    g_drums = Synth(2.4f, 260.0f, [](float t) {
        float v = 0;
        for (const float st : { 0.0f, 0.9f, 1.2f, 1.8f }) {
            const float lt = t - st;
            if (lt > 0 && lt < 0.5f)
                v += (sinf(2 * PI * (58.0f - 40.0f * lt) * lt) +
                      Noise() * 0.25f * expf(-lt * 30.0f)) * expf(-lt * 9.0f);
        }
        return v;
    });

    g_ready = true;
}

void SfxShutdown() {
    if (!g_ready) return;
    for (Sound& s : g_sounds) UnloadSound(s);
    UnloadSound(g_wind);
    UnloadSound(g_drums);
    UnloadSound(g_rain);
    UnloadSound(g_lute);
    UnloadSound(g_music);
    for (Sound& s : g_thudVar) UnloadSound(s);
    CloseAudioDevice();
    g_ready = false;
}

void SfxAmbience(float volume) {
    if (!g_ready) return;
    SetSoundVolume(g_wind, volume);
    if (volume > 0.01f && !IsSoundPlaying(g_wind)) PlaySound(g_wind);
}

void SfxRain(float volume) {
    if (!g_ready) return;
    SetSoundVolume(g_rain, volume * 0.8f);
    if (volume > 0.01f && !IsSoundPlaying(g_rain)) PlaySound(g_rain);
}

void SfxDrums(float volume) {   // the war drums (V114)
    if (!g_ready) return;
    SetSoundVolume(g_drums, volume);
    if (volume > 0.01f && !IsSoundPlaying(g_drums)) PlaySound(g_drums);
}

void SfxMinstrel(float volume) {
    if (!g_ready) return;
    SetSoundVolume(g_lute, volume);
    if (volume > 0.01f && !IsSoundPlaying(g_lute)) PlaySound(g_lute);
}

void SfxMusic(float volume) {
    if (!g_ready) return;
    SetSoundVolume(g_music, volume);
    if (volume > 0.01f && !IsSoundPlaying(g_music)) PlaySound(g_music);
}

void SfxPlay(Sfx s, float volume) {
    if (!g_ready) return;
    const int i = (int)s;
    const double now = GetTime();
    if (now - g_lastPlay[i] < MIN_GAP[i]) return;   // per-effect cooldown (V145)

    // The voice budget (V145): a crowded 120 ms first ducks, then drops.
    const int recent = RecentVoices(now);
    if (recent >= DROP_AT) return;
    if (recent >= DUCK_AT)
        volume *= (float)DUCK_AT / (float)(recent + 1);

    g_lastPlay[i] = now;
    g_voiceTimes[g_voiceNext++ % VOICE_RING] = now;

    // Impact variety (N5): thuds rotate through sibling voices.
    if (s == Sfx::Thud) {
        Sound& v = (g_thudNext++ % 3 == 0) ? g_sounds[i]
                                           : g_thudVar[g_thudNext % 2];
        SetSoundVolume(v, volume);
        PlaySound(v);
        return;
    }
    SetSoundVolume(g_sounds[i], volume);
    PlaySound(g_sounds[i]);
}
