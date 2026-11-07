#pragma once
// Effektkette fuer Mode Wheel. Reines C++17 (ohne JUCE), damit separat testbar.
// Eigene Nachbauten des *Charakters* bekannter Pedale/Geraete, keine exakten Schaltungsmodelle:
//   Fuzz (Germanium-Fuzz-Face-Art) -> Vibrato (Tape-Wobble) -> Phaser (Small-Stone-Art, 4 Stufen)
//   -> Tape-Echo (Space-Echo-Art) -> Spring-Reverb (vereinfacht, Holy-Grail-Spring-Art)
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace fx
{
constexpr float kPi = 3.14159265358979323846f;

struct Params
{
    float fuzz = 0.0f;                       // 0..1
    float vibDepth = 0.0f, vibRate = 3.0f;   // 0..1, Hz
    float phaser = 0.0f, phaserRate = 0.5f;  // 0..1, Hz
    float echoMix = 0.0f, echoTime = 0.3f, echoFeedback = 0.4f;   // 0..1, Sekunden, 0..0.95
    float springMix = 0.0f;                  // 0..1 (nur wenn Spring-Reverb gewaehlt)
};

struct OnePole   // Tiefpass
{
    float z = 0.0f;
    static float coef (float fc, double sr) { return 1.0f - std::exp (-2.0f * kPi * fc / (float) sr); }
    float lp (float x, float c) { z += c * (x - z); return z; }
};

inline float lerp (float a, float b, float t) { return a + (b - a) * t; }

// Weicher Limiter: unter 0.8 unveraendert, darueber sanft gegen 1.0 (verhindert hartes Clipping bei Extremeinstellungen)
inline float softLimit (float x)
{
    const float a = std::fabs (x);
    if (a <= 0.8f) return x;
    const float y = 0.8f + 0.2f * std::tanh ((a - 0.8f) / 0.2f);
    return x < 0.0f ? -y : y;
}

// Ringpuffer mit gebrochener Verzoegerung (linear)
struct DelayLine
{
    std::vector<float> buf;
    size_t mask = 0, w = 0;
    void init (size_t minSize)
    {
        size_t n = 16; while (n < minSize) n <<= 1;
        buf.assign (n, 0.0f); mask = n - 1; w = 0;
    }
    void push (float x) { buf[w] = x; w = (w + 1) & mask; }
    float read (float d) const      // d >= 1 Samples
    {
        const float pos = (float) w - d;
        const float fl = std::floor (pos);
        const size_t i0 = ((size_t) (long) fl) & mask, i1 = (i0 + 1) & mask;
        const float fr = pos - fl;
        return buf[i0] + (buf[i1] - buf[i0]) * fr;
    }
};

//==============================================================================
class Fuzz
{
public:
    void prepare (double sr) { sr_ = sr; lpC = OnePole::coef (6500.0f, sr); hpC = 1.0f - std::exp (-2.0f * kPi * 25.0f / (float) sr); reset(); }
    void reset() { for (auto& s : st) s = {}; last = 0.0f; }

    void process (float* L, float* R, int n, float amt)
    {
        if (amt <= 0.0005f && last <= 0.0005f) { last = amt; return; }
        float* ch[2] = { L, R };
        for (int c = 0; c < 2; ++c)
        {
            auto& s = st[c];
            for (int i = 0; i < n; ++i)
            {
                const float a = lerp (last, amt, (float) (i + 1) / (float) n);
                const float gain = 5.0f + 150.0f * a * std::sqrt (a);
                const float wetMix = std::min (1.0f, a * 5.0f);
                const float x = ch[c][i];
                // 2x Oversampling (grob): Mittelpunkt interpolieren, beide Punkte formen, mitteln
                const float mid = 0.5f * (s.prev + x);
                const float y = 0.5f * (shape (mid, gain) + shape (x, gain));
                s.prev = x;
                // Nach dem Clipping: Gleichanteil weg, Hoehen dunkler (Germanium-Fuzz klingt "rund")
                s.hp += hpC * (y - s.hp);
                float v = s.lpf.lp (y - s.hp, lpC);
                v *= 0.2f;                                   // Ausgangspegel angleichen
                ch[c][i] = lerp (x, v, wetMix);
            }
        }
        last = amt;
    }

private:
    static float shape (float x, float gain)
    {
        constexpr float bias = 0.28f;                         // asymmetrisch wie ein Germanium-Transistor
        return std::tanh (gain * x + bias) - std::tanh (bias);
    }
    struct St { float prev = 0.0f, hp = 0.0f; OnePole lpf; };
    St st[2];
    double sr_ = 44100.0;
    float lpC = 0.5f, hpC = 0.01f, last = 0.0f;
};

//==============================================================================
class Vibrato
{
public:
    void prepare (double sr, uint32_t seed = 1234u)
    {
        sr_ = sr; rng = seed;
        for (auto& d : dl) d.init ((size_t) (0.06 * sr) + 8);
        phase = 0.0f; drift[0] = drift[1] = 0.0f; target[0] = target[1] = 0.0f; counter = 0; last = 0.0f;
    }

    void process (float* L, float* R, int n, float depth, float rate)
    {
        float* ch[2] = { L, R };
        const float centre = 0.012f * (float) sr_;           // 12 ms Grundverzoegerung
        const float maxMod = 0.0035f * (float) sr_;          // bis 3.5 ms Hub
        const int driftEvery = (int) (0.35 * sr_);
        const float driftC = 1.0f - std::exp (-2.0f * kPi * 1.3f / (float) sr_);

        for (int i = 0; i < n; ++i)
        {
            if (++counter >= driftEvery)                     // zufaellige "Tape-Instabilitaet"
            {
                counter = 0;
                for (int c = 0; c < 2; ++c) target[c] = 2.0f * rnd01() - 1.0f;
            }
            phase += rate / (float) sr_; phase -= std::floor (phase);
            const float d = lerp (last, depth, (float) (i + 1) / (float) n);
            const float wet = std::min (1.0f, d * 10.0f);
            for (int c = 0; c < 2; ++c)
            {
                drift[c] += driftC * (target[c] - drift[c]);
                const float x = ch[c][i];
                dl[c].push (x);
                if (d <= 0.0005f) continue;
                const float lfo = std::sin (2.0f * kPi * (phase + 0.15f * (float) c)) + 0.6f * drift[c];
                const float del = std::max (2.0f, centre + maxMod * d * lfo / 1.6f);
                ch[c][i] = lerp (x, dl[c].read (del), wet);
            }
        }
        last = depth;
    }

private:
    float rnd01() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (float) (rng & 0xFFFFFF) / 16777216.0f; }
    DelayLine dl[2];
    double sr_ = 44100.0;
    uint32_t rng = 1;
    float phase = 0.0f, drift[2] = {}, target[2] = {}, last = 0.0f;
    int counter = 0;
};

