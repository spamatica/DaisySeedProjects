// Tests of the Auto Filter (envelope filter, five types): the sweep range of each type (found from the frequency response of the resting filter,
// Up at the bottom of the range, Down at the top), that playing harder opens the filter (Up) or closes it (Down), Sens, Decay, Peak, the three
// filter modes, Mix, the dry lows of the Bass type, that the block path equals the per-sample path, and bounds with a full-scale input.
#include "auto_filter_module.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <random>
#include <vector>
using namespace bkshepherd;
using A = AutoFilterModule;

static int fails = 0;
static void check(bool ok, const char *what) {
    fails += !ok;
    std::printf("  %-120s %s\n", what, ok ? "OK" : "FAILED");
}

static constexpr float kSr = 48000.0f;
static constexpr size_t N = 48000;
static const char *kTypeNames[A::TYPE_COUNT] = {"Dynamic", "Mutron", "Q-Tron", "Auto Wah", "Bass"};

struct Cfg {
    int type = A::DYNAMIC, dir = 0, filter = A::BANDPASS;
    float sens = 0.625f, range = 0.3f, peak = 0.5f, decay = 0.4f, mix = 1.0f, level = 0.5f;
};

static std::unique_ptr<A> make(const Cfg &c) {
    auto m = std::make_unique<A>();
    m->SetParameterRaw(A::TYPE, c.type);
    m->SetParameterRaw(A::DIRECTION, c.dir);
    m->SetParameterRaw(A::FILTER, c.filter);
    m->SetParameterAsFloat(A::SENS, c.sens);
    m->SetParameterAsFloat(A::RANGE, c.range);
    m->SetParameterAsFloat(A::PEAK, c.peak);
    m->SetParameterAsFloat(A::DECAY, c.decay);
    m->SetParameterAsFloat(A::MIX, c.mix);
    m->SetParameterAsFloat(A::LEVEL, c.level);
    m->Init(kSr);
    return m;
}

static std::vector<float> run(A &m, const std::vector<float> &in, size_t block = 48) {
    std::vector<float> l(in.size()), r(in.size());
    for (size_t o = 0; o < in.size(); o += block) m.ProcessMonoBlock(&in[o], &l[o], &r[o], std::min(block, in.size() - o));
    return l;
}

static std::vector<float> sine(float f, float peak, size_t n = N) {
    std::vector<float> v(n);
    for (size_t i = 0; i < n; i++) v[i] = peak * std::sin(2.0f * 3.14159265f * f * i / kSr);
    return v;
}

// The amplitude at frequency f over the second half of the signal (a single-bin DFT)
static double amp(const std::vector<float> &x, float f) {
    const size_t from = x.size() / 2, n = x.size() - from;
    double re = 0, im = 0;
    for (size_t i = 0; i < n; i++) {
        const double w = 2.0 * 3.14159265358979 * f * (from + i) / kSr;
        re += x[from + i] * std::cos(w);
        im += x[from + i] * std::sin(w);
    }
    return 2.0 * std::sqrt(re * re + im * im) / n;
}

// The gain in dB at frequency f for a tone of the given peak
static double gainDb(const Cfg &c, float f, float peak) {
    auto m = make(c);
    const std::vector<float> x = sine(f, peak);
    return 20.0 * std::log10(amp(run(*m, x), f) / amp(x, f) + 1e-12);
}

// The frequency (1/12 octave grid from 30 Hz to 12 kHz) where the resting filter has its largest gain; a tone of -100 dBFS does not move it
static double peakFrequency(const Cfg &c) {
    double best = -1e9, bestF = 0;
    for (float f = 30.0f; f < 12000.0f; f *= std::pow(2.0f, 1.0f / 12.0f)) {
        const double g = gainDb(c, f, 0.00001f);
        if (g > best) { best = g; bestF = f; }
    }
    return bestF;
}

