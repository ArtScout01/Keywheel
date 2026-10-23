#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "Theory.h"
#include "SoundEngine.h"
#include "Effects.h"

struct SawSound : juce::SynthesiserSound
{
    bool appliesToNote (int) override { return true; }
    bool appliesToChannel (int) override { return true; }
};

class SynthVoice : public juce::SynthesiserVoice
{
public:
    explicit SynthVoice (std::atomic<float>* soundParam) : sound (soundParam) {}
    bool canPlaySound (juce::SynthesiserSound* s) override { return dynamic_cast<SawSound*> (s) != nullptr; }
    void startNote (int midi, float vel, juce::SynthesiserSound*, int) override
    {
        engine.start ((int) sound->load(), (float) juce::MidiMessage::getMidiNoteInHertz (midi), vel, getSampleRate(),
                      (uint32_t) juce::Random::getSystemRandom().nextInt());
    }
    void stopNote (float, bool allowTail) override
    {
        if (allowTail) engine.release(); else { engine.stopNow(); clearCurrentNote(); }
    }
    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}
    void renderNextBlock (juce::AudioBuffer<float>& b, int start, int num) override
    {
        if (! engine.active()) { clearCurrentNote(); return; }
        const int chans = b.getNumChannels();
        for (int i = 0; i < num; ++i)
        {
            float l = 0.0f, r = 0.0f;
            engine.next (l, r);
            if (chans > 1)
            {
                b.addSample (0, start + i, l);
                b.addSample (1, start + i, r);
                for (int ch = 2; ch < chans; ++ch) b.addSample (ch, start + i, 0.5f * (l + r));
            }
            else if (chans == 1)
                b.addSample (0, start + i, 0.5f * (l + r));
            if (! engine.active()) { clearCurrentNote(); break; }
        }
    }
private:
    std::atomic<float>* sound;
    snd::Engine engine;
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
    juce::Reverb reverb;
    fx::Chain fxChain;
    std::atomic<int> progPreset { 0 };
    std::atomic<bool> progStartReq { false }, progStopReq { false }, progRunning { false };
    int progStep = 0;
    double progCounter = 0, sampleRate = 44100;
    std::vector<int> progHeld;

    // Which output notes were started for each incoming note (so note-offs always match,
    // even if parameters change while a chord is held)
    struct HeldChord { int count = 0; std::array<int, 6> notes {}; };
    std::array<HeldChord, 16 * 128> heldMap;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModeWheelProcessor)
};
