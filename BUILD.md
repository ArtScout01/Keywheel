# Build

## Option A: GitHub Actions (empfohlen, baut Windows UND Mac)
1. Neues GitHub-Repo anlegen, den Inhalt dieses Ordners hochladen (inkl. `.github/`).
2. Tab "Actions" -> Workflow "Build plugin" laeuft automatisch.
3. Unter dem fertigen Run bei "Artifacts": `ModeWheel-VST3-Windows` und `ModeWheel-VST3-macOS` herunterladen.

## Option B: lokal
Voraussetzungen: CMake 3.22+, Git, Visual Studio 2022 (Windows) bzw. Xcode (macOS).

    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release

## Installieren
- Windows: `Mode Wheel.vst3` nach `C:\Program Files\Common Files\VST3\`
- macOS:   `Mode Wheel.vst3` nach `~/Library/Audio/Plug-Ins/VST3/`
  Falls macOS blockiert: `xattr -cr "~/Library/Audio/Plug-Ins/VST3/Mode Wheel.vst3"`

## DAW
- Ableton Live 11/12: Preferences > Plug-ins > "Use VST3 System Folders" an, dann Rescan.
- FL Studio 21+: Plugin Manager > "Find installed plugins" (VST3-Ordner muss in der Liste stehen).
