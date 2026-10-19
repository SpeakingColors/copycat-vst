// Copycat: sound analysis (pure C++, no JUCE). Port of the mockup's analyser.
// Finds the pitch, key, chord, tempo, the amplitude envelope (attack, decay, sustain, release), the long term spectrum,
// a guess at the filter cutoff and slope, the oscillator shape, the number of detuned voices, and a few EQ suggestions.
#pragma once
#include <vector>
#include <complex>
#include <cmath>
#include <algorithm>
#include <string>
#include <array>
#include <cstdio>

namespace copycat {

constexpr double kPi = 3.14159265358979323846;
inline const char* noteName(int pc) { static const char* n[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; return n[((pc % 12) + 12) % 12]; }
inline double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

inline void fftInPlace(std::vector<std::complex<double>>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap(a[i], a[j]); }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2 * kPi / (double) len; const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) { std::complex<double> w(1); for (size_t k = 0; k < len / 2; ++k) { auto u = a[i + k], v = a[i + k + len / 2] * w; a[i + k] = u + v; a[i + k + len / 2] = u - v; w *= wl; } }
    }
}
inline std::vector<double> hann(size_t n) { std::vector<double> w(n); for (size_t i = 0; i < n; ++i) w[i] = 0.5 - 0.5 * std::cos(2 * kPi * (double) i / (double) n); return w; }
// magnitude spectrum of x[from .. from+N), zero padded to P (>= N), P/2 bins
inline std::vector<float> magSpec(const std::vector<float>& x, size_t from, size_t N, const std::vector<double>& w, size_t P = 0) {
    if (!P) P = N; std::vector<std::complex<double>> a(P);
    for (size_t i = 0; i < N; ++i) a[i] = (from + i < x.size() ? (double) x[from + i] : 0.0) * w[i];
    fftInPlace(a); std::vector<float> m(P / 2); for (size_t i = 0; i < P / 2; ++i) m[i] = (float) std::abs(a[i]); return m;
}
inline double corr(const std::vector<double>& a, const std::vector<double>& b) {
    const size_t n = a.size(); double ma = 0, mb = 0; for (size_t i = 0; i < n; ++i) { ma += a[i]; mb += b[i]; } ma /= (double) n; mb /= (double) n;
    double s = 0, sa = 0, sb = 0; for (size_t i = 0; i < n; ++i) { s += (a[i] - ma) * (b[i] - mb); sa += (a[i] - ma) * (a[i] - ma); sb += (b[i] - mb) * (b[i] - mb); }
    return s / std::sqrt(sa * sb + 1e-20);
}

struct Env { double attack = 0.01, decay = 0.3, sustain = 0.7, release = 0.3; double onset = 0, peakTime = 0; };
struct Chord { std::string name; std::vector<std::string> notes; bool single = false; };
struct EqBand { double freq, gainDb; };
enum class Wave { Saw, Square, Triangle, Sine };
inline const char* waveName(Wave w) { return w == Wave::Saw ? "saw" : w == Wave::Square ? "square" : w == Wave::Triangle ? "triangle" : "sine"; }

struct Result {
    double f0 = 0; int midi = 0; int cents = 0; std::string noteName_;
    std::string key; double keyStrength = 0; std::array<double, 12> chroma {};
    Chord chord; bool hasChord = false;
    double bpm = 0; int onsets = 0;                      // bpm 0 = not enough hits
    Env env;
    std::vector<float> longTerm; double longTermBinHz = 0;      // dB, 4096 point average
    double cutoffHz = 16000; int slope = 24;
    Wave wave = Wave::Saw; std::vector<double> harmonicsDb;
    int voices = 1; double detuneCents = 0;
    std::vector<EqBand> eq;
};

