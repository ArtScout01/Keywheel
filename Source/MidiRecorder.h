#pragma once
// Nimmt die vom Plugin erzeugten Noten auf und schreibt sie als Standard-MIDI-File (Format 1).
// Reines C++17 (ohne JUCE), damit separat testbar. Kein Speicher wird im Audio-Thread alloziert.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace midirec
{
constexpr int kTicks = 960;            // Aufloesung: Ticks pro Viertelnote

struct Event { double ppq; uint8_t status, d1, d2; };

class Recorder
{
public:
    Recorder() { events.reserve (200000); }

    void start (double bpmAtStart)
    {
        events.clear();
        on.fill (false);
        noteOns = 0;
        bpm = bpmAtStart >= 20.0 && bpmAtStart <= 400.0 ? bpmAtStart : 120.0;
        recording = true;
    }

    // Audio-Thread: nur Note-On/Off. ppq = absolute Zeit in Viertelnoten.
    void push (double ppq, uint8_t status, uint8_t d1, uint8_t d2)
    {
        if (! recording) return;
        const uint8_t type = status & 0xf0;
        const bool isOn  = type == 0x90 && d2 > 0;
        const bool isOff = type == 0x80 || (type == 0x90 && d2 == 0);
        if (! isOn && ! isOff) return;
        const size_t idx = (size_t) (status & 0x0f) * 128 + (d1 & 0x7f);
        if (isOff && ! on[idx]) return;                               // Note-Off ohne passendes Note-On ignorieren
        if (events.size() >= events.capacity()) return;                // nie neu allozieren
        on[idx] = isOn;
        if (isOn) ++noteOns;
        // Velocity-0-Note-On wird als echtes Note-Off gespeichert
        const uint8_t st = isOff ? (uint8_t) (0x80 | (status & 0x0f)) : status;
        events.push_back ({ ppq, st, (uint8_t) (d1 & 0x7f), (uint8_t) (isOff ? 0 : (d2 & 0x7f)) });
    }

    // Beendet die Aufnahme; haengende Noten werden bei nowPpq abgeschaltet.
    void stop (double nowPpq)
    {
        if (! recording) return;
        for (size_t i = 0; i < on.size(); ++i)
            if (on[i] && events.size() < events.capacity())
                events.push_back ({ nowPpq, (uint8_t) (0x80 | (i / 128)), (uint8_t) (i % 128), 0 });
        on.fill (false);
        recording = false;
    }

    bool isRecording() const { return recording; }
    bool empty() const { return events.empty(); }
    int noteCount() const { return noteOns; }

    std::vector<uint8_t> toMidiFile() const
    {
        std::vector<uint8_t> out;
        if (events.empty()) return out;

        // Bei einem Taktanfang (4/4) beginnen, damit das Einfuegen im DAW auf dem Raster landet
        double firstOn = events.front().ppq;
        for (auto& e : events) if ((e.status & 0xf0) == 0x90 && e.d2 > 0) { firstOn = e.ppq; break; }
        const double origin = std::floor (firstOn / 4.0 + 1e-9) * 4.0;

        struct T { uint32_t tick; int prio; size_t order; Event e; };
        std::vector<T> ev;
        ev.reserve (events.size());
        for (size_t i = 0; i < events.size(); ++i)
        {
            const auto& e = events[i];
            const double t = std::max (0.0, (e.ppq - origin) * (double) kTicks);
            const bool isOn = (e.status & 0xf0) == 0x90 && e.d2 > 0;
            ev.push_back ({ (uint32_t) std::llround (t), isOn ? 1 : 0, i, e });   // Note-Off vor Note-On bei gleichem Tick
        }
        std::stable_sort (ev.begin(), ev.end(), [] (const T& a, const T& b)
                          { return a.tick != b.tick ? a.tick < b.tick : (a.prio != b.prio ? a.prio < b.prio : a.order < b.order); });

        const uint32_t lastTick = ev.back().tick;
        const uint32_t bar = (uint32_t) kTicks * 4;
        const uint32_t endTick = std::max<uint32_t> (bar, ((lastTick + bar - 1) / bar) * bar);

        // ---- Header: Format 1, 2 Tracks ----
        put4 (out, "MThd"); put32 (out, 6); put16 (out, 1); put16 (out, 2); put16 (out, (uint16_t) kTicks);

        // ---- Track 0: Tempo + Taktart ----
        std::vector<uint8_t> t0;
        const uint32_t us = (uint32_t) std::llround (60000000.0 / bpm);
        vlq (t0, 0); t0.insert (t0.end(), { 0xFF, 0x51, 0x03, (uint8_t) (us >> 16), (uint8_t) (us >> 8), (uint8_t) us });
        vlq (t0, 0); t0.insert (t0.end(), { 0xFF, 0x58, 0x04, 0x04, 0x02, 0x18, 0x08 });
        vlq (t0, 0); t0.insert (t0.end(), { 0xFF, 0x2F, 0x00 });
        chunk (out, t0);

        // ---- Track 1: Noten ----
        std::vector<uint8_t> t1;
        static const char name[] = "Mode Wheel";
        vlq (t1, 0); t1.insert (t1.end(), { 0xFF, 0x03, (uint8_t) (sizeof (name) - 1) });
        t1.insert (t1.end(), name, name + sizeof (name) - 1);
        uint32_t prev = 0;
        for (auto& x : ev)
        {
            vlq (t1, x.tick - prev); prev = x.tick;
            t1.push_back (x.e.status); t1.push_back (x.e.d1); t1.push_back (x.e.d2);
        }
        vlq (t1, endTick - prev);
        t1.insert (t1.end(), { 0xFF, 0x2F, 0x00 });
        chunk (out, t1);
        return out;
    }

private:
    static void put4 (std::vector<uint8_t>& o, const char* s) { o.insert (o.end(), s, s + 4); }
    static void put32 (std::vector<uint8_t>& o, uint32_t v) { for (int i = 3; i >= 0; --i) o.push_back ((uint8_t) (v >> (8 * i))); }
    static void put16 (std::vector<uint8_t>& o, uint16_t v) { o.push_back ((uint8_t) (v >> 8)); o.push_back ((uint8_t) v); }
    static void vlq (std::vector<uint8_t>& o, uint32_t v)
    {
        uint8_t b[5]; int n = 0;
        b[n++] = v & 0x7f;
        while ((v >>= 7) != 0) b[n++] = (uint8_t) ((v & 0x7f) | 0x80);
        while (n > 0) o.push_back (b[--n]);
    }
    static void chunk (std::vector<uint8_t>& out, const std::vector<uint8_t>& body)
    {
        put4 (out, "MTrk"); put32 (out, (uint32_t) body.size());
        out.insert (out.end(), body.begin(), body.end());
    }

    std::vector<Event> events;
    std::array<bool, 16 * 128> on {};
    double bpm = 120.0;
    int noteOns = 0;
    bool recording = false;
};
} // namespace midirec
