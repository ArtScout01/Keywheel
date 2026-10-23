#include "PluginEditor.h"

static void setChoice (juce::AudioProcessorValueTreeState& s, const char* id, int index)
{
    if (auto* p = s.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 ((float) index));
}

static juce::Colour segColour (int i, float alpha = 1.0f)
{
    return juce::Colour::fromHSV (std::fmod ((float) (i * 30 + 10), 360.0f) / 360.0f, 0.55f, (i % 2) ? 0.92f : 0.98f, alpha);
}

//==============================================================================
void WheelComponent::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float R = juce::jmin (b.getWidth(), b.getHeight()) / 2.0f - 4.0f;
    const auto c = b.getCentre();
    const int t = tonic(), m = mode(), pc0 = t, tonicPos = (t * 7) % 12;
    const auto iv = theory::intervals (m);

    int dia[12];
    for (int& d : dia) d = -1;
    for (int d = 0; d < 7; ++d) dia[(((pc0 + iv[(size_t) d]) % 12) * 7) % 12] = d;

    auto at = [&] (float r, int i) { const float a = juce::degreesToRadians ((float) i * 30.0f);
                                      return juce::Point<float> (c.x + r * std::sin (a), c.y - r * std::cos (a)); };
    auto ring = [&] (float r1, float r2, int i)
    {
        juce::Path p;
        p.addPieSegment (c.x - r2, c.y - r2, r2 * 2, r2 * 2,
                         juce::degreesToRadians ((float) i * 30.0f - 15.0f),
                         juce::degreesToRadians ((float) i * 30.0f + 15.0f), r1 / r2);
        return p;
    };

    for (int i = 0; i < 12; ++i)
    {
        const int pc = (i * 7) % 12;
        const bool on = dia[i] >= 0, isHeld = (i == heldIdx);

        auto outer = ring (R * 0.72f, R, i);
        g.setColour (segColour (i, on ? 1.0f : 0.25f));
        g.fillPath (outer);
        g.setColour (i == tonicPos ? juce::Colours::white : juce::Colour (0xff120e1f));
        g.strokePath (outer, juce::PathStrokeType (i == tonicPos ? 4.0f : 2.0f));
        g.setColour (juce::Colour (0xff141020).withAlpha (on ? 1.0f : 0.55f));
        g.setFont (juce::FontOptions (R * 0.1f, juce::Font::bold));
        g.drawText (theory::noteName (pc, pc0, m), juce::Rectangle<float> (60, 28).withCentre (at (R * 0.86f, i)), juce::Justification::centred);

        auto inner = ring (R * 0.4f, R * 0.69f, i);
        g.setColour (on ? segColour (i, 0.95f) : juce::Colour (0xff1d1730));
        g.fillPath (inner);
        g.setColour (isHeld ? juce::Colours::white : juce::Colour (0xff120e1f));
        g.strokePath (inner, juce::PathStrokeType (isHeld ? 4.0f : 2.0f));
        if (on)
        {
            const auto ch = theory::chord (pc0, m, dia[i], ext());
            g.setColour (juce::Colour (0xff141020));
            g.setFont (juce::FontOptions (R * 0.1f, juce::Font::bold));
            g.drawText (ch.degLabel, juce::Rectangle<float> (60, 24).withCentre (at (R * 0.55f, i) + juce::Point<float> (0, -8)), juce::Justification::centred);
            g.setFont (juce::FontOptions (R * 0.06f, juce::Font::plain));
            g.drawText (ch.roman, juce::Rectangle<float> (76, 20).withCentre (at (R * 0.55f, i) + juce::Point<float> (0, 10)), juce::Justification::centred);
        }
    }

    g.setColour (juce::Colour (0xff1d1730));
    g.fillEllipse (c.x - R * 0.36f, c.y - R * 0.36f, R * 0.72f, R * 0.72f);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (R * 0.2f, juce::Font::bold));
    g.drawText (theory::noteName (pc0, pc0, m), juce::Rectangle<float> (120, 40).withCentre (c + juce::Point<float> (0, -10)), juce::Justification::centred);
    g.setColour (juce::Colour (0xffa69fc0));
    g.setFont (juce::FontOptions (R * 0.07f, juce::Font::plain));
    g.drawText (juce::String (theory::modeNames[m]).toLowerCase(), juce::Rectangle<float> (120, 24).withCentre (c + juce::Point<float> (0, 22)), juce::Justification::centred);
}