//==============================================================================
class Phaser
{
public:
    void prepare (double sr) { sr_ = sr; phase = 0.0f; for (auto& c : ap) for (auto& z : c) z = 0.0f; fbState[0] = fbState[1] = 0.0f; last = 0.0f; }

    void process (float* L, float* R, int n, float mix, float rate)
    {
        float* ch[2] = { L, R };
        const float fb = 0.45f;                              // "Color"-Schalter der Small Stone: Rueckkopplung an
        for (int i = 0; i < n; ++i)
        {
            phase += rate / (float) sr_; phase -= std::floor (phase);
            const float m = lerp (last, mix, (float) (i + 1) / (float) n);
            for (int c = 0; c < 2; ++c)
            {
                const float lfo = 0.5f + 0.5f * std::sin (2.0f * kPi * (phase + 0.25f * (float) c));
                const float fc = 200.0f * std::exp2 (4.0f * lfo);           // 200 Hz .. 3.2 kHz
                const float t = std::tan (kPi * std::min (fc, (float) sr_ * 0.45f) / (float) sr_);
                const float a = (t - 1.0f) / (t + 1.0f);
                const float x = ch[c][i];
                float y = x + fb * fbState[c];
                for (int s = 0; s < 4; ++s)
                {
                    const float o = a * y + ap[c][s];
                    ap[c][s] = y - a * o;
                    y = o;
                }
                fbState[c] = y;
                ch[c][i] = x * (1.0f - 0.5f * m) + y * 0.5f * m;
            }
        }
        last = mix;
    }

private:
    double sr_ = 44100.0;
    float phase = 0.0f, ap[2][4] = {}, fbState[2] = {}, last = 0.0f;
};

//==============================================================================
class TapeEcho
{
public:
    void prepare (double sr)
    {
        sr_ = sr;
        for (auto& d : dl) d.init ((size_t) (1.3 * sr));
        for (auto& v : lpS) v = {};
        for (auto& v : hpS) v = 0.0f;
        wow = 0.0f; flut = 0.0f; smoothT = 0.3f * (float) sr; last = 0.0f;
        lpC = OnePole::coef (3500.0f, sr); hpC = 1.0f - std::exp (-2.0f * kPi * 140.0f / (float) sr);
        tC = 1.0f - std::exp (-1.0f / (0.09f * (float) sr));   // Zeit-Glaettung -> Tonhoehen-Schlieren bei Aenderung
    }

