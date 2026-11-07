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

// Small pill you can drag into the DAW: starts an external drag of the recorded MIDI file
class DragChip : public juce::Component
{
public:
    DragChip() { setMouseCursor (juce::MouseCursor::DraggingHandCursor); }
    std::function<juce::File()> makeFile;
    std::function<bool()> isReady;

    void paint (juce::Graphics& g) override
    {
        const bool en = isReady && isReady();
        g.setColour (en ? juce::Colour (0xffb48cff) : juce::Colour (0xff3a3360));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), (float) getHeight() * 0.5f);
        g.setColour (en ? juce::Colours::black : juce::Colour (0xff8a84a8));
        g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        g.drawText ("Drag MIDI", getLocalBounds(), juce::Justification::centred);
    }
    void mouseDown (const juce::MouseEvent&) override { dragging = false; }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging || e.getDistanceFromDragStart() < 6 || ! (isReady && isReady())) return;
        dragging = true;
        const auto f = makeFile ? makeFile() : juce::File();
        if (f.existsAsFile())
            juce::DragAndDropContainer::performExternalDragDropOfFiles (juce::StringArray (f.getFullPathName()), false, this,
                                                                       [this] { dragging = false; });
    }
private:
    bool dragging = false;
};

// Separate window that shows the user interface of the hosted instrument
class HostedWindow : public juce::DocumentWindow
{
public:
    HostedWindow (const juce::String& title, juce::AudioProcessorEditor* editor, std::function<void()> onClose)
        : juce::DocumentWindow (title, juce::Colour (0xff1d1730), juce::DocumentWindow::closeButton),
          closeCb (std::move (onClose))
    {
        setUsingNativeTitleBar (true);
        setContentOwned (editor, true);
        setResizable (editor->isResizable(), false);
        centreWithSize (getWidth(), getHeight());
        setAlwaysOnTop (true);
        setVisible (true);
    }
    void closeButtonPressed() override { if (closeCb) closeCb(); }
private:
    std::function<void()> closeCb;
};

class ModeWheelEditor : public juce::AudioProcessorEditor,
                        public juce::FileDragAndDropTarget,
                        private juce::Timer
{
public:
    explicit ModeWheelEditor (ModeWheelProcessor&);
    ~ModeWheelEditor() override { stopTimer(); hostedWindow.reset(); }
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
    void updateRecorderUi();
    void showInstrumentMenu();
    void loadInstrument (const juce::File&);
    void toggleInstrumentUi();
    void removeInstrument();
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

    // MIDI recorder + hosted instrument row
    juce::TextButton recBtn { "Record" }, saveBtn { "Save .mid" }, instrBtn { "Instrument..." },
                     instrUiBtn { "UI" }, instrOffBtn { "X" };
    DragChip dragChip;
    std::unique_ptr<HostedWindow> hostedWindow;
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