int WheelComponent::hitSegment (juce::Point<float> p, bool& outerRing) const
{
    const auto b = getLocalBounds().toFloat();
    const float R = juce::jmin (b.getWidth(), b.getHeight()) / 2.0f - 4.0f;
    const auto d = p - b.getCentre();
    const float dist = d.getDistanceFromOrigin();
    if (dist > R || dist < R * 0.4f) return -1;
    outerRing = dist >= R * 0.7f;
    float a = std::atan2 (d.x, -d.y);                       // 0 = top, clockwise
    int idx = (int) std::lround (a / juce::degreesToRadians (30.0f));
    return ((idx % 12) + 12) % 12;
}

void WheelComponent::mouseDown (const juce::MouseEvent& e)
{
    bool outerRing = false;
    const int idx = hitSegment (e.position, outerRing);
    if (idx < 0) return;
    const int pcAt = (idx * 7) % 12;

    if (outerRing) { setChoice (proc.apvts, "tonic", pcAt); repaint(); return; }

    const int pc0 = tonic(), m = mode();
    const auto iv = theory::intervals (m);
    for (int d = 0; d < 7; ++d)
        if ((pc0 + iv[(size_t) d]) % 12 == pcAt)
        {
            const auto ch = theory::chord (pc0, m, d, ext());
            held.clear();
            juce::StringArray names;
            for (int x : ch.ints)
            {
                const int nt = 48 + ch.rootPc + x;
                held.push_back (nt);
                proc.keyState.noteOn (1, nt, 0.8f);
                names.add (theory::noteName (nt % 12, pc0, m));
            }
            heldIdx = idx;
            if (onChord) onChord (ch.roman + "  " + ch.name + "   =   " + names.joinIntoString ("  "));
            repaint();
            return;
        }
}

void WheelComponent::mouseUp (const juce::MouseEvent&)
{
    for (int nt : held) proc.keyState.noteOff (1, nt, 0.0f);
    held.clear(); heldIdx = -1; repaint();
}

//==============================================================================
ModeWheelEditor::ModeWheelEditor (ModeWheelProcessor& p)
    : AudioProcessorEditor (&p), proc (p), wheel (p),
      keys (p.keyState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    setSize (560, 900);
    setResizable (true, true);
    setResizeLimits (560, 760, 560, 1200);

    for (int i = 0; i < 6; ++i) modeBox.addItem (theory::modeNames[i], i + 1);
    modeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, "mode", modeBox);
    addAndMakeVisible (modeBox);

    // sound + reverb
    for (int i = 0; i < snd::numSounds; ++i) soundBox.addItem (snd::names[i], i + 1);
    soundAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, "sound", soundBox);
    reverbTypeBox.addItem ("Reverb: Hall", 1);
    reverbTypeBox.addItem ("Reverb: Spring", 2);
    reverbTypeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, "reverbType", reverbTypeBox);
    addAndMakeVisible (soundBox); addAndMakeVisible (reverbTypeBox);

    setupKnob (knobs[0], "Fuzz",    "fuzz");
    setupKnob (knobs[1], "Vibrato", "vibDepth");
    setupKnob (knobs[2], "V.Rate",  "vibRate");
    setupKnob (knobs[3], "Phaser",  "phaser");
    setupKnob (knobs[4], "P.Rate",  "phaserRate");
    setupKnob (knobs[5], "Echo",    "echoMix");
    setupKnob (knobs[6], "E.Time",  "echoTime");
    setupKnob (knobs[7], "E.Fdbk",  "echoFb");
    setupKnob (knobs[8], "Reverb",  "reverb");

    // chord type + borrowed chords
    for (int i = 0; i < 3; ++i) extBox.addItem (theory::extNames[i], i + 1);
    extAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, "chordType", extBox);
    borrowBox.addItem ("Borrow: off", 1);
    borrowBox.addItem ("Borrow: out-of-key notes", 2);
    borrowBox.addItem ("Borrow: all notes", 3);
    borrowAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, "borrow", borrowBox);
    for (int i = 0; i < 6; ++i) borrowFromBox.addItem (juce::String ("from ") + theory::modeNames[i], i + 1);
    borrowFromAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, "borrowFrom", borrowFromBox);
    addAndMakeVisible (extBox); addAndMakeVisible (borrowBox); addAndMakeVisible (borrowFromBox);

    const auto& ps = theory::presets();
    for (size_t i = 0; i < ps.size(); ++i) presetBox.addItem (ps[i].name, (int) i + 1);
    presetBox.setSelectedId (1, juce::dontSendNotification);
    presetBox.onChange = [this]
    {
        const auto& pr = theory::presets()[(size_t) presetBox.getSelectedItemIndex()];
        setChoice (proc.apvts, "tonic", pr.tonicPc);
        setChoice (proc.apvts, "mode", pr.mode);
        if (proc.isProgressionRunning()) proc.startProgression (presetBox.getSelectedItemIndex());
    };
    addAndMakeVisible (presetBox);

    playBtn.onClick = [this] { proc.startProgression (presetBox.getSelectedItemIndex()); };
    stopBtn.onClick = [this] { proc.stopProgression(); };
    addAndMakeVisible (playBtn); addAndMakeVisible (stopBtn);

    smartAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, "smart", smartBtn);
    smartBtn.setColour (juce::ToggleButton::textColourId, juce::Colours::white);
    addAndMakeVisible (smartBtn);

    info.setJustificationType (juce::Justification::centred);
    info.setColour (juce::Label::textColourId, juce::Colour (0xffa69fc0));
    info.setText ("Click the outer ring to pick a key. Click the inner ring to play a chord.", juce::dontSendNotification);
    addAndMakeVisible (info);
    wheel.onChord = [this] (const juce::String& s) { info.setText (s, juce::dontSendNotification); };
    addAndMakeVisible (wheel);

    keys.setAvailableRange (48, 83);
    keys.setLowestVisibleKey (48);
    keys.setKeyWidth (560.0f / 21.0f);
    keys.setScrollButtonsVisible (false);
    addAndMakeVisible (keys);

    // ---- Key / BPM detector ----
    detectStatus.setText ("Drop an audio file here to detect key and BPM", juce::dontSendNotification);
    detectStatus.setColour (juce::Label::textColourId, juce::Colour (0xffa69fc0));
    keyLabel.setFont (juce::Font (juce::FontOptions (20.0f, juce::Font::bold)));
    bpmLabel.setFont (juce::Font (juce::FontOptions (20.0f, juce::Font::bold)));
    keyLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    bpmLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    keyLabel.setText ("Key: -", juce::dontSendNotification);
    bpmLabel.setText ("BPM: -", juce::dontSendNotification);
    applyBtn.setEnabled (false);
    applyBtn.onClick = [this] { applyKeyToWheel(); };
    openBtn.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose an audio file", juce::File(),
                                                       "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg;*.m4a;*.aac");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe = juce::Component::SafePointer<ModeWheelEditor> (this)] (const juce::FileChooser& fc)
                              {
                                  if (safe != nullptr && fc.getResult().existsAsFile())
                                      safe->analyseFile (fc.getResult());
                              });
    };
    addAndMakeVisible (detectStatus); addAndMakeVisible (keyLabel); addAndMakeVisible (bpmLabel);
    addAndMakeVisible (openBtn); addAndMakeVisible (applyBtn);

    startTimerHz (15);
}

