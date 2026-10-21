#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "Theory.h"

struct SawSound : juce::SynthesiserSound
{
    bool appliesToNote (int) override { return true; }
    bool appliesToChannel (int) override { return true; }
};

class SawVoice : public juce::SynthesiserVoice
{
public:
    bool canPlaySound (juce::SynthesiserSound* s) override { return dynamic_cast<SawSound*> (s) != nullptr; }
    void startNote (int midi, float vel, juce::SynthesiserSound*, int) override
    {
        freq = juce::MidiMessage::getMidiNoteInHertz (midi);
        level = vel * 0.15f; phase = 0; lp = 0;
        adsr.setSampleRate (getSampleRate());
        adsr.setParameters ({ 0.01f, 0.25f, 0.7f, 0.35f });
        adsr.noteOn();
    }
    void stopNote (float, bool allowTail) override
    {
        if (allowTail) adsr.noteOff(); else { adsr.reset(); clearCurrentNote(); }
    }
    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}
    void renderNextBlock (juce::AudioBuffer<float>& b, int start, int num) override
    {
        if (! adsr.isActive()) { clearCurrentNote(); return; }
        const double sr = getSampleRate();
        for (int i = 0; i < num; ++i)
        {
            const float e = adsr.getNextSample();
            lp += 0.2f * ((float) (2.0 * phase - 1.0) - lp);
            const float v = lp * e * level;
            for (int ch = 0; ch < b.getNumChannels(); ++ch) b.addSample (ch, start + i, v);
            phase += freq / sr; if (phase >= 1.0) phase -= 1.0;
            if (! adsr.isActive()) { clearCurrentNote(); break; }
        }
    }
private:
    juce::ADSR adsr; double freq = 440, phase = 0; float level = 0.1f, lp = 0;
};

class ModeWheelProcessor : public juce::AudioProcessor
{
public:
    ModeWheelProcessor();
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& l) const override { return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo(); }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Mode Wheel"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    void startProgression (int presetIndex) { progPreset = presetIndex; progStartReq = true; }
    void stopProgression() { progStopReq = true; }
    bool isProgressionRunning() const { return progRunning.load(); }

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyState;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    juce::Synthesiser synth;
    std::atomic<int> progPreset { 0 };
    std::atomic<bool> progStartReq { false }, progStopReq { false }, progRunning { false };
    int progStep = 0;
    double progCounter = 0, sampleRate = 44100;
    std::vector<int> progHeld;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModeWheelProcessor)
};
