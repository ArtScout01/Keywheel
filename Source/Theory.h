#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <vector>

#define FLT_ "\xe2\x99\xad"
#define SHP_ "\xe2\x99\xaf"

namespace theory
{
inline const int MAJ[7] = { 0, 2, 4, 5, 7, 9, 11 };
inline const char* modeNames[6] = { "Major", "Minor", "Dorian", "Phrygian", "Mixolydian", "Lydian" };
inline const int modeK[6]       = { 0, 5, 1, 2, 4, 3 };
inline const char* RN[7] = { "I", "II", "III", "IV", "V", "VI", "VII" };

inline const char* const* sharpNames()
{
    static const char* n[12] = { "C", "C" SHP_, "D", "D" SHP_, "E", "F", "F" SHP_, "G", "G" SHP_, "A", "A" SHP_, "B" };
    return n;
}
inline const char* const* flatNames()
{
    static const char* n[12] = { "C", "D" FLT_, "D", "E" FLT_, "E", "F", "G" FLT_, "G", "A" FLT_, "A", "B" FLT_, "B" };
    return n;
}

inline std::array<int, 7> intervals (int mode)
{
    const int k = modeK[mode];
    std::array<int, 7> iv {};
    for (int j = 0; j < 7; ++j) iv[(size_t) j] = (MAJ[(k + j) % 7] - MAJ[k] + 12) % 12;
    return iv;
}

// Spelling follows the parent major key (sharps for G..F#, flats otherwise)
inline const char* const* names (int tonicPc, int mode)
{
    const int par = ((tonicPc - MAJ[modeK[mode]]) % 12 + 12) % 12;
    const int idx = (par * 7) % 12;
    return (idx >= 1 && idx <= 6) ? sharpNames() : flatNames();
}

inline juce::String noteName (int pc, int tonicPc, int mode)
{
    return juce::String::fromUTF8 (names (tonicPc, mode)[((pc % 12) + 12) % 12]);
}

struct Chord
{
    int rootPc = 0, degree = 0;
    std::vector<int> ints;
    juce::String roman, degLabel, name;
};

// ext: 0 = triad, 1 = seventh chord, 2 = ninth chord (stacked diatonically from the mode)
inline const char* extNames[3] = { "Triads", "7th chords", "9th chords" };

inline Chord chord (int tonicPc, int mode, int d, int ext = 0)
{
    const auto iv = intervals (mode);
    auto up = [&] (int steps) { return (iv[(size_t) ((d + steps) % 7)] - iv[(size_t) d] + 12) % 12; };
    const int th = up (2), fi = up (4), sv = up (6), nn = up (1) + 12;
    const bool minor = (th == 3), dim = (fi == 6);
    const int v = iv[(size_t) d];
    juce::String pf = v < MAJ[d] ? juce::String::fromUTF8 (FLT_) : v > MAJ[d] ? juce::String::fromUTF8 (SHP_) : juce::String();
    Chord c;
    c.degree = d;
    c.rootPc = (tonicPc + v) % 12;
    c.ints = { 0, th, fi };
    if (ext >= 1) c.ints.push_back (sv);
    if (ext >= 2) c.ints.push_back (nn);
    c.degLabel = pf + juce::String (d + 1);

    // Roman numeral
    juce::String r = RN[d];
    const bool halfDim = dim && ext >= 1 && sv == 10;
    juce::String dimMark = dim ? juce::String::fromUTF8 (halfDim ? "\xc3\xb8" : "\xc2\xb0") : juce::String();
    juce::String tag;
    if (ext >= 1) tag = juce::String (sv == 11 ? "maj" : "") + (ext == 2 ? "9" : "7");
    c.roman = pf + ((minor || dim) ? r.toLowerCase() : r) + dimMark + tag;

    // Chord symbol
    juce::String suffix;
    if (ext == 0)
        suffix = dim ? "dim" : minor ? "m" : (fi == 8 ? "aug" : "");
    else
    {
        juce::String s7;
        if (dim)        s7 = (sv == 9 ? "dim7" : "m7b5");
        else if (minor) s7 = (sv == 11 ? "m(maj7)" : "m7");
        else            s7 = (sv == 11 ? "maj7" : "7");
        if (ext == 2)
        {
            const bool nat = (nn == 14);
            if (s7 == "maj7")          s7 = nat ? "maj9" : "maj7(b9)";
            else if (s7 == "7")        s7 = nat ? "9" : "7(b9)";
            else if (s7 == "m7")       s7 = nat ? "m9" : "m7(b9)";
            else if (s7 == "m(maj7)")  s7 = nat ? "m(maj9)" : "m(maj7)(b9)";
            else                       s7 += nat ? "(9)" : "(b9)";
        }
        suffix = s7;
    }
    c.name = noteName (c.rootPc, tonicPc, mode) + suffix;
    return c;
}

struct Step   { int root; std::vector<int> ints; };
struct Preset { const char* name; int tonicPc; int mode; std::vector<Step> steps; };

inline const std::vector<Preset>& presets()
{
    static const std::vector<int> maj { 0, 4, 7 }, min { 0, 3, 7 }, maj7 { 0, 4, 7, 11 }, min7 { 0, 3, 7, 10 }, hendrix { 0, 4, 7, 10, 15 };
    static const std::vector<Preset> p = {
        { "Hendrix 7#9 (C)",           0, 0, { { 0, hendrix } } },
        { "Mixolydian rock E-D-A-E",   4, 4, { { 0, maj }, { 10, maj }, { 5, maj }, { 0, maj } } },
        { "Phrygian Em-F-Em-F",        4, 3, { { 0, min }, { 1, maj }, { 0, min }, { 1, maj } } },
        { "Floating Cmaj7-Fmaj7",      0, 0, { { 0, maj7 }, { 5, maj7 } } },
        { "Dorian Dm7-G",              2, 2, { { 0, min7 }, { 5, maj } } }
    };
    return p;
}
} // namespace theory
