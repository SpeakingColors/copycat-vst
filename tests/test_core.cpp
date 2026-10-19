// Core tests for Copycat. Pure C++, no JUCE. The test sounds are built here with additive synthesis.
#include "../src/core/CopycatAnalysis.h"
#include <cstdio>
#include <random>
using namespace copycat;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; std::printf("FAIL %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)
static const double sr = 48000;

struct Synth { Wave wave = Wave::Saw; double f = 220; int voices = 1; double detuneC = 0; double cutoff = 0; int poles = 4;
               double A = 0.01, D = 0.3, S = 0.6, R = 0.3, gate = 1.0, len = 2.0; };
static std::vector<float> synth(const Synth& s) {
    const size_t n = (size_t)(s.len * sr); std::vector<float> x(n, 0.f);
    for (int v = 0; v < s.voices; ++v) {
        const double det = s.voices > 1 ? ((double) v / (s.voices - 1) * 2 - 1) * s.detuneC : 0, f = s.f * std::pow(2.0, det / 1200.0);
        for (int h = 1; f * h < 20000; ++h) {
            double amp = 0; switch (s.wave) { case Wave::Saw: amp = 1.0 / h; break; case Wave::Square: amp = (h % 2) ? 1.0 / h : 0; break; case Wave::Triangle: amp = (h % 2) ? 1.0 / (h * h) * ((h % 4 == 1) ? 1 : -1) : 0; break; default: amp = h == 1 ? 1 : 0; }
            if (amp == 0) continue; const double ph0 = 0.37 * v + 0.11 * h;
            for (size_t i = 0; i < n; ++i) x[i] += (float)(amp * std::sin(2 * kPi * f * h * (double) i / sr + ph0));
        }
    }
    if (s.cutoff > 0) { const double k = 1 - std::exp(-2 * kPi * s.cutoff / sr); std::vector<double> z((size_t) s.poles, 0.0); for (size_t i = 0; i < n; ++i) { double v = x[i]; for (auto& zz : z) { zz += k * (v - zz); v = zz; } x[i] = (float) v; } }
    float pk = 0; for (float v : x) pk = std::max(pk, std::fabs(v)); for (auto& v : x) v /= pk * 1.25f;
    for (size_t i = 0; i < n; ++i) {         // ADSR
        const double t = (double) i / sr; double e;
        if (t < s.gate) e = t < s.A ? t / s.A : (t < s.A + s.D ? 1 - (1 - s.S) * (t - s.A) / s.D : s.S);
        else { const double t0 = s.gate, e0 = (t0 < s.A ? t0 / s.A : (t0 < s.A + s.D ? 1 - (1 - s.S) * (t0 - s.A) / s.D : s.S)); e = e0 * std::max(0.0, 1 - (t - t0) / s.R); }
        x[i] *= (float) e;
    }
    return x;
}

static void testF0() {
    for (double f : { 82.4, 110.0, 261.6, 440.0, 659.3 }) {
        Synth s; s.f = f; s.len = 1.5; auto x = synth(s);
        const double got = findF0(x, 12000, sr);
        CHECK(std::fabs(1200 * std::log2(got / f)) < 8, "f0 %.1f Hz found as %.2f Hz", f, got);
    }
}
static void testFullNote() {
    Synth s; s.f = 220; s.wave = Wave::Saw; s.cutoff = 3000; s.A = 0.05; s.D = 0.4; s.S = 0.5; s.R = 0.5; s.gate = 1.0; s.len = 2.0;
    auto x = synth(s); Result R; CHECK(analyse(x, sr, R), "analyse runs");
    std::printf("note %s %+dc  adsr A %.3f D %.3f S %.2f R %.3f  wave %s  cut %.0f slope %d  voices %d\n", R.noteName_.c_str(), R.cents, R.env.attack, R.env.decay, R.env.sustain, R.env.release, waveName(R.wave), R.cutoffHz, R.slope, R.voices);
    CHECK(R.midi == 57, "note A3 (midi 57), got %d (%s)", R.midi, R.noteName_.c_str());
    CHECK(std::abs(R.cents) < 10, "tuning within 10 cents (%d)", R.cents);
    CHECK(R.wave == Wave::Saw, "saw is recognised, got %s", waveName(R.wave));
    CHECK(std::fabs(R.env.attack - 0.05) < 0.04, "attack %.3f s, wanted about 0.05", R.env.attack);
    CHECK(std::fabs(R.env.sustain - 0.5) < 0.15, "sustain %.2f, wanted about 0.5", R.env.sustain);
    CHECK(R.env.release > 0.1 && R.env.release < 1.5, "release %.2f s is in a believable range for 0.5 s", R.env.release);
    CHECK(R.voices == 1, "single voice, got %d", R.voices);
    // four cascaded one-pole filters at 3 kHz have their combined -3 dB point near 1.3 kHz
    CHECK(R.cutoffHz > 650 && R.cutoffHz < 2600, "cutoff %.0f Hz is within a factor of 2 of the filter's 1.3 kHz -3 dB point", R.cutoffHz);
}
static void testWaves() {
    for (Wave w : { Wave::Saw, Wave::Square, Wave::Triangle, Wave::Sine }) {
        Synth s; s.wave = w; s.f = 196; s.A = 0.01; s.D = 0.1; s.S = 0.9; s.R = 0.2; auto x = synth(s); Result R; analyse(x, sr, R);
        if (w == Wave::Sine) { std::printf("sine harmonics:"); for (double h : R.harmonicsDb) std::printf(" %.0f", h); std::printf("\n"); }
        CHECK(R.wave == w, "wave %s recognised as %s", waveName(w), waveName(R.wave));
    }
}
static void testVoices() {
    for (double dc : { 8.0, 18.0, 30.0 }) {
        Synth s; s.f = 220; s.voices = 7; s.detuneC = dc; s.wave = Wave::Saw; s.cutoff = 5000; s.A = 0.01; s.D = 0.2; s.S = 0.8; s.R = 0.3; auto x = synth(s); Result R; analyse(x, sr, R);
        std::printf("7 voices, detune %.0f c: found %d voices, %.1f c\n", dc, R.voices, R.detuneCents);
        CHECK(R.voices >= 3, "a detuned saw (%.0f c) is not seen as one voice (%d)", dc, R.voices);
        CHECK(std::fabs(R.detuneCents - dc) < 0.5 * dc + 3, "detune %.0f c reported as %.1f c", dc, R.detuneCents);
    }
}
static void testKeyChord() {
    // C major triad as three sustained saws, then a progression for the key
    auto mix = [](std::initializer_list<double> fs, double secs) { std::vector<float> m((size_t)(secs * sr), 0.f); for (double f : fs) { Synth s; s.f = f; s.len = secs; s.gate = secs; s.R = 0.01; s.A = 0.01; s.D = 0.01; s.S = 1; s.cutoff = 4000; auto x = synth(s); for (size_t i = 0; i < m.size(); ++i) m[i] += x[i] * 0.4f; } return m; };
    auto chordC = mix({ 261.63, 329.63, 392.0 }, 2.0);
    Chord c; bestChord(chromaOf(chordC, 0, chordC.size(), sr), c); CHECK(c.name == "C", "chord C major found as %s", c.name.c_str());
    auto chordAm = mix({ 220.0, 261.63, 329.63 }, 2.0); Chord a; bestChord(chromaOf(chordAm, 0, chordAm.size(), sr), a); CHECK(a.name == "Am", "chord A minor found as %s", a.name.c_str());
    std::vector<float> prog; for (auto& part : { chordC, mix({ 349.23, 440.0, 523.25 }, 2.0), mix({ 392.0, 493.88, 587.33 }, 2.0), chordC }) prog.insert(prog.end(), part.begin(), part.end());
    std::string key; double st; bestKey(chromaOf(prog, 0, prog.size(), sr), key, st); CHECK(key == "C major", "I IV V I in C is %s", key.c_str());
}
static void testTempo() {
    for (double bpm : { 90.0, 120.0, 150.0 }) {
        std::vector<float> x((size_t)(8 * sr), 0.f); std::mt19937 rng(7); std::uniform_real_distribution<float> u(-1.f, 1.f); const double per = 60.0 / bpm;
        for (double t = 0.2; t < 7.5; t += per) for (size_t i = 0; i < 6000; ++i) x[(size_t)(t * sr) + i] += 0.8f * std::exp(-(float) i / 1200.f) * (float) std::sin(2 * kPi * 120 * (double) i / sr) + 0.1f * u(rng) * std::exp(-(float) i / 800.f);
        const size_t win = 240; auto env = rmsEnv(x, 480, win); int on = 0; const double got = findTempo(env, (double) win / sr, &on);
        CHECK(std::fabs(got - bpm) < 2.5 || std::fabs(got - bpm * 2) < 4 || std::fabs(got * 2 - bpm) < 4, "tempo %.0f found as %.1f (%d hits)", bpm, got, on);
    }
    std::vector<float> flat(48000, 0.1f); for (size_t i = 0; i < flat.size(); ++i) flat[i] = 0.1f * (float) std::sin(0.05 * (double) i);
    CHECK(findTempo(rmsEnv(flat, 480, 240), 240.0 / sr) == 0.0, "no hits means no tempo");
}
static void testEq() {
    // pink-ish tilt plus a 2 kHz bump: the biggest suggestion should be a cut near 2 kHz
    const double bh = sr / 4096; std::vector<float> db(2048); for (size_t i = 1; i < db.size(); ++i) { const double f = (double) i * bh; db[i] = (float)(-3.0 * std::log2(f / 100.0) + 9.0 * std::exp(-std::pow(std::log2(f / 2000.0) / 0.35, 2))); }
    auto eq = suggestEq(db, bh); CHECK(!eq.empty(), "eq suggestions exist");
    if (!eq.empty()) CHECK(std::fabs(std::log2(eq[0].freq / 2000.0)) < 0.35 && eq[0].gainDb < -2, "first suggestion is a cut near 2 kHz (got %.0f Hz %+.1f dB)", eq[0].freq, eq[0].gainDb);
}
static void testEdgeCases() {
    Result R; std::vector<float> tiny(100, 0.f); CHECK(!analyse(tiny, sr, R), "too short is refused");
    std::vector<float> silent((size_t) sr, 0.f); analyse(silent, sr, R);       // must not crash or hang
    std::mt19937 rng(1); std::uniform_real_distribution<float> u(-1.f, 1.f); std::vector<float> noise((size_t)(sr * 1.5)); for (auto& v : noise) v = 0.3f * u(rng);
    Result N; analyse(noise, sr, N); CHECK(std::isfinite(N.f0), "noise gives a finite answer");
}
int main() {
    testF0(); testFullNote(); testWaves(); testVoices(); testKeyChord(); testTempo(); testEq(); testEdgeCases();
    if (fails) { std::printf("%d FAILED\n", fails); return 1; }
    std::printf("all tests passed\n"); return 0;
}
