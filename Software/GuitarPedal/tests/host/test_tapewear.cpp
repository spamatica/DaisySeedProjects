// Tests of the Tape Wear module (a modulated delay with wow and flutter, Wear, hiss, dropouts and a tape stop on the second footswitch): unity gain for
// small signals, Mix, wow and flutter (the pitch deviation), the Dark switch, hiss, dropouts, the tape stop (press, release, the effect switched off
// while held), the LED, the block path and bounds. Only one module is used at a time: the delay line is shared by all instances.
#include "tapewear_module.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <random>
#include <vector>
using namespace bkshepherd;
using T = TapeWearModule;

static int fails = 0;
static void check(bool ok, const char *what) {
    fails += !ok;
    std::printf("  %-120s %s\n", what, ok ? "OK" : "FAILED");
}

static constexpr float kSr = 48000.0f;
static constexpr size_t N = 48000;

struct Cfg {
    float wear = 0.0f, wow = 0.0f, flutter = 0.0f, dropouts = 0.0f, mix = 1.0f, level = 0.8f; // a clean setting: no wear, no modulation
    bool hiss = false, dark = false, unstable = false, sat = false;
};

static std::unique_ptr<T> make(const Cfg &c) {
    auto m = std::make_unique<T>();
    m->SetParameterAsFloat(T::WEAR, c.wear);
    m->SetParameterAsFloat(T::WOW, c.wow);
    m->SetParameterAsFloat(T::FLUTTER, c.flutter);
    m->SetParameterAsFloat(T::DROPOUTS, c.dropouts);
    m->SetParameterAsFloat(T::MIX, c.mix);
    m->SetParameterAsFloat(T::LEVEL, c.level);
    m->SetParameterAsBool(T::HISS, c.hiss);
    m->SetParameterAsBool(T::DARK, c.dark);
    m->SetParameterAsBool(T::UNSTABLE, c.unstable);
    m->SetParameterAsBool(T::SATURATION, c.sat);
    m->Init(kSr);
    m->SetEnabled(true);
    return m;
}

static std::vector<float> run(T &m, const std::vector<float> &in, size_t block = 48) {
    std::vector<float> l(in.size()), r(in.size());
    for (size_t o = 0; o < in.size(); o += block) m.ProcessMonoBlock(&in[o], &l[o], &r[o], std::min(block, in.size() - o));
    return l;
}

static std::vector<float> sine(float f, float peak, size_t n = N) {
    std::vector<float> v(n);
    for (size_t i = 0; i < n; i++) v[i] = peak * std::sin(2.0f * 3.14159265f * f * i / kSr);
    return v;
}

static double rms(const std::vector<float> &x, size_t from, size_t to) {
    double s = 0;
    for (size_t i = from; i < to; i++) s += double(x[i]) * x[i];
    return std::sqrt(s / (to - from));
}

// The gain in dB at frequency f for a sine of the given peak (measured over the second half)
static double gainDb(const Cfg &c, float f, float peak = 0.01f) {
    auto m = make(c);
    const std::vector<float> x = sine(f, peak, 24000), y = run(*m, x);
    return 20.0 * std::log10(rms(y, 12000, 24000) / rms(x, 12000, 24000) + 1e-12);
}

// The lowest and highest frequency of a sine, from the intervals between its rising zero crossings
static std::pair<double, double> freqRange(const std::vector<float> &y, size_t from) {
    double lo = 1e9, hi = 0, last = -1;
    for (size_t i = from + 1; i < y.size(); i++) {
        if (y[i - 1] < 0.0f && y[i] >= 0.0f) {
            const double t = i - 1 + double(-y[i - 1]) / (y[i] - y[i - 1]);
            if (last >= 0) {
                const double f = kSr / (t - last);
                lo = std::min(lo, f);
                hi = std::max(hi, f);
            }
            last = t;
        }
    }
    return {lo, hi};
}

