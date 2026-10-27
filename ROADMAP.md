# Roadmap: Copycat

Sound analyser.

## Done
- [x] Analysis engine in pure C++ with unit tests (pitch, key, chord, tempo, envelope, oscillator, voices, filter, EQ ideas)
- [x] Release packaging: installers (Windows, macOS, Linux Flatpak extension), uninstallers and PDF manual built by the release workflow (test builds, not tested on real machines)
- [x] JUCE plugin shell with Capture button and text readout, CI (core tests, configure, build)

## Next
- [ ] Try the analysis on real recordings and tighten it (cutoff and detune are rough)
- [ ] The Rebuild synth in C++ (the mockup has one in JavaScript) so the plugin can play the copy
- [x] cat-ui editor from the mockup (Sound, Synth guess, Envelope and filter, EQ curve and Recipes views) drawn through `catui::RenderGate`; Rebuild, presets and settings still to do
- [ ] Fix the analysis gaps seen on the synthetic test sound: filter cutoff (4x too low), tempo from clear hits, EQ ideas
- [ ] Presets and state backup folders

## Every cat plugin
- Draw through `catui::RenderGate` so meters and animations stop when the window is not in front (audio, automation and MIDI keep running)
- Settings page with MIDI filters on by default, selectable preset/backup folders, UI size reset to 100%
