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
- [x] Analysis gaps from the synthetic test sound fixed: the filter cutoff now follows the filter (read from the harmonics: -3 dB point within about 30 percent on one, two and four pole test filters, 12 or 24 dB slope), the tempo is read from a fixed 10 ms envelope (hits of any pitch, half-tempo corrected, about 1 percent accurate), the EQ ideas ignore the harmonic comb and the noise floor. A sound filtered below its own fundamental reads as a sine and an open filter. All of this is checked on synthetic sounds only, not on real recordings
- [x] The last capture is saved with the project (16 bit audio, analysed again on load); it has no sound settings of its own, so there are no presets to save. Still open: a Recipes export, backup folder setting

## Every cat plugin
- Draw through `catui::RenderGate` so meters and animations stop when the window is not in front (audio, automation and MIDI keep running)
- Settings page with MIDI filters on by default, selectable preset/backup folders, UI size reset to 100%
