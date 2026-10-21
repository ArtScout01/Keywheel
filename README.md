# Mode Wheel (VST3 + Standalone)

Music-theory colour wheel (circle of fifths + scale degrees) that plays chords, outputs MIDI, has a built-in synth,
and a **Key / BPM detector**: drop an audio file onto the plugin window (or click "Open audio file...").

## Key / BPM detector
- Formats: WAV, AIFF, FLAC, MP3, OGG (M4A/AAC where the OS can decode it).
- Analyses up to 150 s from the middle of the file, in a background thread.
- Shows the most likely key plus the runner-up (often the relative major/minor) and the BPM plus the half/double alternative.
- "Set wheel to key" puts the wheel on the detected key.
- If your DAW blocks file drops into plug-in windows, use the "Open audio file..." button.

## Build
See BUILD.md. Short version: push the folder to GitHub, the Action builds Windows + macOS VST3s.