// ---- pieces (public so they can be tested alone) ----
// Fundamental by harmonic product on a zero padded 16384 point spectrum, refined by parabolic interpolation.
inline double findF0(const std::vector<float>& x, size_t from, double sr, std::vector<float>* magOut = nullptr, double* binHz = nullptr) {
    const size_t N = 16384, P = 65536; auto w = hann(N); auto m = magSpec(x, from, N, w, P); const double bh = sr / (double) P;
    const size_t lo = (size_t)(40 / bh), hi = (size_t)(1000 / bh); size_t best = lo; double bv = 0; bool any = false;
    for (size_t i = lo; i < hi; ++i) { double s = std::log(m[i] + 1e-9); for (int h = 2; h <= 5; ++h) s += std::log(m[i * (size_t) h] + 1e-9); if (!any || s > bv) { bv = s; best = i; any = true; } }
    const double a = std::log(m[best - 1] + 1e-9), b = std::log(m[best] + 1e-9), c = std::log(m[best + 1] + 1e-9), d = a - 2 * b + c, off = d != 0 ? 0.5 * (a - c) / d : 0;
    // A pure tone can make a sub-harmonic win the product; a fundamental 24 dB below one of its own harmonics is that artefact, so step up.
    for (int guard = 0; guard < 3; ++guard) {
        size_t up = 0; float upM = m[best];
        for (size_t k = 2; k <= 5; ++k) { const size_t c = best * k; if (c + 3 >= m.size()) break; float mx = 0; size_t at = c; for (size_t j = c - 2; j <= c + 2; ++j) if (m[j] > mx) { mx = m[j]; at = j; } if (mx > upM) { upM = mx; up = at; } }
        if (up && upM > 16.f * m[best]) best = up; else break;
    }
    if (magOut) *magOut = m; if (binHz) *binHz = bh;
    return ((double) best + off) * bh;
}

// Amplitude envelope in windows of `win` samples. Returns the peak-hold envelope.
inline std::vector<float> peakEnv(const std::vector<float>& x, size_t win) {
    const size_t n = x.size() / win; std::vector<float> e(n > 1 ? n - 1 : 0);
    for (size_t k = 0; k < e.size(); ++k) { float m = 0; for (size_t i = 0; i < 2 * win; ++i) m = std::max(m, std::fabs(x[k * win + i])); e[k] = m; }
    return e;
}
inline std::vector<float> rmsEnv(const std::vector<float>& x, size_t rw, size_t hop) {
    const size_t n = x.size() > rw ? (x.size() - rw) / hop : 0; std::vector<float> e(n);
    for (size_t k = 0; k < n; ++k) { double s = 0; for (size_t i = 0; i < rw; ++i) { const double v = x[k * hop + i]; s += v * v; } e[k] = (float)(std::sqrt(s / (double) rw) * 1.4142135); }
    return e;
}
// ADSR from an envelope: attack from 10 % to 90 % of the peak, decay and sustain from a two line fit in dB, release from the tail after the gate.
inline Env fitAdsr(const std::vector<float>& env, double dt, size_t* gateOut = nullptr, size_t* onOut = nullptr, size_t* pkOut = nullptr) {
    Env r; if (env.size() < 8) return r;
    size_t k, pkAll = 0; float pk = 0; for (k = 0; k < env.size(); ++k) if (env[k] > pk) { pk = env[k]; pkAll = k; } (void) pkAll;
    size_t on = 0; for (k = 0; k < env.size(); ++k) if (env[k] > 0.1f * pk) { on = k; break; }
    std::vector<double> db(env.size()); for (k = 0; k < env.size(); ++k) db[k] = 20 * std::log10(env[k] + 1e-6);
    size_t pkI = on; float pkV = 0; for (k = on; k < std::min(env.size(), on + (size_t) std::lround(0.25 / dt)); ++k) if (env[k] > pkV) { pkV = env[k]; pkI = k; }
    size_t t10 = on, t90 = pkI; for (k = on; k <= pkI; ++k) if (env[k] >= 0.1f * pkV) { t10 = k; break; } for (k = on; k <= pkI; ++k) if (env[k] >= 0.9f * pkV) { t90 = k; break; }
    const double att = std::max(0.001, (double)(t90 - t10) * dt / 0.8);
    size_t iEnd = env.size() - 1; for (k = pkI + 1; k < env.size(); ++k) if (env[k] < 0.1f * pkV) { iEnd = k; break; }
    auto fitErr = [&](size_t a0, size_t a1) { const double n = (double)(a1 - a0 + 1); if (n < 2) return 0.0; double sx = 0, sy = 0, sxx = 0, sxy = 0; for (size_t q = a0; q <= a1; ++q) { sx += (double) q; sy += db[q]; sxx += (double)(q * q); sxy += (double) q * db[q]; }
        const double den = n * sxx - sx * sx != 0 ? n * sxx - sx * sx : 1, sl = (n * sxy - sx * sy) / den, ic = (sy - sl * sx) / n; double e = 0; for (size_t q = a0; q <= a1; ++q) { const double d = db[q] - (ic + sl * (double) q); e += d * d; } return e; };
    size_t bestB = iEnd; double be = 1e18;
    if (iEnd > pkI + 8) for (size_t bb = pkI + 3; bb + 2 < iEnd; ++bb) { const double er = fitErr(pkI, bb) + fitErr(bb, iEnd); if (er < be) { be = er; bestB = bb; } }
    double sus = (double) env[bestB] / pkV, decT = dt;
    for (k = pkI; k <= bestB; ++k) if (env[k] <= env[bestB] * 1.12f) { decT = std::max(dt, (double)(k - pkI) * dt); break; }
    size_t r20 = bestB; for (k = bestB; k < env.size(); ++k) if (env[k] <= env[bestB] * 0.1f) { r20 = k; break; }
    double rel = std::max(0.02, (double)(r20 - bestB) * dt * 3); if (sus < 0.06) sus = 0;
    r.attack = att; r.decay = std::min(2.0, decT); r.sustain = clampd(sus, 0, 1); r.release = std::min(2.5, rel); r.onset = (double) on * dt; r.peakTime = (double) pkI * dt;
    if (gateOut) *gateOut = bestB; if (onOut) *onOut = on; if (pkOut) *pkOut = pkI;
    return r;
}