int main() {
    std::printf("The sweep range of each type (the resting filter's peak, measured; Range 0%% and 100%%, Up = bottom, Down = top):\n");
    struct R { float lo, hi, oct; };
    const R range[A::TYPE_COUNT] = {{150, 1200, 3.5f}, {100, 800, 4.0f}, {120, 1000, 4.5f}, {350, 800, 2.6f}, {50, 300, 3.2f}};
    for (int t = 0; t < A::TYPE_COUNT; t++) {
        Cfg c; c.type = t; c.peak = 0.5f; c.sens = 0.0f;
        c.range = 0.0f;
        const double lo = peakFrequency(c);
        c.range = 1.0f;
        const double hi = peakFrequency(c);
        c.range = 0.0f; c.dir = 1;
        const double top = peakFrequency(c);
        std::printf("    %-9s Up: %.0f Hz .. %.0f Hz, Down at Range 0%%: %.0f Hz\n", kTypeNames[t], lo, hi, top);
        char what[160];
        std::snprintf(what, sizeof what, "%s: rests at %.0f Hz at Range 0%% and %.0f Hz at Range 100%% (within 20%%)", kTypeNames[t], range[t].lo, range[t].hi);
        check(std::fabs(lo / range[t].lo - 1.0) < 0.2 && std::fabs(hi / range[t].hi - 1.0) < 0.2, what);
        const double expectTop = range[t].lo * std::pow(2.0, range[t].oct);
        std::snprintf(what, sizeof what, "%s: Down rests at the top of the sweep, %.0f Hz (within 20%%)", kTypeNames[t], expectTop);
        check(std::fabs(top / expectTop - 1.0) < 0.2, what);
    }

    std::printf("\nPlaying harder moves the filter (Low Pass at 2 kHz; the gain of a soft against a hard tone):\n");
    for (int t = 0; t < A::TYPE_COUNT; t++) {
        Cfg up; up.type = t; up.filter = A::LOWPASS; up.peak = 0.0f; up.range = 0.0f;
        Cfg down = up; down.dir = 1;
        const double upSoft = gainDb(up, 2000.0f, 0.003f), upHard = gainDb(up, 2000.0f, 0.3f);
        const double downSoft = gainDb(down, 2000.0f, 0.003f), downHard = gainDb(down, 2000.0f, 0.3f);
        std::printf("    %-9s Up: soft %+.1f dB, hard %+.1f dB; Down: soft %+.1f dB, hard %+.1f dB\n", kTypeNames[t], upSoft, upHard, downSoft, downHard);
        char what[160];
        std::snprintf(what, sizeof what, "%s: Up opens with the playing (a 2 kHz tone passes more when hard) and Down closes", kTypeNames[t]);
        // (the input stage of the Mutron compresses the hard tone a little, so the margin is modest)
        check(upHard > upSoft + 4.0 && downHard < downSoft - 2.0, what);
    }

    std::printf("\nSens, Peak, Decay and the filter modes (Dynamic):\n");
    {
        Cfg c; c.filter = A::LOWPASS; c.peak = 0.0f; c.range = 0.0f;
        c.sens = 0.3f;
        const double lowSens = gainDb(c, 2000.0f, 0.01f);
        c.sens = 0.9f;
        const double highSens = gainDb(c, 2000.0f, 0.01f);
        std::printf("    2 kHz at a medium tone: Sens 30%% %+.1f dB, Sens 90%% %+.1f dB\n", lowSens, highSens);
        check(highSens > lowSens + 6.0, "more Sens opens the filter at the same playing level");
    }
    {
        Cfg c; c.sens = 0.0f; c.range = 0.5f;
        const double f0 = peakFrequency(c);
        c.peak = 0.0f;
        const double loPeak = gainDb(c, f0, 0.00001f) - gainDb(c, f0 * 2.0f, 0.00001f);
        c.peak = 1.0f;
        const double hiPeak = gainDb(c, f0, 0.00001f) - gainDb(c, f0 * 2.0f, 0.00001f);
        std::printf("    band-pass, the peak against an octave above: Peak 0%% %.1f dB, Peak 100%% %.1f dB\n", loPeak, hiPeak);
        check(hiPeak > loPeak + 6.0, "Peak sharpens the band-pass: the resonance is narrower at 100% than at 0%");
    }
    {
        Cfg c; c.sens = 0.0f; c.range = 0.5f; c.peak = 0.5f;
        c.filter = A::LOWPASS;
        const double lpLow = gainDb(c, 60.0f, 0.00001f), lpHigh = gainDb(c, 8000.0f, 0.00001f);
        c.filter = A::HIGHPASS;
        const double hpLow = gainDb(c, 60.0f, 0.00001f), hpHigh = gainDb(c, 8000.0f, 0.00001f);
        std::printf("    Low Pass: 60 Hz %+.1f dB, 8 kHz %+.1f dB; High Pass: 60 Hz %+.1f dB, 8 kHz %+.1f dB\n", lpLow, lpHigh, hpLow, hpHigh);
        check(lpLow - lpHigh > 20.0, "Low Pass passes the lows and cuts the highs");
        check(hpHigh - hpLow > 20.0, "High Pass passes the highs and cuts the lows");
    }
    {
        // Decay: a loud burst, then a quiet probe tone; how long until the filter has closed again (Low Pass, Up, the probe's gain halves)
        auto fallTime = [](float decay) {
            Cfg c; c.filter = A::LOWPASS; c.peak = 0.0f; c.range = 0.0f; c.decay = decay; c.sens = 0.8f;
            auto m = make(c);
            std::vector<float> burst = sine(220.0f, 0.5f, 9600), tail = sine(2000.0f, 0.002f, 48000);
            std::vector<float> x(burst);
            x.insert(x.end(), tail.begin(), tail.end());
            const std::vector<float> y = run(*m, x);
            // the output level of the probe in 10 ms windows: when it has fallen 6 dB from its first window after the burst
            auto rms = [&](size_t from) { double s = 0; for (size_t i = from; i < from + 480; i++) s += double(y[i]) * y[i]; return std::sqrt(s / 480); };
            const double start = rms(9600 + 480);
            for (size_t o = 9600 + 480; o + 480 < x.size(); o += 480)
                if (rms(o) < 0.5 * start) return (o - 9600) / kSr;
            return 1.0f;
        };
        const double fast = fallTime(0.0f), slow = fallTime(1.0f);
        std::printf("    time for the filter to fall back: Decay 0%% %.2f s, Decay 100%% %.2f s\n", fast, slow);
        check(slow > 3.0 * fast, "Decay sets how slowly the filter falls back after a note (100% more than three times 0%)");
    }

    std::printf("\nMix, level and the dry lows of the Bass type:\n");
    {
        Cfg c; c.mix = 0.0f;
        auto m = make(c);
        const std::vector<float> x = sine(440.0f, 0.01f);
        const std::vector<float> y = run(*m, x);
        double worst = 0;
        for (size_t i = 0; i < N; i++) worst = std::fmax(worst, std::fabs(y[i] - x[i]));
        std::printf("    Mix 0%%: largest difference from the input %.5f\n", worst);
        check(worst < 1e-4, "Mix 0% is the dry signal");
        Cfg lo; lo.level = 0.0f; Cfg hi; hi.level = 1.0f;
        const double d = gainDb(hi, 330.0f, 0.01f) - gainDb(lo, 330.0f, 0.01f);
        std::printf("    Level 100%% against 0%%: %.1f dB\n", d);
        check(std::fabs(d - 24.0) < 1.5, "Level spans 24 dB (-12 to +12)");
    }
    {
        Cfg bass; bass.type = A::BASS; bass.dir = 1; bass.range = 1.0f; bass.sens = 0.0f;   // Down, resting at the top: the filter is open high
        Cfg dyn = bass; dyn.type = A::DYNAMIC;
        dyn.range = 0.0f;
        const double b = gainDb(bass, 60.0f, 0.00001f) - gainDb(bass, 1000.0f, 0.00001f);
        const double d = gainDb(dyn, 60.0f, 0.00001f) - gainDb(dyn, 1000.0f, 0.00001f);
        std::printf("    60 Hz against 1 kHz with the filter far above: Bass %+.1f dB, Dynamic %+.1f dB\n", b, d);
        check(b > d + 10.0, "Bass keeps the dry lows when the filter is open high (the Dynamic type loses them)");
    }

    std::printf("\nBlock path and bounds:\n");
    {
        std::mt19937 rng(7);
        std::uniform_real_distribution<float> u(-1.0f, 1.0f);
        std::vector<float> x(N);
        for (size_t i = 0; i < N; i++) x[i] = (0.5f + 0.5f * std::sin(i * 0.0002f)) * (0.7f * u(rng) + 0.3f * std::sin(i * 0.05f));
        bool same = true, bounded = true;
        for (int t = 0; t < A::TYPE_COUNT; t++)
            for (int f = 0; f < A::FILTER_COUNT; f++)
                for (int dir = 0; dir < 2; dir++) {
                    Cfg c; c.type = t; c.filter = f; c.dir = dir; c.peak = 1.0f; c.sens = 1.0f; c.level = 1.0f;
                    auto a = make(c), b = make(c), s = make(c);
                    const std::vector<float> y1 = run(*a, x, 1), y48 = run(*b, x, 48), y37 = run(*s, x, 37);
                    same = same && y1 == y48 && y1 == y37;
                    for (float v : y48) bounded = bounded && std::isfinite(v) && std::fabs(v) <= 1.0f;
                }
        check(same, "per-sample, block 48 and block 37 give identical output (every type, filter and direction, Peak and Sens at 100%)");
        check(bounded, "the output is finite and never above full scale with a loud noisy input at the maximum Peak, Sens and Level");
        Cfg c;
        auto m = make(c);
        const std::vector<float> y = run(*m, std::vector<float>(N, 0.0f));
        float peak = 0;
        for (float v : y) peak = std::fmax(peak, std::fabs(v));
        check(peak == 0.0f, "silence in gives silence out");
    }

    std::printf("\n%s\n", fails ? "SOME TESTS FAILED" : "ALL OK");
    return fails ? 1 : 0;
}