void ModeWheelEditor::setupKnob (Knob& k, const juce::String& text, const char* id)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    k.slider.setPopupDisplayEnabled (true, true, this);
    k.slider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xffb48cff));
    k.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k.slider);
    if (auto* prm = proc.apvts.getParameter (id))
        k.slider.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
    k.label.setText (text, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::Font (juce::FontOptions (11.0f)));
    k.label.setColour (juce::Label::textColourId, juce::Colour (0xffa69fc0));
    addAndMakeVisible (k.slider);
    addAndMakeVisible (k.label);
}

bool ModeWheelEditor::isAudioFile (const juce::File& f)
{
    return f.hasFileExtension ("wav;aif;aiff;flac;mp3;ogg;m4a;aac");
}

bool ModeWheelEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files) if (isAudioFile (juce::File (f))) return true;
    return false;
}

void ModeWheelEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false; repaint();
    for (auto& f : files)
        if (isAudioFile (juce::File (f))) { analyseFile (juce::File (f)); return; }
}

void ModeWheelEditor::analyseFile (const juce::File& f)
{
    haveKey = false; applyBtn.setEnabled (false);
    detectStatus.setText ("Analysing " + f.getFileName() + " ...", juce::dontSendNotification);
    keyLabel.setText ("Key: ...", juce::dontSendNotification);
    bpmLabel.setText ("BPM: ...", juce::dontSendNotification);
    analyser.analyse (f, [safe = juce::Component::SafePointer<ModeWheelEditor> (this)] (FileAnalyser::Outcome o)
                         { if (safe != nullptr) safe->showOutcome (o); });
}