inline std::array<double, 12> chromaOf(const std::vector<float>& x, size_t from, size_t to, double sr) {
    std::array<double, 12> ch {}; const size_t N = 8192; auto w = hann(N); const double bh = sr / (double) N;
    for (size_t p = from; p + N <= to; p += 4096) {
        auto m = magSpec(x, p, N, w); struct Pk { double f, a; }; std::vector<Pk> pk; float mx = 0;
        const size_t lo = (size_t)(55 / bh), hi = std::min(m.size() - 2, (size_t)(3000 / bh)); for (size_t b = lo; b < hi; ++b) mx = std::max(mx, m[b]);
        for (size_t b = lo + 1; b < hi; ++b) if (m[b] > m[b - 1] && m[b] >= m[b + 1] && m[b] > 0.08f * mx) { const double den = (double)(m[b - 1] - 2 * m[b] + m[b + 1]); const double d = 0.5 * (double)(m[b - 1] - m[b + 1]) / (den != 0 ? den : 1); pk.push_back({ ((double) b + d) * bh, (double) m[b] }); }
        std::sort(pk.begin(), pk.end(), [](const Pk& a, const Pk& b) { return a.f < b.f; }); if (pk.size() > 40) pk.resize(40);
        for (size_t i = 0; i < pk.size(); ++i) {       // skip peaks that are harmonics of a lower peak
            bool isH = false; for (size_t j = 0; j < i; ++j) { const double r = pk[i].f / pk[j].f, n = std::round(r); if (n >= 2 && n <= 10 && std::fabs(r - n) < 0.025 * n && pk[j].a > 0.3 * pk[i].a) { isH = true; break; } }
            if (!isH) { const int mi = (int) std::lround(69 + 12 * std::log2(pk[i].f / 440)); ch[(size_t)(((mi % 12) + 12) % 12)] += pk[i].a * pk[i].a; }
        }
    }
    return ch;
}
inline bool bestChord(const std::array<double, 12>& ch, Chord& out) {
    static const struct { const char* n; std::vector<int> iv; } chords[] = { { "", { 0, 4, 7 } }, { "m", { 0, 3, 7 } }, { "7", { 0, 4, 7, 10 } }, { "m7", { 0, 3, 7, 10 } }, { "maj7", { 0, 4, 7, 11 } }, { "sus4", { 0, 5, 7 } } };
    double tot = 0, mxc = 0; int mi0 = 0; for (int i = 0; i < 12; ++i) { tot += ch[(size_t) i]; if (ch[(size_t) i] > mxc) { mxc = ch[(size_t) i]; mi0 = i; } }
    if (tot < 1e-9) return false;
    if (mxc / tot > 0.6) { out = { noteName(mi0), { noteName(mi0) }, true }; return true; }
    double bs = -1; std::vector<double> c(ch.begin(), ch.end());
    for (int r = 0; r < 12; ++r) for (auto& cd : chords) {
        std::vector<double> t(12, 0.0); for (int iv : cd.iv) t[(size_t)((r + iv) % 12)] = 1;
        const double s = corr(c, t) - 0.015 * (double) cd.iv.size();
        if (s > bs) { bs = s; out.name = std::string(noteName(r)) + cd.n; out.notes.clear(); for (int iv : cd.iv) out.notes.push_back(noteName(r + iv)); out.single = false; }
    }
    return true;
}
inline bool bestKey(const std::array<double, 12>& ch, std::string& name, double& strength) {
    static const double maj[] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 }, mnr[] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
    std::vector<double> c(ch.begin(), ch.end()); double tot = 0; for (double v : c) tot += v; if (tot < 1e-9) return false;
    double best = -2; for (int r = 0; r < 12; ++r) { std::vector<double> pm(12), pn(12); for (int i = 0; i < 12; ++i) { pm[(size_t) i] = maj[(i - r + 12) % 12]; pn[(size_t) i] = mnr[(i - r + 12) % 12]; }
        const double a = corr(c, pm), b = corr(c, pn); if (a > best) { best = a; name = std::string(noteName(r)) + " major"; } if (b > best) { best = b; name = std::string(noteName(r)) + " minor"; } }
    strength = best; return true;
}
// Tempo from rises in the envelope: autocorrelation of the onset curve between 70 and 190 BPM. bpm 0 if fewer than 3 hits.
inline double findTempo(const std::vector<float>& env, double dt, int* onsetsOut = nullptr) {
    if (env.size() < 4) return 0; float pk = 0; for (float v : env) pk = std::max(pk, v);
    std::vector<double> db(env.size()), onE(env.size() - 1); for (size_t k = 0; k < env.size(); ++k) db[k] = 20 * std::log10(env[k] + 1e-6);
    int nOn = 0; bool prev = false; for (size_t k = 1; k < env.size(); ++k) { const double d = db[k] - db[k - 1]; onE[k - 1] = std::max(0.0, d); const bool is = d > 6; if (is && !prev && env[k] > 0.15f * pk) ++nOn; prev = is; }
    if (onsetsOut) *onsetsOut = nOn; if (nOn < 3) return 0;
    double bestB = 0, bl = 0; for (double bpm = 70; bpm <= 190; bpm += 0.5) { const double lag = 60.0 / bpm / dt; double s = 0; int cnt = 0; for (size_t k = 0; k + (size_t) lag < onE.size(); ++k) { const size_t j = (size_t) std::llround((double) k + lag); s += onE[k] * (j < onE.size() ? onE[j] : 0.0); ++cnt; } s /= std::max(1, cnt); if (s > bl) { bl = s; bestB = bpm; } }
    return bestB;
}