int main() {
    std::printf("Tape Wear\n");

    { // gain and Mix
        const double g = gainDb(Cfg{}, 1000.0f);
        std::printf("    1 kHz at -40 dB, no wear: %+.2f dB\n", g);
        check(std::fabs(g) < 0.3, "no wear: unity gain for a small signal (Level 0 dB)");
        Cfg w;
        w.wear = 0.4f;
        const double gw = gainDb(w, 1000.0f);
        std::printf("    the same at the default Wear: %+.2f dB; at -6 dB peak: %+.2f dB\n", gw, gainDb(w, 1000.0f, 0.5f));
        check(std::fabs(gw) < 1.0, "the default Wear: a small signal keeps its level (within 1 dB)");
        check(gainDb(w, 1000.0f, 0.5f) < gw - 1.0, "a loud signal is compressed more than a small one");
        Cfg m0;
        m0.mix = 0.0f;
        m0.wear = 1.0f;
        m0.wow = 1.0f;
        auto m = make(m0);
        const std::vector<float> x = sine(440.0f, 0.3f, 24000), y = run(*m, x);
        float diff = 0;
        for (size_t i = 0; i < x.size(); i++) diff = std::fmax(diff, std::fabs(y[i] - x[i]));
        check(diff < 1e-6f, "Mix 0%: the input passes through unchanged, whatever the other settings");
        Cfg lv;
        lv.level = 1.0f;
        std::printf("    Level at maximum: %+.2f dB\n", gainDb(lv, 1000.0f));
        check(std::fabs(gainDb(lv, 1000.0f) - 6.0) < 0.4, "Level 100% is +6 dB");
    }

    { // wow and flutter
        Cfg c;
        auto still = make(c);
        const auto f0 = freqRange(run(*still, sine(1000.0f, 0.3f, N * 4)), 24000);
        c.wow = 1.0f;
        auto wow = make(c);
        const auto fw = freqRange(run(*wow, sine(1000.0f, 0.3f, N * 4)), 24000);
        Cfg fl;
        fl.flutter = 1.0f;
        auto flut = make(fl);
        const auto ff = freqRange(run(*flut, sine(1000.0f, 0.3f, N * 4)), 24000);
        Cfg un;
        un.wow = 1.0f;
        un.unstable = true;
        auto unst = make(un);
        const auto fu = freqRange(run(*unst, sine(1000.0f, 0.3f, N * 4)), 24000);
        std::printf("    frequency range of a 1 kHz tone: none %.1f..%.1f, Wow %.1f..%.1f, Flutter %.1f..%.1f, Wow and Unstable %.1f..%.1f Hz\n", f0.first, f0.second, fw.first,
                    fw.second, ff.first, ff.second, fu.first, fu.second);
        check(f0.second - f0.first < 1.0, "no Wow and Flutter: the pitch is steady");
        check(fw.second - fw.first > 15.0 && fw.second - fw.first < 30.0, "Wow 100%: the pitch wobbles by about 21 Hz peak to peak (6 ms at 0.28 Hz)");
        check(ff.second - ff.first > 40.0, "Flutter 100%: a faster wobble (1.2 ms at 5.4 Hz, about 82 Hz peak to peak)");
        check(fu.second - fu.first > (fw.second - fw.first) * 1.5, "Unstable makes the wow deeper and faster");
    }

    { // the Dark switch and Wear darken the tape
        Cfg c;
        const double off = gainDb(c, 12000.0f);
        c.dark = true;
        const double dark = gainDb(c, 12000.0f);
        Cfg w;
        w.wear = 1.0f;
        const double worn = gainDb(w, 12000.0f);
        std::printf("    12 kHz: %+.1f dB, Dark %+.1f dB, Wear 100%% %+.1f dB\n", off, dark, worn);
        check(dark < off - 1.5, "Dark lowers the bandwidth");
        check(worn < off - 5.0, "Wear 100% darkens the tape much more");
    }

    { // hiss
        const std::vector<float> silence(N, 0.0f);
        auto none = make(Cfg{});
        const double r0 = rms(run(*none, silence), 0, N);
        Cfg w;
        w.wear = 1.0f;
        auto wm = make(w);
        const double rw = rms(run(*wm, silence), 0, N);
        w.hiss = true;
        auto wh = make(w);
        const double rh = rms(run(*wh, silence), 0, N);
        std::printf("    hiss rms: no wear %.6f, Wear 100%% %.6f, with Hiss %.6f\n", r0, rw, rh);
        check(r0 == 0.0, "no wear and no Hiss: silence in gives silence out");
        check(rw > 0.0 && rh > rw * 3.0, "Wear adds a little hiss, the Hiss switch a lot more");
    }

    { // dropouts: the signal falls to a low level for 20 to 70 ms now and then
        auto count = [&](float amount) {
            Cfg c;
            c.dropouts = amount;
            auto m = make(c);
            const std::vector<float> y = run(*m, sine(1000.0f, 0.3f, N * 20));
            size_t low = 0, windows = 0;
            for (size_t o = 4800; o + 240 <= y.size(); o += 240, windows++) low += rms(y, o, o + 240) < 0.3 * 0.707 * 0.25;
            return double(low) / windows;
        };
        const double none = count(0.0f), full = count(1.0f);
        std::printf("    time with the signal down (over 20 s): Dropouts 0%% %.1f%%, 100%% %.1f%%\n", none * 100, full * 100);
        check(none == 0.0, "no Dropouts: the signal never drops");
        check(full > 0.05 && full < 0.2, "Dropouts 100%: about two a second of 20 to 70 ms (5 to 20% of the time; the lengths are not all the shortest)");
    }

    { // the tape stop
        auto m = make(Cfg{});
        const std::vector<float> x = sine(1000.0f, 0.3f, N);
        const double normal = rms(run(*m, x), 24000, N);
        check(!m->AlternateFootswitchForTempo(), "the second footswitch is not used for tap tempo");
        m->AlternateFootswitchPressed();
        const std::vector<float> held = run(*m, sine(1000.0f, 0.3f, N * 2));
        const double stopped = rms(held, N * 2 - 4800, N * 2);
        const auto fr = freqRange(held, 0);
        m->AlternateFootswitchReleased();
        const std::vector<float> back = run(*m, sine(1000.0f, 0.3f, N * 3));
        const double resumed = rms(back, N * 3 - 4800, N * 3);
        std::printf("    level: normal %.3f, held for 2 s %.4f, 3 s after the release %.3f; the pitch fell to %.0f Hz\n", normal, stopped, resumed, fr.first);
        check(stopped < normal * 0.1, "held: the tape slows down and the sound fades away");
        check(fr.first < 500.0, "held: the pitch falls (below half)");
        check(std::fabs(resumed / normal - 1.0) < 0.05, "released: the tape runs up to speed again and the level is back");

        auto led = make(Cfg{});
        const float off = led->GetBrightnessForLED(1);
        led->AlternateFootswitchPressed();
        const float on = led->GetBrightnessForLED(1);
        std::printf("    left LED: %.2f at rest, %.2f while held\n", off, on);
        check(on > off, "the left LED is brighter while the tape is held");

        auto sw = make(Cfg{});
        sw->AlternateFootswitchPressed();
        sw->SetEnabled(false);
        sw->SetEnabled(true);
        const std::vector<float> after = run(*sw, sine(1000.0f, 0.3f, N * 2));
        check(std::fabs(rms(after, N * 2 - 4800, N * 2) / normal - 1.0) < 0.05, "switching the effect off while the footswitch is down does not leave the tape stopped");
    }

    { // the block path and bounds
        std::mt19937 rng(7);
        std::uniform_real_distribution<float> u(0.0f, 1.0f);
        std::vector<float> noise(N * 2);
        for (float &x : noise) x = (u(rng) * 2.0f - 1.0f) * 0.8f;
        bool same = true, bounded = true;
        for (int k = 0; k < 6; k++) {
            Cfg c;
            c.wear = u(rng), c.wow = u(rng), c.flutter = u(rng), c.dropouts = u(rng), c.mix = u(rng), c.level = u(rng);
            c.hiss = k & 1, c.dark = k & 2, c.unstable = k & 4, c.sat = k % 3 == 0;
            auto play = [&](size_t block) {
                auto m = make(c);
                std::vector<float> out;
                const size_t step = 48 * 37;
                for (size_t o = 0; o < noise.size(); o += step) {
                    if ((o / step) % 2 == 0) m->AlternateFootswitchPressed(); else m->AlternateFootswitchReleased();
                    const size_t len = std::min(step, noise.size() - o);
                    std::vector<float> part(noise.begin() + o, noise.begin() + o + len);
                    std::vector<float> y = run(*m, part, block);
                    out.insert(out.end(), y.begin(), y.end());
                }
                return out;
            };
            const std::vector<float> y1 = play(1), y48 = play(48), y37 = play(37);
            same = same && y1 == y48 && y1 == y37;
            for (float x : y48) bounded = bounded && std::isfinite(x) && std::fabs(x) <= 5.0f;
        }
        check(same, "per-sample, block 48 and block 37 give identical output (random settings, with the footswitch pressed and released)");
        check(bounded, "the output is finite and bounded for loud noise at random settings");
    }

    std::printf("\n%s\n", fails ? "SOME TESTS FAILED" : "ALL OK");
    return fails ? 1 : 0;
}
