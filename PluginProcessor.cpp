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
    return l;
}

ModeWheelProcessor::ModeWheelProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    for (int i = 0; i < 8; ++i) synth.addVoice (new SawVoice());
    synth.addSound (new SawSound());
}

void ModeWheelProcessor::prepareToPlay (double sr, int)
{
    sampleRate = sr;
    synth.setCurrentPlaybackSampleRate (sr);
}

void ModeWheelProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    const int n = buffer.getNumSamples();
    const int tonic = (int) apvts.getRawParameterValue ("tonic")->load();
    const int mode  = (int) apvts.getRawParameterValue ("mode")->load();
    const bool smart = apvts.getRawParameterValue ("smart")->load() > 0.5f;
    const auto iv = theory::intervals (mode);

    juce::MidiBuffer out;

    // 1) Incoming MIDI. With Smart Chords on, a scale note becomes the diatonic triad on that degree.
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        const int pos = meta.samplePosition;
        if (smart && (msg.isNoteOn() || msg.isNoteOff()))
        {
            const int rel = ((msg.getNoteNumber() - tonic) % 12 + 12) % 12;
            int d = -1;
            for (int k = 0; k < 7; ++k) if (iv[(size_t) k] == rel) d = k;
            if (d >= 0)
            {
                for (int x : theory::chord (tonic, mode, d).ints)
                {
                    const int nt = juce::jlimit (0, 127, msg.getNoteNumber() + x);
                    out.addEvent (msg.isNoteOn() ? juce::MidiMessage::noteOn (msg.getChannel(), nt, msg.getVelocity())
                                                 : juce::MidiMessage::noteOff (msg.getChannel(), nt), pos);
                }
                continue;
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