inline Wave guessWave(const std::vector<double>& harmDb) {
    auto shape = [](Wave w, int h) { const double lg = 20 * std::log10((double) h); switch (w) { case Wave::Saw: return -lg; case Wave::Square: return (h % 2) ? -lg : -60.0; case Wave::Triangle: return (h % 2) ? -2 * lg : -60.0; default: return h == 1 ? 0.0 : -60.0; } };
    Wave best = Wave::Saw; double be = 1e30;
    for (Wave w : { Wave::Saw, Wave::Square, Wave::Triangle, Wave::Sine }) { double e = 0; for (int h = 1; h <= 6 && h <= (int) harmDb.size(); ++h) { const double d = clampd(harmDb[(size_t)(h - 1)], -60, 6) - clampd(shape(w, h), -60, 6); e += d * d; } if (e < be) { be = e; best = w; } }
    return best;
}

inline std::vector<float> longTermSpectrum(const std::vector<float>& x, double sr, double* binHz = nullptr) {
    const size_t NL = 4096; auto w = hann(NL); std::vector<double> acc(NL / 2, 0.0); int cnt = 0;
    for (size_t p = 0; p + NL <= x.size(); p += 1024) { auto m = magSpec(x, p, NL, w); for (size_t i = 0; i < NL / 2; ++i) acc[i] += (double) m[i] * m[i]; ++cnt; }
    std::vector<float> s(NL / 2, -120.f); if (cnt) for (size_t i = 0; i < NL / 2; ++i) s[i] = (float)(10 * std::log10(acc[i] / cnt + 1e-12));
    if (binHz) *binHz = sr / (double) NL; return s;
}
// power average in +-1/6 octave
inline std::vector<float> smoothSpec(const std::vector<float>& db, double bh) {
    std::vector<float> out(db.size(), -120.f);
    for (size_t i = 1; i < db.size(); ++i) { const double f = (double) i * bh; const size_t a = std::max<size_t>(1, (size_t)std::lround(f / std::pow(2.0, 1.0 / 6) / bh)), b = std::min(db.size() - 1, (size_t)std::lround(f * std::pow(2.0, 1.0 / 6) / bh)); double s = 0; int n = 0; for (size_t j = a; j <= b; ++j) { s += std::pow(10.0, db[j] / 10.0); ++n; } out[i] = (float)(10 * std::log10(s / std::max(1, n) + 1e-12)); }
    return out;
}
// Filter cutoff and slope: compare the smoothed spectrum with the line of an ideal saw (-6 dB per octave) from the fundamental up.
inline void estimateCutoff(const std::vector<float>& smoothDb, double bh, double f0, double& cut, int& slope) {
    auto ref = [f0](double f) { return -6.0 * std::log2(f / f0); }; cut = 16000; slope = 24; double lvl = 0; int n = 0;
    for (int h = 1; h <= 3; ++h) { const size_t b = (size_t) std::lround(f0 * h / bh); if (b < smoothDb.size()) { lvl += smoothDb[b] - ref(f0 * h); ++n; } }
    if (!n) return; lvl /= n; double found = 16000;
    for (double f = f0 * 3; f < 18000; f *= 1.04) { const size_t b = (size_t) std::lround(f / bh); if (b >= smoothDb.size()) break; if (smoothDb[b] - (lvl + ref(f)) < -3) { found = f; break; } }
    auto at = [&](double f) { const size_t b = std::min(smoothDb.size() - 1, (size_t) std::lround(f / bh)); return (double) smoothDb[b] - (lvl + ref(f)); };
    slope = (at(found) - at(found * 2)) > 18 ? 24 : 12; cut = clampd(found * 1.25, 200, 16000);
}
// Up to 4 EQ bands that would move the sound's spectrum toward a straight tilt line (80 Hz to 12 kHz).
inline std::vector<EqBand> suggestEq(const std::vector<float>& smoothDb, double bh) {
    struct P { double l2, y, f; }; std::vector<P> pts; for (double f = 80; f < 12000; f *= 1.12) { const size_t b = (size_t) std::lround(f / bh); if (b < smoothDb.size()) pts.push_back({ std::log2(f), (double) smoothDb[b], f }); }
    if (pts.size() < 4) return {};
    const double n = (double) pts.size(); double sx = 0, sy = 0, sxx = 0, sxy = 0; for (auto& p : pts) { sx += p.l2; sy += p.y; sxx += p.l2 * p.l2; sxy += p.l2 * p.y; }
    const double sl = (n * sxy - sx * sy) / (n * sxx - sx * sx), ic = (sy - sl * sx) / n;
    struct D { double f, d; }; std::vector<D> dev; for (auto& p : pts) dev.push_back({ p.f, p.y - (ic + sl * p.l2) });
    std::sort(dev.begin(), dev.end(), [](const D& a, const D& b) { return std::fabs(a.d) > std::fabs(b.d); });
    std::vector<EqBand> out; for (auto& d : dev) { if (out.size() >= 4) break; bool ok = true; for (auto& q : out) if (std::fabs(std::log2(q.freq / d.f)) <= 0.8) ok = false; if (ok && std::fabs(d.d) > 1.5) out.push_back({ d.f, clampd(-d.d * 0.6, -9, 9) }); }
    return out;
}

