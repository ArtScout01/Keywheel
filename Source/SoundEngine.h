#pragma once
// Klangerzeugung fuer Mode Wheel. Reines C++17 (ohne JUCE), damit separat testbar.
// Die Sounds sind eigene Synthese-Nachbauten des *Charakters* bekannter Klaenge, keine Samples.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace snd
{
constexpr int numSounds = 7;
inline const char* names[numSounds] = { "Saw", "Juno Pad (wobble)", "Tape Flute", "Tape Strings",
                                        "Airy Vocal Pad", "Fuzz Mono Synth", "Juno Brass" };

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;

struct Adsr
{
    enum Stage { idle, attack, decay, sustain, release };
    Stage stage = idle;
    float a = 0.01f, d = 0.1f, s = 0.7f, r = 0.2f, v = 0.0f, relRate = 0.0f;
    double sr = 44100.0;

    void set (float A, float D, float S, float R, double sampleRate) { a = A; d = D; s = S; r = R; sr = sampleRate; }
    void noteOn() { stage = attack; }
    void noteOff()
    {
        if (stage == idle) return;
        stage = Stage::release;
        relRate = v / std::max (1.0f, r * (float) sr);
    }
    bool active() const { return stage != idle; }
    void kill() { stage = idle; v = 0.0f; }
    float next()
    {
        switch (stage)
        {
            case attack:  v += 1.0f / std::max (1.0f, a * (float) sr); if (v >= 1.0f) { v = 1.0f; stage = decay; } break;
            case decay:   v -= (1.0f - s) / std::max (1.0f, d * (float) sr); if (v <= s) { v = s; stage = sustain; } break;
            case sustain: v = s; break;
            case release: v -= relRate; if (v <= 0.0f) { v = 0.0f; stage = idle; } break;
            case idle:    v = 0.0f; break;
        }
        return v;
    }
};

// Zustandsvariablen-Filter (TPT / Cytomic), stabil auch bei schneller Modulation
struct Svf
{
    float ic1 = 0.0f, ic2 = 0.0f;
    static float coeff (float fc, double sr)
    {
        return std::tan (kPi * std::min (std::max (fc, 20.0f), (float) sr * 0.45f) / (float) sr);
    }
    void process (float x, float g, float k, float& lp, float& bp)
    {
        const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        lp = v2; bp = v1;
    }
};

inline float blep (float t, float dt)
{
    if (t < dt)        { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}
inline float sawBL (float ph, float dt) { return 2.0f * ph - 1.0f - blep (ph, dt); }
inline float sqBL (float ph, float dt)
{
    float p2 = ph + 0.5f; if (p2 >= 1.0f) p2 -= 1.0f;
    return (ph < 0.5f ? 1.0f : -1.0f) + blep (ph, dt) - blep (p2, dt);
}
inline float cents (float c) { return std::exp2 (c / 1200.0f); }
inline void wrap (float& p) { p -= std::floor (p); }

class Engine
{
public:
    void start (int soundIndex, float frequency, float velocity, double sampleRate, uint32_t seed)
    {
        *this = Engine();
        sound = std::min (std::max (soundIndex, 0), numSounds - 1);
        freq = frequency; vel = velocity; sr = sampleRate;
        rng = seed ? seed : 0x9E3779B9u;
        for (float& p : ph) p = rnd01();
        lfoP = rnd01(); lfoF = rnd01(); wowP = rnd01(); fltP = rnd01(); vibP = rnd01(); morphP = rnd01();

        switch (sound)
        {
            case 0:  amp.set (0.01f, 0.25f, 0.7f, 0.35f, sr);  level = 0.15f; ph[0] = 0.0f; break;
            case 1:  amp.set (0.35f, 0.6f, 0.8f, 1.0f, sr);    level = 0.085f; break;
            case 2:  amp.set (0.03f, 0.2f, 0.85f, 0.3f, sr);   level = 0.10f; break;
            case 3:  amp.set (0.18f, 0.3f, 0.85f, 0.45f, sr);  level = 0.10f; break;
            case 4:  amp.set (0.55f, 0.4f, 0.9f, 1.3f, sr);    level = 0.10f; break;
            case 5:  amp.set (0.004f, 0.2f, 0.85f, 0.1f, sr);  fenv.set (0.004f, 0.28f, 0.25f, 0.15f, sr); level = 0.08f; break;
            default: amp.set (0.025f, 0.3f, 0.85f, 0.2f, sr);  fenv.set (0.1f, 0.55f, 0.3f, 0.25f, sr);   level = 0.085f; break;
        }
        level *= vel;
        amp.noteOn(); fenv.noteOn();
    }

    void release() { amp.noteOff(); fenv.noteOff(); }
    void stopNow() { amp.kill(); fenv.kill(); }
    bool active() const { return amp.active(); }

    void next (float& outL, float& outR)
    {
        const float e = amp.next();
        const float fe = fenv.next();
        const float inv = 1.0f / (float) sr;
        float l = 0.0f, r = 0.0f;
        ++t;

        switch (sound)
        {
            case 0: // einfacher Saegezahn (Originalklang)
            {
                const float saw = 2.0f * ph[0] - 1.0f;
                lpOld += 0.2f * (saw - lpOld);
                ph[0] += freq * inv; wrap (ph[0]);
                l = r = lpOld;
                break;
            }
            case 1: // Juno-Pad: 3 verstimmte Saegen + Sub, LFO auf Tonhoehe UND Filter ("unter Wasser")
            {
                lfoP += 0.62f * inv; wrap (lfoP);
                lfoF += 0.37f * inv; wrap (lfoF);
                const float f = freq * cents (28.0f * std::sin (kTwoPi * lfoP));
                const float dts[3] = { f * cents (-9.0f) * inv, f * inv, f * cents (9.0f) * inv };
                for (int i = 0; i < 3; ++i) { ph[i] += dts[i]; wrap (ph[i]); }
                ph[3] += 0.5f * f * inv; wrap (ph[3]);
                const float sub = sqBL (ph[3], 0.5f * f * inv) * 0.35f;
                const float o0 = sawBL (ph[0], dts[0]), o1 = sawBL (ph[1], dts[1]), o2 = sawBL (ph[2], dts[2]);
                const float fc = 1100.0f * std::exp2 (1.6f * std::sin (kTwoPi * lfoF));
                const float g = Svf::coeff (fc, sr), k = 0.9f;
                float lp, bp;
                f_[0][0].process (o0 + 0.7f * o1 + sub, g, k, lp, bp); l = lp;
                f_[1][0].process (o2 + 0.7f * o1 + sub, g, k, lp, bp); r = lp;
                break;
            }
            case 2: // Tape Flute: Sinus + Obertoene, Wow & Flutter, Rauschen, Saettigung, Hoehen weg
            {
                wowP += 0.65f * inv; wrap (wowP);
                lfoP += 6.1f * inv;  wrap (lfoP);
                const float f = freq * cents (9.0f * std::sin (kTwoPi * wowP) + 3.0f * std::sin (kTwoPi * lfoP));
                ph[0] += f * inv; wrap (ph[0]);
                float x = std::sin (kTwoPi * ph[0]) + 0.35f * std::sin (2.0f * kTwoPi * ph[0]) + 0.12f * std::sin (3.0f * kTwoPi * ph[0]);
                x = std::tanh (1.6f * (0.8f * x + 0.05f * noise()));
                float lp, bp;
                f_[0][0].process (x, Svf::coeff (3200.0f, sr), 1.4f, lp, bp);
                l = r = lp;
                break;
            }
            case 3: // Tape Strings: 2 Saegen, Wow & Flutter, dunkel + gesaettigt
            {
                wowP += 0.55f * inv; wrap (wowP);
                lfoP += 5.7f * inv;  wrap (lfoP);
                const float f = freq * cents (8.0f * std::sin (kTwoPi * wowP) + 3.0f * std::sin (kTwoPi * lfoP));
                const float d0 = f * cents (-6.0f) * inv, d1 = f * cents (6.0f) * inv;
                ph[0] += d0; wrap (ph[0]); ph[1] += d1; wrap (ph[1]);
                const float o0 = sawBL (ph[0], d0), o1 = sawBL (ph[1], d1), n = 0.03f * noise();
                float lp, bp;
                const float g = Svf::coeff (2200.0f, sr);
                f_[0][0].process (std::tanh (1.4f * (o0 + 0.5f * o1 + n)), g, 1.2f, lp, bp); l = lp;
                f_[1][0].process (std::tanh (1.4f * (o1 + 0.5f * o0 + n)), g, 1.2f, lp, bp); r = lp;
                break;
            }
            case 4: // Airy Vocal Pad: Saegen durch Formantfilter (ooh -> ahh), Vibrato, Hauchen
            {
                vibP += 5.2f * inv; wrap (vibP);
                morphP += 0.12f * inv; wrap (morphP);
                const float fade = std::min (1.0f, (float) t / (0.8f * (float) sr));
                const float f = freq * cents (6.0f * fade * std::sin (kTwoPi * vibP));
                const float dts[3] = { f * cents (-6.0f) * inv, f * inv, f * cents (6.0f) * inv };
                for (int i = 0; i < 3; ++i) { ph[i] += dts[i]; wrap (ph[i]); }
                const float o0 = sawBL (ph[0], dts[0]), o1 = sawBL (ph[1], dts[1]), o2 = sawBL (ph[2], dts[2]);
                const float m = 0.5f + 0.5f * std::sin (kTwoPi * morphP);
                const float fc[3] = { 420.0f + 400.0f * m, 850.0f + 350.0f * m, 2800.0f };
                const float q[3]  = { 6.0f, 8.0f, 10.0f };
                const float gain[3] = { 1.0f, 0.7f, 0.35f };
                const float breath = 0.10f * noise();
                for (int c = 0; c < 2; ++c)
                {
                    const float in = (c == 0 ? o0 : o2) + 0.7f * o1 + breath;
                    float acc = 0.0f;
                    for (int b = 0; b < 3; ++b)
                    {
                        float lp, bp;
                        const float k = 1.0f / q[b];
                        f_[c][b].process (in, Svf::coeff (fc[b], sr), k, lp, bp);
                        acc += gain[b] * k * bp * 2.2f;     // k*bp = Bandpass mit Verstaerkung 1 im Peak
                    }
                    (c == 0 ? l : r) = acc + 0.01f * noise();
                }
                break;
            }
            case 5: // Fuzz Mono Synth: Saw+Saw+Sub, hart uebersteuert, Filterhuellkurve, nochmal Saettigung
            {
                const float d0 = freq * inv, d1 = freq * cents (7.0f) * inv, ds = 0.5f * freq * inv;
                ph[0] += d0; wrap (ph[0]); ph[1] += d1; wrap (ph[1]); ph[2] += ds; wrap (ph[2]);
                float x = sawBL (ph[0], d0) + 0.7f * sawBL (ph[1], d1) + 0.5f * sqBL (ph[2], ds);
                x = std::tanh (3.5f * x);
                float lp, bp;
                f_[0][0].process (x, Svf::coeff (350.0f + 3200.0f * fe, sr), 1.0f, lp, bp);
                l = r = std::tanh (1.8f * lp);
                break;
            }
            default: // Juno Brass: Saegen + Sub, Filterhuellkurve mit mittlerem Attack, resonant
            {
                const float d0 = freq * cents (-4.0f) * inv, d1 = freq * cents (4.0f) * inv, ds = 0.5f * freq * inv;
                ph[0] += d0; wrap (ph[0]); ph[1] += d1; wrap (ph[1]); ph[2] += ds; wrap (ph[2]);
                const float o0 = sawBL (ph[0], d0), o1 = sawBL (ph[1], d1), sub = 0.5f * sqBL (ph[2], ds);
                const float fc = 300.0f + 5000.0f * fe * (0.6f + 0.4f * vel);
                const float g = Svf::coeff (fc, sr);
                float lp, bp;
                f_[0][0].process (o0 + 0.6f * o1 + sub, g, 0.5f, lp, bp); l = lp;
                f_[1][0].process (o1 + 0.6f * o0 + sub, g, 0.5f, lp, bp); r = lp;
                break;
            }
        }
        outL = l * e * level;
        outR = r * e * level;
    }

private:
    float rnd01() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (float) (rng & 0xFFFFFF) / 16777216.0f; }
    float noise() { return 2.0f * rnd01() - 1.0f; }

    int sound = 0;
    float freq = 440.0f, vel = 1.0f, level = 0.1f, lpOld = 0.0f;
    double sr = 44100.0;
    uint32_t rng = 1;
    uint64_t t = 0;
    float ph[4] = {}, lfoP = 0, lfoF = 0, wowP = 0, fltP = 0, vibP = 0, morphP = 0;
    Adsr amp, fenv;
    Svf f_[2][3];
};
} // namespace snd
