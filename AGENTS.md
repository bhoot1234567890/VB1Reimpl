# Memory

## Project Overview
JUCE-based reimplementation of the VB-1 (Virtual Bassist) VST plugin. C++17, CMake build system. Reverse-engineered from the original x86_64 VST2 binary. Produces VST3 + AU formats for macOS (arm64, optionally x86_64 universal).

Key files:
- `Source/VB1DSP.h` — Waveguide DSP: Karplus-Strong excitation, 8-voice SynthesiserVoice, 16 presets
- `Source/PluginProcessor.h/.cpp` — AudioProcessor + APVTS (6 params) + 8-voice Synthesiser
- `Source/PluginEditor.h/.cpp` — 6-knob GUI + program combo
- `CMakeLists.txt` — juce_add_plugin (VST3+AU, synth, MIDI-in) + headless renderer app

## Build
Requires JUCE 7.0.5 checkout. No npm/node involved.
```bash
git clone --depth 1 --branch 7.0.5 https://github.com/juce-framework/JUCE
cmake -B build -DJUCE_DIR=$PWD/JUCE -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
```

## Architecture Notes
- DSP is waveguide physical modeling (Karplus-Strong variants)
- 6 params: Pick, PickUp, Damper, Shape, Release, Volume
- Shape (param 4) is unsolved — not in linear render loop or note-on seeder; likely an output waveshaper or excitation table selector
- **[TUNE]** markers in code indicate coefficients needing Phase 4 A/B fitting (not architecture changes)
- Original is VST2 x86_64 only — requires Rosetta host for A/B comparison
- 8-voice polyphony with voice stealing

## Common Workflows
- **Build plugin:** `cmake --build build --config Release -j`
- **A/B diff testing:** `python tools/ab_diff.py original.wav reimpl.wav --plot diff.png`
- **Generate test MIDI:** `python tools/generate_test_midi.py`
- **Package/install:** `tools/package.sh` (codesigns + installs to ~/Library/Audio/Plug-Ins)
- **Headless render (Phase 4):** `./build/VB1Render` renders reimpl DSP to WAV for comparison