void ModeWheelEditor::showOutcome (const FileAnalyser::Outcome& o)
{
    if (! o.ok)
    {
        detectStatus.setText (o.error, juce::dontSendNotification);
        keyLabel.setText ("Key: -", juce::dontSendNotification);
        bpmLabel.setText ("BPM: -", juce::dontSendNotification);
        return;
    }
    const auto& r = o.result;
    lastResult = r;

    auto keyName = [] (int pc, bool minor)
    {
        return theory::noteName (pc, pc, minor ? 1 : 0) + (minor ? " minor" : " major");
    };

    haveKey = r.keyScore > 0.0f;
    applyBtn.setEnabled (haveKey);
    detectStatus.setText (o.fileName, juce::dontSendNotification);

    if (haveKey)
        keyLabel.setText ("Key: " + keyName (r.tonicPc, r.minor) + "    (or " + keyName (r.altTonicPc, r.altMinor) + ")"
                              + (r.keyScore < 0.6f ? "   - unsure" : ""),
                          juce::dontSendNotification);
    else
        keyLabel.setText ("Key: -", juce::dontSendNotification);

    if (r.bpm > 0.0f)
    {
        auto fmt = [] (float v) { return juce::String (v, std::abs (v - std::round (v)) < 0.05f ? 0 : 1); };
        bpmLabel.setText ("BPM: " + fmt (r.bpm)
                              + (r.bpmAlt > 0.0f ? "    (or " + fmt (r.bpmAlt) + ")" : juce::String())
                              + (r.bpmScore < 0.4f ? "   - no clear beat" : ""),
                          juce::dontSendNotification);
    }
    else
        bpmLabel.setText ("BPM: -", juce::dontSendNotification);
}

void ModeWheelEditor::applyKeyToWheel()
{
    if (! haveKey) return;
    setChoice (proc.apvts, "tonic", lastResult.tonicPc);
    setChoice (proc.apvts, "mode", lastResult.minor ? 1 : 0);
}

void ModeWheelEditor::timerCallback() { wheel.repaint(); }

void ModeWheelEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff120e1f));
    g.setColour (juce::Colour (0xff1d1730));
    g.fillRoundedRectangle (fxArea.toFloat(), 10.0f);
    g.setColour (juce::Colour (0xff4a3f78));
    g.drawRoundedRectangle (fxArea.toFloat().reduced (1.0f), 10.0f, 1.5f);
    const auto a = dropArea.toFloat();
    g.setColour (dragOver ? juce::Colour (0xff3b2f66) : juce::Colour (0xff1d1730));
    g.fillRoundedRectangle (a, 10.0f);
    g.setColour (dragOver ? juce::Colours::white : juce::Colour (0xff4a3f78));
    g.drawRoundedRectangle (a.reduced (1.0f), 10.0f, dragOver ? 2.5f : 1.5f);
}

void ModeWheelEditor::resized()
{
    auto r = getLocalBounds().reduced (10);
    auto top = r.removeFromTop (30);
    modeBox.setBounds (top.removeFromLeft (110));
    top.removeFromLeft (6);
    soundBox.setBounds (top.removeFromLeft (150));
    top.removeFromLeft (6);
    smartBtn.setBounds (top.removeFromLeft (112));
    top.removeFromLeft (6);
    reverbTypeBox.setBounds (top);
    r.removeFromTop (8);
    auto row = r.removeFromTop (30);
    playBtn.setBounds (row.removeFromRight (80)); row.removeFromRight (6);
    stopBtn.setBounds (row.removeFromRight (60)); row.removeFromRight (8);
    presetBox.setBounds (row);
    r.removeFromTop (8);
    {
        auto row3 = r.removeFromTop (30);
        const int w = (row3.getWidth() - 16) / 3;
        extBox.setBounds (row3.removeFromLeft (w)); row3.removeFromLeft (8);
        borrowBox.setBounds (row3.removeFromLeft (w + 20)); row3.removeFromLeft (8);
        borrowFromBox.setBounds (row3);
    }
    r.removeFromTop (8);
    fxArea = r.removeFromTop (88);
    {
        auto fa = fxArea.reduced (6, 6);
        const int w = fa.getWidth() / (int) knobs.size();
        for (auto& k : knobs)
        {
            auto cell = fa.removeFromLeft (w);
            k.label.setBounds (cell.removeFromBottom (16));
            k.slider.setBounds (cell);
        }
    }
    keys.setBounds (r.removeFromBottom (110).withX (0).withWidth (getWidth()));
    r.removeFromBottom (8);
    info.setBounds (r.removeFromBottom (30));
    r.removeFromBottom (8);
    dropArea = r.removeFromBottom (112);
    {
        auto da = dropArea.reduced (10, 8);
        auto btns = da.removeFromRight (160);
        openBtn.setBounds (btns.removeFromTop (30)); btns.removeFromTop (8);
        applyBtn.setBounds (btns.removeFromTop (30));
        detectStatus.setBounds (da.removeFromTop (22));
        keyLabel.setBounds (da.removeFromTop (30));
        bpmLabel.setBounds (da.removeFromTop (30));
    }
    wheel.setBounds (r.reduced (0, 6));
}
