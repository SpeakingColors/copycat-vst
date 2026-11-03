// Copycat Rebuild synth (pure C++, no JUCE): plays the copy from what the analyser found.
// One note: 1 to 7 detuned PolyBLEP voices, one oscillator shape, a 12 or 24 dB/oct low pass whose -3 dB point is the analysed cutoff, and an ADSR.
#pragma once
#include "CopycatAnalysis.h"

namespace copycat {

struct RebuildParams {
    Wave wave = Wave::Saw; double hz = 220; int voices = 1; double detuneCents = 0; double cutoffHz = 16000; int slope = 24; Env env; double gateSec = 0.8;
    static RebuildParams from(const Result& r) {
        RebuildParams p; p.wave = r.wave; p.hz = r.f0 > 0 ? r.f0 : 220; p.voices = std::max(1, std::min(7, r.voices)); p.detuneCents = r.detuneCents;
        p.cutoffHz = r.cutoffHz; p.slope = r.slope; p.env = r.env; p.gateSec = r.gateSec > 0.05 ? r.gateSec : r.env.attack + r.env.decay + 0.4; return p;
    }
};

class RebuildVoice {
public:
    void prepare(double sampleRate) { sr = sampleRate; }
    void setParams(const RebuildParams& p) { P = p; }
    void noteOn(double hz = 0) {
        const double base = hz > 0 ? hz : P.hz; nv = std::max(1, std::min(7, P.voices));
        for (int v = 0; v < nv; ++v) {
            const double det = nv > 1 ? ((double) v / (nv - 1) * 2 - 1) * P.detuneCents : 0; inc[v] = base * std::pow(2.0, det / 1200.0) / sr;
            ph[v] = std::fmod(0.37 * (double) v, 1.0); tri[v] = 0;
        }
        const int poles = P.slope >= 24 ? 4 : 2;       // n equal one pole stages have their -3 dB point at fp * sqrt(2^(1/n) - 1)
        npole = poles; const double fp = P.cutoffHz / std::sqrt(std::pow(2.0, 1.0 / poles) - 1.0);
        kf = 1.0 - std::exp(-2.0 * kPi * std::min(fp, 0.45 * sr) / sr); for (auto& z : zf) z = 0;
        open = P.cutoffHz >= 15000; stage = 1; level = 0; t = 0; relStep = 0;
    }
    void noteOff() { if (stage == 4 || stage == 0) return; stage = 4; relStep = level / std::max(1.0, P.env.release * sr); }
    bool active() const { return stage != 0; }
    float process() {
        if (!stage) return 0.f; double x = 0;
        for (int v = 0; v < nv; ++v) { x += osc(v); ph[v] += inc[v]; if (ph[v] >= 1.0) ph[v] -= 1.0; }
        x *= 0.5 / std::sqrt((double) nv);
        if (!open) { double y = x; for (int i = 0; i < npole; ++i) { zf[i] += kf * (y - zf[i]); y = zf[i]; } x = y; }
        x *= envStep(); return (float) x;
    }
private:
    double sr = 44100; RebuildParams P; int nv = 1, npole = 4, stage = 0; double inc[7] {}, ph[7] {}, tri[7] {}, zf[4] {}, kf = 0.1, level = 0, t = 0, relStep = 0; bool open = false;
    static double blep(double t, double dt) { if (t < dt) { t /= dt; return t + t - t * t - 1.0; } if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; } return 0.0; }
    double osc(int v) {
        const double p = ph[v], d = inc[v];
        switch (P.wave) {
            case Wave::Saw: return (2.0 * p - 1.0) - blep(p, d);
            case Wave::Square: { double y = p < 0.5 ? 1.0 : -1.0; y += blep(p, d); y -= blep(std::fmod(p + 0.5, 1.0), d); return y; }
            case Wave::Triangle: { double y = p < 0.5 ? 1.0 : -1.0; y += blep(p, d); y -= blep(std::fmod(p + 0.5, 1.0), d); tri[v] += 4.0 * d * y; tri[v] -= 0.0005 * tri[v]; return tri[v]; }
            default: return std::sin(2.0 * kPi * p);
        }
    }
    double envStep() {
        const double a = std::max(0.001, P.env.attack), dc = std::max(0.001, P.env.decay), s = P.env.sustain;
        if (stage == 1) { level += 1.0 / (a * sr); if (level >= 1.0) { level = 1.0; stage = 2; } }
        else if (stage == 2) { level -= (1.0 - s) / (dc * sr); if (level <= s) { level = s; stage = 3; } }
        else if (stage == 4) { level -= relStep; if (level <= 0.0) { level = 0; stage = 0; } }
        return level;
    }
};

// Renders the copy as one note: held for gateSec, then released. Peak is set to `peak` (so it can be matched to the original).
inline std::vector<float> renderCopy(const RebuildParams& p, double sr, double peak = 0.5, double maxSeconds = 4.0) {
    RebuildVoice v; v.prepare(sr); v.setParams(p); v.noteOn();
    const size_t gate = (size_t) (p.gateSec * sr), total = std::min((size_t) (maxSeconds * sr), gate + (size_t) ((p.env.release + 0.1) * sr));
    std::vector<float> y(total); float mx = 0;
    for (size_t i = 0; i < total; ++i) { if (i == gate) v.noteOff(); y[i] = v.process(); mx = std::max(mx, std::fabs(y[i])); }
    if (mx > 0) for (auto& s : y) s *= (float) (peak / mx);
    return y;
}

// How alike two sounds are: correlation of their long term spectra in dB (1 = same shape), from the analyser's own spectrum.
inline double spectrumMatch(const Result& a, const Result& b) {
    const size_t n = std::min(a.longTerm.size(), b.longTerm.size()); if (n < 8 || a.longTermBinHz <= 0 || std::fabs(a.longTermBinHz - b.longTermBinHz) > 1e-6) return 0;
    const size_t hi = std::min(n, (size_t) (12000.0 / a.longTermBinHz)); if (hi < 8) return 0;
    std::vector<double> x(hi), y(hi); for (size_t i = 0; i < hi; ++i) { x[i] = a.longTerm[i]; y[i] = b.longTerm[i]; } return corr(x, y);
}

}