    void process (float* L, float* R, int n, float mix, float time, float feedback)
    {
        float* ch[2] = { L, R };
        const float targetT = std::min (std::max (time, 0.04f), 1.2f) * (float) sr_;
        const float fbk = std::min (feedback, 0.95f);
        for (int i = 0; i < n; ++i)
        {
            wow += 0.55f / (float) sr_;  wow -= std::floor (wow);
            flut += 6.3f / (float) sr_;  flut -= std::floor (flut);
            smoothT += tC * (targetT - smoothT);
            const float m = lerp (last, mix, (float) (i + 1) / (float) n);
            const float mod = 0.0006f * std::sin (2.0f * kPi * wow) + 0.00015f * std::sin (2.0f * kPi * flut);
            for (int c = 0; c < 2; ++c)
            {
                const float x = ch[c][i];
                const float t = std::max (2.0f, smoothT * (c == 0 ? 1.0f : 1.07f) + mod * (float) sr_);
                const float d = dl[c].read (t);
                // Rueckkopplung: Saettigung + dunkler werdende Wiederholungen
                float f = std::tanh (1.2f * d);
                hpS[c] += hpC * (f - hpS[c]);
                f = lpS[c].lp (f - hpS[c], lpC);
                dl[c].push (x + fbk * f);
                ch[c][i] = x + m * d;
            }
        }
        last = mix;
    }

private:
    DelayLine dl[2];
    OnePole lpS[2];
    float hpS[2] = {};
    double sr_ = 44100.0;
    float wow = 0.0f, flut = 0.0f, smoothT = 13000.0f, last = 0.0f, lpC = 0.3f, hpC = 0.02f, tC = 0.001f;
};

//==============================================================================
// Vereinfachter Spring-Reverb: zwei Federn = Rueckkopplungsschleife mit vielen Allpaessen
// (Dispersion -> das typische "Boing"/Zwitschern) und dunkler werdenden Wiederholungen.
class Spring
{
public:
    void prepare (double sr)
    {
        sr_ = sr;
        const double len[2] = { 0.0331, 0.0377 };
        for (int c = 0; c < 2; ++c)
        {
            dl[c].init ((size_t) (len[c] * sr) + 8);
            delaySamples[c] = (float) (len[c] * sr);
            lp[c] = {}; dc[c] = 0.0f;
            for (auto& z : ap[c]) z = 0.0f;
        }
        lpC = OnePole::coef (3000.0f, sr);
        dcC = 1.0f - std::exp (-2.0f * kPi * 80.0f / (float) sr);
        last = 0.0f;
    }

    void process (float* L, float* R, int n, float mix)
    {
        if (mix <= 0.0005f && last <= 0.0005f) { last = mix; return; }
        float* ch[2] = { L, R };
        constexpr int N = 28;
        const float a = 0.62f, fb = 0.955f;
        for (int i = 0; i < n; ++i)
        {
            const float m = lerp (last, mix, (float) (i + 1) / (float) n);
            const float in = 0.5f * (L[i] + R[i]);
            for (int c = 0; c < 2; ++c)
            {
                float y = in + fb * lp[c].lp (dl[c].read (delaySamples[c]), lpC);
                for (int s = 0; s < N; ++s)                   // Dispersion
                {
                    const float o = a * y + ap[c][s];
                    ap[c][s] = y - a * o;
                    y = o;
                }
                dc[c] += dcC * (y - dc[c]);
                y -= dc[c];
                dl[c].push (y);
                const float wet = y * 0.55f;
                ch[c][i] = ch[c][i] * (1.0f - 0.3f * m) + wet * m * 1.6f;
            }
        }
        last = mix;
    }

private:
    DelayLine dl[2];
    OnePole lp[2];
    double sr_ = 44100.0;
    float delaySamples[2] = {}, ap[2][28] = {}, dc[2] = {}, lpC = 0.3f, dcC = 0.01f, last = 0.0f;
};

//==============================================================================
class Chain
{
public:
    void prepare (double sr)
    {
        fuzz.prepare (sr); vib.prepare (sr); phaser.prepare (sr); echo.prepare (sr); spring.prepare (sr);
    }
    void process (float* L, float* R, int n, const Params& p)
    {
        fuzz.process (L, R, n, p.fuzz);
        vib.process (L, R, n, p.vibDepth, p.vibRate);
        phaser.process (L, R, n, p.phaser, p.phaserRate);
        echo.process (L, R, n, p.echoMix, p.echoTime, p.echoFeedback);
        spring.process (L, R, n, p.springMix);
    }
private:
    Fuzz fuzz; Vibrato vib; Phaser phaser; TapeEcho echo; Spring spring;
};
} // namespace fx
