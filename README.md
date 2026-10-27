# Copycat 🐈

<img src="assets/icon.svg" width="96" align="right" alt="icon">

Sound analyser (VST3 and standalone): play a sound through it and it tells you the note, key, chord, tempo, oscillator shape, voices and detune, filter, envelope, and some EQ ideas, so you can rebuild the sound in your synth.

<p align="center"><img src="docs/screenshot.png" alt="Copycat editor" width="900"><br><sub>Screenshot of the real plugin window (Linux standalone build, analysing a synthetic 7 voice saw test sound).</sub></p>

**Manual:** [PDF, opens in the browser](https://github.com/SpeakingColors/copycat-vst/blob/main/docs/copycat-vst-manual.pdf)

## Status
Early. What exists today:
- A tested pure C++ analysis engine (`src/core`), ported from the [mockup](docs/mockup/): pitch, key, chord, tempo, ADSR, long term spectrum, filter cutoff and slope, oscillator guess, voice count and detune, EQ suggestions. The tests build their own synth sounds and check the answers.
- The real window (`src/copycat/CopycatEditor.cpp`), built from the mockup: capture bar (Listen level, Capture 2 s, Auto capture, Load file), five views (Sound spectrogram with hover readout, Synth guess, Envelope and filter, EQ curve, Recipes), estimates panel, level meter, wood cheeks, resizable with fixed aspect (double click a cheek for 100 %). Audio passes through unchanged.
- Not yet: the Rebuild synth that plays the copy (the Original and Copy buttons and the spectrum match are disabled), presets, settings page. Checked only on a synthetic test sound on Linux: key, root note, oscillator and voice count read right, but the filter cutoff came out about 4 times too low, the tempo was not found from four clean hits, and EQ ideas look odd. Nothing here has been tried on real recordings or in a host yet.

Run the engine tests: `sh tests/run.sh` (needs only g++).

<p align="center">
  <a href="https://ko-fi.com/studio87"><img src="https://img.shields.io/badge/Support_Studio_87-on_Ko--fi-FF5E5B?style=for-the-badge&logo=kofi&logoColor=white" alt="Support Studio 87 on Ko-fi"></a>
</p>

☕ Free and open source, made by one person. If it helps your music, a coffee on [Ko-fi](https://ko-fi.com/studio87) keeps it coming. 💜

## Docs
- [Interactive mockups](docs/README.md)
- [Roadmap](ROADMAP.md)

## License
AGPL-3.0-or-later, see [LICENSE](LICENSE).

Not affiliated with or endorsed by any hardware maker named here. All trademarks belong to their owners.
