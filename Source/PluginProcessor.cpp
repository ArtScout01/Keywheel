#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessorValueTreeState::ParameterLayout ModeWheelProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "tonic", 1 }, "Key",
           juce::StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 0));
    juce::StringArray modes;
    for (auto* m : theory::modeNames) modes.add (m);
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "mode", 1 }, "Mode", modes, 0));
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "smart", 1 }, "Smart Chords", false));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "chordType", 1 }, "Chord Type",
           juce::StringArray { theory::extNames[0], theory::extNames[1], theory::extNames[2] }, 0));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "borrow", 1 }, "Borrow",
           juce::StringArray { "Borrow: off", "Borrow: out-of-key notes", "Borrow: all notes" }, 0));
    juce::StringArray soundNames;
    for (auto* n : snd::names) soundNames.add (n);
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "sound", 1 }, "Sound", soundNames, 0));
    l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "reverb", 1 }, "Reverb",
           juce::NormalisableRange<float> (0.0f, 1.0f), 0.2f));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "reverbType", 1 }, "Reverb Type",
           juce::StringArray { "Reverb: Hall", "Reverb: Spring" }, 0));
    auto amount = [] (const char* id, const char* name, float def)
    { return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (0.0f, 1.0f), def); };
    l.add (amount ("fuzz", "Fuzz", 0.0f));
    l.add (amount ("vibDepth", "Vibrato Depth", 0.0f));
    l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "vibRate", 1 }, "Vibrato Rate",
           juce::NormalisableRange<float> (0.2f, 8.0f, 0.0f, 0.5f), 3.0f));
    l.add (amount ("phaser", "Phaser", 0.0f));
    l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "phaserRate", 1 }, "Phaser Rate",
           juce::NormalisableRange<float> (0.1f, 6.0f, 0.0f, 0.5f), 0.5f));
    l.add (amount ("echoMix", "Echo", 0.0f));
    l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "echoTime", 1 }, "Echo Time",
           juce::NormalisableRange<float> (0.06f, 0.9f, 0.0f, 0.5f), 0.3f));
    l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "echoFb", 1 }, "Echo Feedback",
           juce::NormalisableRange<float> (0.0f, 0.95f), 0.4f));
    juce::StringArray from;
    for (auto* m : theory::modeNames) from.add (juce::String ("from ") + m);
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "borrowFrom", 1 }, "Borrow From", from, 1));
    return l;
}

ModeWheelProcessor::ModeWheelProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    for (int i = 0; i < 16; ++i) synth.addVoice (new SynthVoice (apvts.getRawParameterValue ("sound")));
    synth.addSound (new SawSound());
}

void ModeWheelProcessor::prepareToPlay (double sr, int)
{
    sampleRate = sr;
    synth.setCurrentPlaybackSampleRate (sr);
    reverb.setSampleRate (sr);
    reverb.reset();
    fxChain.prepare (sr);
}

void ModeWheelProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    const int n = buffer.getNumSamples();
    const int tonic = (int) apvts.getRawParameterValue ("tonic")->load();
    const int mode  = (int) apvts.getRawParameterValue ("mode")->load();
    const bool smart = apvts.getRawParameterValue ("smart")->load() > 0.5f;
    const int ext    = (int) apvts.getRawParameterValue ("chordType")->load();
    const int borrow = (int) apvts.getRawParameterValue ("borrow")->load();
    const int src    = (int) apvts.getRawParameterValue ("borrowFrom")->load();
    const auto iv    = theory::intervals (mode);
    const auto ivSrc = theory::intervals (src);
    const bool borrowing = borrow > 0 && src != mode;

    juce::MidiBuffer out;

    // Chord for one incoming note. Returns false if the note should pass through unchanged.
    auto chordFor = [&] (int note, std::array<int, 6>& notes, int& count) -> bool
    {
        const int rel = ((note - tonic) % 12 + 12) % 12;
        int d = -1, ds = -1;
        for (int k = 0; k < 7; ++k) { if (iv[(size_t) k] == rel) d = k; if (ivSrc[(size_t) k] == rel) ds = k; }

        int base = note;
        theory::Chord ch;
        if (d >= 0)
        {
            if (borrowing && borrow == 2)          // all notes: take the chord of this degree from the source mode
            {
                ch = theory::chord (tonic, src, d, ext);
                int diff = ivSrc[(size_t) d] - iv[(size_t) d];
                if (diff > 6) diff -= 12;
                if (diff < -5) diff += 12;
                base = note + diff;                // e.g. E -> Eb when borrowing from minor
            }
            else
                ch = theory::chord (tonic, mode, d, ext);
        }
        else if (borrowing && ds >= 0)             // out-of-key note that exists in the source mode
            ch = theory::chord (tonic, src, ds, ext);
        else
            return false;

        count = 0;
        for (int x : ch.ints)
            if (count < 6) notes[(size_t) count++] = juce::jlimit (0, 127, base + x);
        return true;
    };

    // 1) Incoming MIDI. With Smart Chords on, a note becomes a chord (diatonic, extended, or borrowed).
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        const int pos = meta.samplePosition;

        if (msg.isAllNotesOff() || msg.isAllSoundOff())
            for (auto& h : heldMap) h.count = 0;

        if (msg.isNoteOn() || msg.isNoteOff())
        {
            const int ch = juce::jlimit (1, 16, msg.getChannel());
            auto& hc = heldMap[(size_t) ((ch - 1) * 128 + msg.getNoteNumber())];

            if (msg.isNoteOff())
            {
                if (hc.count > 0)
                {
                    for (int i = 0; i < hc.count; ++i) out.addEvent (juce::MidiMessage::noteOff (ch, hc.notes[(size_t) i]), pos);
                    hc.count = 0;
                    continue;
                }
            }
            else if (smart)
            {
                std::array<int, 6> notes {};
                int count = 0;
                if (chordFor (msg.getNoteNumber(), notes, count))
                {
                    for (int i = 0; i < hc.count; ++i) out.addEvent (juce::MidiMessage::noteOff (ch, hc.notes[(size_t) i]), pos);
                    hc.count = count;
                    hc.notes = notes;
                    for (int i = 0; i < count; ++i) out.addEvent (juce::MidiMessage::noteOn (ch, notes[(size_t) i], msg.getVelocity()), pos);
                    continue;
                }
            }
        }
        out.addEvent (msg, pos);
    }

    // 2) Progression player (one chord per bar at host tempo)
    auto fire = [&] (int off)
    {
        for (int nt : progHeld) out.addEvent (juce::MidiMessage::noteOff (1, nt), off);
        progHeld.clear();
        const auto& ps = theory::presets();
        const auto& p = ps[(size_t) juce::jlimit (0, (int) ps.size() - 1, progPreset.load())];
        const auto& s = p.steps[(size_t) progStep % p.steps.size()];
        const int rp = (tonic + s.root) % 12;
        for (int x : s.ints)
        {
            const int nt = juce::jlimit (0, 127, 48 + rp + x);
            out.addEvent (juce::MidiMessage::noteOn (1, nt, (juce::uint8) 90), off);
            progHeld.push_back (nt);
        }
        ++progStep;
    };

    if (progStopReq.exchange (false))
    {
        for (int nt : progHeld) out.addEvent (juce::MidiMessage::noteOff (1, nt), 0);
        progHeld.clear();
        progRunning = false;
    }
    if (progStartReq.exchange (false))
    {
        progRunning = true; progStep = 0; fire (0); progCounter = n;
    }
    else if (progRunning)
    {
        double bpm = 120.0;
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
                if (auto b = pos->getBpm()) bpm = *b;
        const double len = sampleRate * 60.0 / juce::jmax (20.0, bpm) * 4.0;
        if (progCounter + n >= len)
        {
            fire (juce::jlimit (0, n - 1, (int) (len - progCounter)));
            progCounter = progCounter + n - len;
        }
        else progCounter += n;
    }

    // 3) Notes clicked in the UI / on the on-screen keyboard; also keeps the keyboard lit for all notes
    keyState.processNextMidiBuffer (out, 0, n, true);

    // 4) Built-in synth, and MIDI out to the host
    synth.renderNextBlock (buffer, out, 0, n);

    if (buffer.getNumChannels() >= 2)
    {
        auto val = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
        const float rv = val ("reverb");
        const bool spring = val ("reverbType") > 0.5f;

        // Effektkette: Fuzz -> Vibrato -> Phaser -> Echo -> (Spring)
        fx::Params fp;
        fp.fuzz = val ("fuzz");
        fp.vibDepth = val ("vibDepth");   fp.vibRate = val ("vibRate");
        fp.phaser = val ("phaser");       fp.phaserRate = val ("phaserRate");
        fp.echoMix = val ("echoMix");     fp.echoTime = val ("echoTime");   fp.echoFeedback = val ("echoFb");
        fp.springMix = spring ? rv : 0.0f;
        float* L = buffer.getWritePointer (0);
        float* R = buffer.getWritePointer (1);
        fxChain.process (L, R, n, fp);

        if (! spring)
        {
            juce::Reverb::Parameters rp;
            rp.roomSize = 0.75f; rp.damping = 0.45f; rp.width = 1.0f; rp.freezeMode = 0.0f;
            rp.wetLevel = 0.45f * rv; rp.dryLevel = 1.0f - 0.35f * rv;
            reverb.setParameters (rp);
            reverb.processStereo (L, R, n);
        }
        for (int i = 0; i < n; ++i) { L[i] = fx::softLimit (L[i]); R[i] = fx::softLimit (R[i]); }
    }

    // 5) MIDI out to the host
    midi.swapWith (out);
}

juce::AudioProcessorEditor* ModeWheelProcessor::createEditor() { return new ModeWheelEditor (*this); }

void ModeWheelProcessor::getStateInformation (juce::MemoryBlock& d)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, d);
}
void ModeWheelProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ModeWheelProcessor(); }