// Everything at once. x is mono at sample rate sr, at least 0.5 s, ideally one note or one hit.
inline bool analyse(const std::vector<float>& x, double sr, Result& R) {
    if (x.size() < (size_t)(0.5 * sr) || x.size() < 20000) return false;
    const size_t win = (size_t) std::lround(sr * 240.0 / 48000.0);
    auto pe = peakEnv(x, win); size_t on0 = 0, pk0 = 0; const double dt = (double) win / sr;
    auto e0 = fitAdsr(pe, dt, nullptr, &on0, &pk0); (void) e0;
    size_t stS = std::min(x.size() - 16384, (pk0 + (size_t) std::lround(0.06 / dt)) * win);
    std::vector<float> m; double mbh = 0; R.f0 = findF0(x, stS, sr, &m, &mbh);
    if (R.f0 < 30) return false;
    const double mi = 69 + 12 * std::log2(R.f0 / 440); R.midi = (int) std::lround(mi); R.cents = (int) std::lround((mi - R.midi) * 100); R.noteName_ = std::string(noteName(R.midi)) + std::to_string(R.midi / 12 - 1);
    const size_t rw = (size_t) clampd(std::round(sr / R.f0 * 1.5), 240, 2400);
    auto re = rmsEnv(x, rw, win); size_t gate = 0; R.env = fitAdsr(re, dt, &gate);
    R.chroma = chromaOf(x, 0, x.size(), sr); bestKey(R.chroma, R.key, R.keyStrength);
    R.hasChord = bestChord(chromaOf(x, x.size() > 12288 ? x.size() - 12288 : 0, x.size(), sr), R.chord);
    R.bpm = findTempo(re, dt, &R.onsets);
    R.longTerm = longTermSpectrum(x, sr, &R.longTermBinHz); auto sm = smoothSpec(R.longTerm, R.longTermBinHz);
    // harmonics
    R.harmonicsDb.clear(); double h1 = 1e-9;
    for (int h = 1; h <= 16; ++h) { const long c0 = std::lround(R.f0 * h / mbh); float mx = 0; for (long i = c0 - 3; i <= c0 + 3; ++i) if (i > 0 && (size_t) i < m.size()) mx = std::max(mx, m[(size_t) i]); if (h == 1) h1 = std::max(1e-9, (double) mx); R.harmonicsDb.push_back(20 * std::log10((double) mx / h1 + 1e-9)); }
    R.wave = guessWave(R.harmonicsDb);
    estimateCutoff(sm, R.longTermBinHz, R.f0, R.cutoffHz, R.slope);
    // detuned voices: how much wider the 2nd harmonic's peak is than a pure tone's would be (power weighted spread inside -6 % to +6 %).
    auto bandSigma = [](const std::vector<float>& mag, double bh, double fc, double& mu) {
        const size_t lo = (size_t) std::max(2.0, std::floor(fc * 0.94 / bh)), hi = std::min(mag.size() - 2, (size_t) std::ceil(fc * 1.06 / bh));
        double sp = 0, sf = 0, sff = 0; for (size_t i = lo; i <= hi; ++i) { const double pw = (double) mag[i] * mag[i], f = (double) i * bh; sp += pw; sf += pw * f; sff += pw * f * f; }
        mu = sp > 0 ? sf / sp : fc; return sp > 0 ? std::sqrt(std::max(0.0, sff / sp - mu * mu)) : 0.0; };
    const int hh = (R.harmonicsDb.size() > 1 && R.harmonicsDb[1] > -28) ? 2 : 1; const double fh = R.f0 * hh;
    double mu = 0, mu0 = 0; const double sig = bandSigma(m, mbh, fh, mu);
    std::vector<float> pure(16384); for (size_t i = 0; i < pure.size(); ++i) pure[i] = (float) std::sin(2 * kPi * fh * (double) i / sr);
    std::vector<float> pm; { auto w = hann(16384); pm = magSpec(pure, 0, 16384, w, 65536); }
    const double sig0 = bandSigma(pm, mbh, fh, mu0);
    const double spreadHz = std::sqrt(std::max(0.0, sig * sig - sig0 * sig0)), D = spreadHz * std::sqrt(3.0) / fh * 1200.0 / std::log(2.0);       // half range in cents, uniform spread
    R.voices = D < 2.5 ? 1 : D < 9 ? 3 : 7; R.detuneCents = std::min(50.0, D * 0.5);       // 0.5: calibrated on synthetic 7 voice saws (the raw spread reads about twice the set detune)
    R.eq = suggestEq(sm, R.longTermBinHz);
    return true;
}
}
