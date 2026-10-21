#pragma once
// Key- und BPM-Erkennung. Reines C++17, keine JUCE-Abhaengigkeit (dadurch separat testbar).
#include <algorithm>
#include <cmath>
#include <complex>
#include <functional>
#include <vector>

namespace analysis
{
struct Result
{
    bool ok = false;
    int   tonicPc = 0;        // 0 = C ... 11 = B
    bool  minor = false;
    float keyScore = 0.0f;    // Korrelation des besten Keys (0..1)
    float keyMargin = 0.0f;   // Abstand zum zweitbesten Key
    int   altTonicPc = 0;     // zweitbester Key (oft die Parallel-Tonart)
    bool  altMinor = false;
    float bpm = 0.0f;
    float bpmAlt = 0.0f;      // halbes oder doppeltes Tempo als Alternative
    float bpmScore = 0.0f;    // 0..1, wie deutlich der Puls ist
};

namespace detail
{
using Cd = std::complex<double>;

inline void fft (std::vector<Cd>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * 3.14159265358979323846 / (double) len;
        const Cd wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            Cd w (1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const Cd u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

inline size_t nearestPow2 (double x)
{
    return (size_t) 1 << (int) std::lround (std::log2 (std::max (2.0, x)));
}
inline size_t ceilPow2 (double x)
{
    return (size_t) 1 << (int) std::ceil (std::log2 (std::max (2.0, x)));
}

inline std::vector<double> hann (size_t n)
{
    std::vector<double> w (n);
    for (size_t i = 0; i < n; ++i)
        w[i] = 0.5 - 0.5 * std::cos (2.0 * 3.14159265358979323846 * (double) i / (double) n);
    return w;
}
} // namespace detail

//==============================================================================
// KEY: Peak-basiertes Chroma + Tuning-Schaetzung + Krumhansl-Kessler-Profile
//==============================================================================
struct KeyResult { int tonic = 0; bool minor = false; float score = 0, margin = 0; int altTonic = 0; bool altMinor = false; bool ok = false; };

inline KeyResult detectKey (const std::vector<float>& x, double sr, const std::function<bool()>& cancelled)
{
    using namespace detail;
    KeyResult kr;
    const size_t N = ceilPow2 (0.34 * sr), hop = N / 2;
    if (x.size() < N * 2) return kr;

    const auto win = hann (N);
    struct Peak { float midi, w; };
    std::vector<std::vector<Peak>> frames;
    std::vector<double> frameMax;
    std::vector<Cd> buf (N);
    std::vector<double> mag (N / 2);

    const size_t kLo = (size_t) std::ceil (65.0 * (double) N / sr), kHi = std::min (N / 2 - 2, (size_t) (2100.0 * (double) N / sr));

    for (size_t pos = 0; pos + N <= x.size(); pos += hop)
    {
        if (cancelled && cancelled()) return kr;
        for (size_t i = 0; i < N; ++i) buf[i] = Cd (x[pos + i] * win[i], 0.0);
        fft (buf);
        double mx = 0;
        for (size_t k = 0; k < N / 2; ++k) { mag[k] = std::abs (buf[k]); }
        for (size_t k = kLo; k <= kHi; ++k) mx = std::max (mx, mag[k]);
        frameMax.push_back (mx);

        std::vector<Peak> peaks;
        for (size_t k = std::max<size_t> (kLo, 2); k <= kHi; ++k)
        {
            if (mag[k] > mag[k - 1] && mag[k] >= mag[k + 1] && mag[k] > 0.08 * mx)
            {
                const double a = std::log (mag[k - 1] + 1e-12), b = std::log (mag[k] + 1e-12), c = std::log (mag[k + 1] + 1e-12);
                const double den = a - 2.0 * b + c;
                const double d = std::abs (den) > 1e-12 ? 0.5 * (a - c) / den : 0.0;
                const double f = ((double) k + std::clamp (d, -0.5, 0.5)) * sr / (double) N;
                peaks.push_back ({ (float) (69.0 + 12.0 * std::log2 (f / 440.0)), (float) std::sqrt (mag[k]) });
            }
        }
        frames.push_back (std::move (peaks));
    }
    if (frames.empty()) return kr;

    const double gMax = *std::max_element (frameMax.begin(), frameMax.end());
    if (gMax <= 0) return kr;

    // Tuning (zirkulaerer Mittelwert der Abweichung vom Halbtonraster)
    double sc = 0, ss = 0;
    for (size_t f = 0; f < frames.size(); ++f)
    {
        if (frameMax[f] < 0.02 * gMax) continue;
        for (auto& p : frames[f])
        {
            const double dev = p.midi - std::round (p.midi);
            sc += p.w * std::cos (2.0 * 3.14159265358979323846 * dev);
            ss += p.w * std::sin (2.0 * 3.14159265358979323846 * dev);
        }
    }
    const double tuning = std::atan2 (ss, sc) / (2.0 * 3.14159265358979323846); // in Halbtonen

    double chroma[12] = {};
    for (size_t f = 0; f < frames.size(); ++f)
    {
        if (frameMax[f] < 0.02 * gMax) continue;
        double fc[12] = {}, tot = 0;
        for (auto& p : frames[f])
        {
            const int pc = (((int) std::lround (p.midi - tuning)) % 12 + 12) % 12;
            fc[pc] += p.w; tot += p.w;
        }
        if (tot <= 0) continue;
        for (int i = 0; i < 12; ++i) chroma[i] += fc[i] / tot;   // jeder Frame zaehlt gleich viel
    }

    static const double MAJ[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    static const double MIN[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };

    auto corr = [&] (const double* prof, int rot)
    {
        double mc = 0, mp = 0;
        for (int i = 0; i < 12; ++i) { mc += chroma[i]; mp += prof[i]; }
        mc /= 12; mp /= 12;
        double num = 0, dc = 0, dp = 0;
        for (int i = 0; i < 12; ++i)
        {
            const double c = chroma[(i + rot) % 12] - mc, p = prof[i] - mp;
            num += c * p; dc += c * c; dp += p * p;
        }
        return (dc > 0 && dp > 0) ? num / std::sqrt (dc * dp) : 0.0;
    };

    struct Cand { double s; int t; bool m; };
    std::vector<Cand> c;
    for (int t = 0; t < 12; ++t) { c.push_back ({ corr (MAJ, t), t, false }); c.push_back ({ corr (MIN, t), t, true }); }
    std::sort (c.begin(), c.end(), [] (const Cand& a, const Cand& b) { return a.s > b.s; });

    kr.ok = true;
    kr.tonic = c[0].t; kr.minor = c[0].m; kr.score = (float) c[0].s;
    kr.altTonic = c[1].t; kr.altMinor = c[1].m; kr.margin = (float) (c[0].s - c[1].s);
    return kr;
}

//==============================================================================
// BPM: Onset-Staerke (Spektralfluss) -> Autokorrelation -> Kamm-Salienz + Tempo-Prior
//==============================================================================
struct BpmResult { bool ok = false; float bpm = 0, alt = 0, score = 0; };

inline BpmResult detectBpm (const std::vector<float>& x, double sr, const std::function<bool()>& cancelled,
                            std::vector<std::pair<double, double>>* debugCurve = nullptr)
{
    using namespace detail;
    BpmResult br;
    const size_t N = nearestPow2 (0.0232 * sr), hop = N / 4;
    if (x.size() < N * 8) return br;

    const auto win = hann (N);
    std::vector<Cd> buf (N);
    const size_t kLo = (size_t) std::ceil (40.0 * (double) N / sr), kHi = std::min (N / 2 - 1, (size_t) (9000.0 * (double) N / sr));
    std::vector<double> prev (N / 2, 0.0), env;

    for (size_t pos = 0; pos + N <= x.size(); pos += hop)
    {
        if (cancelled && cancelled()) return br;
        for (size_t i = 0; i < N; ++i) buf[i] = Cd (x[pos + i] * win[i], 0.0);
        fft (buf);
        double flux = 0;
        for (size_t k = kLo; k <= kHi; ++k)
        {
            const double m = std::log (1.0 + 1000.0 * std::abs (buf[k]) / (double) N);
            flux += std::max (0.0, m - prev[k]);
            prev[k] = m;
        }
        env.push_back (flux);
    }
    const double fr = sr / (double) hop;              // Envelope-Rate in Hz
    if (env.size() < (size_t) (fr * 6)) return br;
    env[0] = 0;

    // Lokalen Mittelwert abziehen, halbwellengleichrichten
    {
        const size_t w = (size_t) (fr * 0.4);
        std::vector<double> cs (env.size() + 1, 0.0), o (env.size());
        for (size_t i = 0; i < env.size(); ++i) cs[i + 1] = cs[i] + env[i];
        for (size_t i = 0; i < env.size(); ++i)
        {
            const size_t a = i > w ? i - w : 0, b = std::min (env.size(), i + w + 1);
            o[i] = std::max (0.0, env[i] - (cs[b] - cs[a]) / (double) (b - a));
        }
        env.swap (o);
    }
    // Gleichanteil entfernen, sonst wirkt die Autokorrelation ueberall "periodisch"
    {
        double mean = 0;
        for (double v : env) mean += v;
        mean /= (double) env.size();
        for (double& v : env) v -= mean;
    }

    // Autokorrelation bis 8 Schlaege bei 50 BPM
    const size_t maxLag = std::min (env.size() - 1, (size_t) (fr * 60.0 / 50.0 * 8.0) + 2);
    std::vector<double> ac (maxLag + 1, 0.0);
    for (size_t lag = 0; lag <= maxLag; ++lag)
    {
        double s = 0;
        for (size_t i = 0; i + lag < env.size(); ++i) s += env[i] * env[i + lag];
        ac[lag] = s / (double) (env.size() - lag);       // unverzerrt normiert
    }
    if (ac[0] <= 0) return br;

    auto acAt = [&] (double lag)
    {
        if (lag < 0 || lag >= (double) maxLag) return 0.0;
        const size_t i = (size_t) lag; const double f = lag - (double) i;
        return ac[i] * (1.0 - f) + ac[i + 1] * f;
    };

    auto salience = [&] (double bpm)
    {
        const double lag = fr * 60.0 / bpm;
        double s = 0, wsum = 0;
        for (int h = 1; h <= 8; ++h)
        {
            if (lag * h >= (double) maxLag) break;
            const double wgt = 1.0 / std::sqrt ((double) h);
            // kleines Fenster, damit leichte Tempo-Schwankungen nicht alles loeschen
            const double v = std::max ({ acAt (lag * h), 0.5 * (acAt (lag * h - 1.0) + acAt (lag * h + 1.0)) });
            s += wgt * v; wsum += wgt;
        }
        return wsum > 0 ? s / wsum / ac[0] : 0.0;
    };

    auto prior = [] (double bpm)           // log-Normalverteilung um 120 BPM
    {
        const double o = std::log2 (bpm / 120.0);
        return std::exp (-0.5 * (o / 0.9) * (o / 0.9));
    };

    double best = 0, bestBpm = 0;
    std::vector<std::pair<double, double>> curve;       // (bpm, gewichtete Salienz)
    for (double bpm = 55.0; bpm <= 215.0; bpm += 0.1)
    {
        // Der echte Puls hat meist auch eine Unterteilung (Achtel/Hi-Hats): doppeltes Tempo stuetzt den Kandidaten.
        const double s = (salience (bpm) + 0.5 * salience (bpm * 2.0) + 0.25 * salience (bpm * 0.5)) * prior (bpm);
        curve.push_back ({ bpm, s });
        if (s > best) { best = s; bestBpm = bpm; }
    }
    if (bestBpm <= 0) return br;
    if (debugCurve) *debugCurve = curve;

    // Halbes/doppeltes Tempo als Alternative
    auto bestNear = [&] (double centre)
    {
        double b = 0, bb = 0;
        if (centre < 55 || centre > 215) return 0.0;
        for (auto& [bpm, s] : curve) if (std::abs (bpm - centre) < centre * 0.03 && s > b) { b = s; bb = bpm; }
        return bb;
    };
    const double a1 = bestNear (bestBpm * 2.0), a2 = bestNear (bestBpm * 0.5);
    br.alt = (float) (a1 > 0 ? a1 : a2);

    // auf ganze BPM einrasten, wenn sehr nah dran (Produktionen laufen fast immer auf runden Tempi)
    const double rounded = std::round (bestBpm);
    br.bpm = (float) (std::abs (bestBpm - rounded) < 0.25 ? rounded : std::round (bestBpm * 10.0) / 10.0);
    br.score = (float) std::clamp (salience (bestBpm) * 2.0, 0.0, 1.0);   // 0 = kein Puls erkennbar
    br.ok = true;
    return br;
}

//==============================================================================
inline Result analyse (std::vector<float> mono, double sampleRate, const std::function<bool()>& cancelled = {})
{
    Result r;
    float peak = 0.0f;
    for (float v : mono) peak = std::max (peak, std::abs (v));
    if (peak < 1.0e-5f || sampleRate < 8000.0) return r;          // Stille oder unbrauchbar
    const float g = 0.95f / peak;                                    // Pegel normalisieren
    for (float& v : mono) v *= g;

    const auto k = detectKey (mono, sampleRate, cancelled);
    const auto b = detectBpm (mono, sampleRate, cancelled);
    if (k.ok)
    {
        r.tonicPc = k.tonic; r.minor = k.minor; r.keyScore = k.score; r.keyMargin = k.margin;
        r.altTonicPc = k.altTonic; r.altMinor = k.altMinor;
    }
    if (b.ok) { r.bpm = b.bpm; r.bpmAlt = b.alt; r.bpmScore = b.score; }
    r.ok = k.ok || b.ok;
    return r;
}
} // namespace analysis
