#pragma once
#include "PluginProcessor.h"
#include "FileAnalyser.h"
#include <array>

class WheelComponent : public juce::Component
{
public:
    explicit WheelComponent (ModeWheelProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    std::function<void (const juce::String&)> onChord;

private:
    int tonic() const { return (int) proc.apvts.getRawParameterValue ("tonic")->load(); }
    int mode()  const { return (int) proc.apvts.getRawParameterValue ("mode")->load(); }
    int ext()   const { return (int) proc.apvts.getRawParameterValue ("chordType")->load(); }
    int hitSegment (juce::Point<float> p, bool& outerRing) const;
    ModeWheelProcessor& proc;
    std::vector<int> held;
    int heldIdx = -1;
};

class ModeWheelEditor : public juce::AudioProcessorEditor,
                        public juce::FileDragAndDropTarget,
                        private juce::Timer
{
public:
    explicit ModeWheelEditor (ModeWheelProcessor&);
    ~ModeWheelEditor() override { stopTimer(); }
    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    void timerCallback() override;
    void analyseFile (const juce::File&);
    void showOutcome (const FileAnalyser::Outcome&);
    void applyKeyToWheel();
    static bool isAudioFile (const juce::File&);
    ModeWheelProcessor& proc;
    WheelComponent wheel;
    juce::ComboBox modeBox, presetBox, extBox, borrowBox, borrowFromBox, soundBox, reverbTypeBox;

    // Effects row (Fuzz, Vibrato, Phaser, Echo, Reverb)
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };
    void setupKnob (Knob&, const juce::String& text, const char* paramId);
    std::array<Knob, 9> knobs;
    juce::Rectangle<int> fxArea;
    juce::TextButton playBtn { "Play loop" }, stopBtn { "Stop" };
    juce::ToggleButton smartBtn { "Smart chords" };
    juce::Label info;

    // Key / BPM detector
    FileAnalyser analyser;
    juce::TextButton openBtn { "Open audio file..." }, applyBtn { "Set wheel to key" };
    juce::Label detectStatus, keyLabel, bpmLabel;
    juce::Rectangle<int> dropArea;
    std::unique_ptr<juce::FileChooser> chooser;
    analysis::Result lastResult;
    bool haveKey = false, dragOver = false;
    juce::MidiKeyboardComponent keys;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAtt, extAtt, borrowAtt, borrowFromAtt, soundAtt, reverbTypeAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> smartAtt;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModeWheelEditor)
};
