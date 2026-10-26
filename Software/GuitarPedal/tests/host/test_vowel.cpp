// Tests of the Vowel module (a bank of four formant filters, A E I O U, with a momentary change to another vowel on the second footswitch):
// the formants of every vowel (found from the frequency response), the voice scaling, Sharp, Glide, the footswitch (press, release, the effect
// switched off while held), Mix, the LED, the block path and bounds.
#include "vowel_module.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <random>
#include <vector>
using namespace bkshepherd;
using V = VowelModule;

static int fails = 0;
static void check(bool ok, const char *what) {
    fails += !ok;
    std::printf("  %-120s %s\n", what, ok ? "OK" : "FAILED");
}

static constexpr float kSr = 48000.0f;
static constexpr size_t N = 48000;

struct Cfg {
    int vowel = V::VOWEL_A, hold = V::VOWEL_I, voice = V::MALE;
    float glide = 0.4f, sharp = 0.5f, mix = 1.0f, level = 0.5f;
};

static std::unique_ptr<V> make(const Cfg &c) {
    auto m = std::make_unique<V>();
    m->SetParameterRaw(V::VOWEL, c.vowel);
    m->SetParameterRaw(V::HOLD_VOWEL, c.hold);
    m->SetParameterRaw(V::VOICE, c.voice);
    m->SetParameterAsFloat(V::GLIDE, c.glide);
    m->SetParameterAsFloat(V::SHARP, c.sharp);
    m->SetParameterAsFloat(V::MIX, c.mix);
    m->SetParameterAsFloat(V::LEVEL, c.level);
    m->Init(kSr);
    m->SetEnabled(true);
    return m;
}

static std::vector<float> run(V &m, const std::vector<float> &in, size_t block = 48) {
    std::vector<float> l(in.size()), r(in.size());
    for (size_t o = 0; o < in.size(); o += block) m.ProcessMonoBlock(&in[o], &l[o], &r[o], std::min(block, in.size() - o));
    return l;
}

static std::vector<float> sine(float f, float peak, size_t n = N) {
    std::vector<float> v(n);
    for (size_t i = 0; i < n; i++) v[i] = peak * std::sin(2.0f * 3.14159265f * f * i / kSr);
    return v;
}

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

// The gain in dB at frequency f, measured on an instance that has been set up (and possibly has its footswitch held)
static double gainOn(V &m, float f) {
    const std::vector<float> x = sine(f, 0.01f, 24000);
    return 20.0 * std::log10(amp(run(m, x), f) / amp(x, f) + 1e-12);
}
static double gainDb(const Cfg &c, float f) {
    auto m = make(c);
    return gainOn(*m, f);
}

// The frequency of the largest gain between lo and hi (1/24 octave grid)
static double peakIn(const Cfg &c, float lo, float hi) {
    double best = -1e9, bestF = 0;
    for (float f = lo; f < hi; f *= std::pow(2.0f, 1.0f / 24.0f)) {
        const double g = gainDb(c, f);
        if (g > best) { best = g; bestF = f; }
    }
    return bestF;
}

int main() {
    const char *names[V::VOWEL_COUNT] = {"A", "E", "I", "O", "U"};
    const float f1[V::VOWEL_COUNT] = {650, 400, 290, 400, 350};
    const float f2[V::VOWEL_COUNT] = {1080, 1700, 1870, 800, 600};

    std::printf("The first formant of every vowel, and the second where it is far from the first:\n");
    for (int v = 0; v < V::VOWEL_COUNT; v++) {
        Cfg c; c.vowel = v;
        const double p1 = peakIn(c, 200.0f, 700.0f);
        char what[160];
        std::snprintf(what, sizeof what, "%s: the lowest peak is at %.0f Hz (the first formant, within 15%%): measured %.0f Hz", names[v], f1[v], p1);
        check(std::fabs(p1 / f1[v] - 1.0) < 0.15, what);
        if (v <= V::VOWEL_I) {
            const double p2 = peakIn(c, 900.0f, 2300.0f);
            std::snprintf(what, sizeof what, "%s: the second formant at %.0f Hz (within 15%%): measured %.0f Hz", names[v], f2[v], p2);
            check(std::fabs(p2 / f2[v] - 1.0) < 0.15, what);
        }
    }
    {
        // the vowels differ: the balance of the first against the second formant region
        double ratio[V::VOWEL_COUNT];
        for (int v = 0; v < V::VOWEL_COUNT; v++) {
            Cfg c; c.vowel = v;
            ratio[v] = gainDb(c, 300.0f) - gainDb(c, 1800.0f);
        }
        std::printf("    300 Hz against 1.8 kHz: A %+.1f, E %+.1f, I %+.1f, O %+.1f, U %+.1f dB\n", ratio[0], ratio[1], ratio[2], ratio[3], ratio[4]);
        check(ratio[V::VOWEL_I] > ratio[V::VOWEL_E] && ratio[V::VOWEL_E] > ratio[V::VOWEL_A] - 40.0 && ratio[V::VOWEL_U] > ratio[V::VOWEL_A], "I is darker than A (the first formant low, the second high), U darker than A");
    }

    std::printf("\nVoice and Sharp:\n");
    {
        Cfg male; male.vowel = V::VOWEL_E;
        Cfg female = male; female.voice = V::FEMALE;
        Cfg child = male; child.voice = V::CHILD;
        const double pm = peakIn(male, 250.0f, 900.0f), pf = peakIn(female, 250.0f, 900.0f), pc = peakIn(child, 250.0f, 900.0f);
        std::printf("    the first formant of E: Male %.0f Hz, Female %.0f Hz, Child %.0f Hz\n", pm, pf, pc);
        check(std::fabs(pf / pm - 1.2) < 0.1 && std::fabs(pc / pm - 1.4) < 0.1, "Female is 20% and Child 40% higher than Male");
        Cfg soft; soft.vowel = V::VOWEL_I; soft.sharp = 0.0f;
        Cfg sharp = soft; sharp.sharp = 1.0f;
        const double softDrop = gainDb(soft, 290.0f) - gainDb(soft, 430.0f), sharpDrop = gainDb(sharp, 290.0f) - gainDb(sharp, 430.0f);
        std::printf("    I, the peak against 430 Hz: Sharp 0%% %.1f dB, Sharp 100%% %.1f dB\n", softDrop, sharpDrop);
        check(sharpDrop > softDrop + 6.0, "Sharp narrows the formants");
    }

    std::printf("\nThe second footswitch:\n");
    {
        Cfg c; c.vowel = V::VOWEL_A; c.hold = V::VOWEL_I; c.glide = 0.0f;  // the fastest glide (5 ms)
        auto m = make(c);
        const double rest = gainOn(*m, 650.0f) - gainOn(*m, 290.0f);
        m->AlternateFootswitchPressed();
        run(*m, std::vector<float>(24000, 0.0f));
        const double held = gainOn(*m, 650.0f) - gainOn(*m, 290.0f);
        m->AlternateFootswitchReleased();
        run(*m, std::vector<float>(24000, 0.0f));
        const double back = gainOn(*m, 650.0f) - gainOn(*m, 290.0f);
        std::printf("    650 Hz against 290 Hz: at rest (A) %+.1f dB, held (I) %+.1f dB, released %+.1f dB\n", rest, held, back);
        check(rest > 6.0 && held < -6.0, "held, the filter has the Hold Vowel (I: 290 Hz up, 650 Hz down); at rest it has the Vowel (A)");
        check(std::fabs(back - rest) < 0.5, "released, it returns to the Vowel");
        check(!m->AlternateFootswitchForTempo(), "the footswitch is not used for tap tempo");

        auto led = make(c);
        const float off = led->GetBrightnessForLED(1);
        led->AlternateFootswitchPressed();
        const float on = led->GetBrightnessForLED(1);
        std::printf("    left LED: %.2f at rest, %.2f while held\n", off, on);
        check(on > off, "the left LED is brighter while the second vowel is held");

        auto sw = make(c);
        sw->AlternateFootswitchPressed();
        sw->SetEnabled(false);
        sw->SetEnabled(true);
        run(*sw, std::vector<float>(24000, 0.0f));
        check(std::fabs(gainOn(*sw, 650.0f) - gainOn(*sw, 290.0f) - rest) < 0.5, "switching the effect off while the footswitch is held releases it (no vowel stuck)");
    }
    {
        // Glide: 20 ms after the press the filter is still on its way with a long glide, and has arrived with a short one
        auto level = [](float glide) {
            // the gain at 290 Hz right after the press: a short burst of that tone, 20 ms
            Cfg c; c.vowel = V::VOWEL_A; c.hold = V::VOWEL_I; c.glide = glide;
            auto m = make(c);
            m->AlternateFootswitchPressed();
            const std::vector<float> x = sine(290.0f, 0.01f, 960);
            const std::vector<float> y = run(*m, x);
            double s = 0;
            for (size_t i = 480; i < 960; i++) s += double(y[i]) * y[i];
            return std::sqrt(s / 480) / (0.01 / std::sqrt(2.0));
        };
        const double quick = level(0.0f), slow = level(1.0f);
        std::printf("    290 Hz in the 20 ms after the press (gain, linear): Glide 5 ms %.3f, Glide 800 ms %.3f\n", quick, slow);
        check(quick > 4.0 * slow, "a short Glide arrives at the Hold Vowel within 20 ms, a long one is still on its way");
    }

    std::printf("\nMix, Level, block path and bounds:\n");
    {
        Cfg c; c.mix = 0.0f;
        auto m = make(c);
        const std::vector<float> x = sine(440.0f, 0.01f);
        const std::vector<float> y = run(*m, x);
        double worst = 0;
        for (size_t i = 0; i < N; i++) worst = std::fmax(worst, std::fabs(y[i] - x[i]));
        check(worst < 1e-4, "Mix 0% is the dry signal");
        Cfg lo; lo.level = 0.0f; Cfg hi; hi.level = 1.0f;
        const double d = gainDb(hi, 650.0f) - gainDb(lo, 650.0f);
        check(std::fabs(d - 24.0) < 1.5, "Level spans 24 dB (-12 to +12)");

        std::mt19937 rng(11);
        std::uniform_real_distribution<float> u(-1.0f, 1.0f);
        std::vector<float> noise(N);
        for (size_t i = 0; i < N; i++) noise[i] = (0.5f + 0.5f * std::sin(i * 0.0003f)) * (0.8f * u(rng) + 0.2f * std::sin(i * 0.04f));
        bool same = true, bounded = true;
        for (int v = 0; v < V::VOWEL_COUNT; v++)
            for (int voice = 0; voice < V::VOICE_COUNT; voice++) {
                Cfg k; k.vowel = v; k.hold = (v + 2) % V::VOWEL_COUNT; k.voice = voice; k.sharp = 1.0f; k.level = 1.0f;
                auto a = make(k), b = make(k), s = make(k);
                // the footswitch is pressed and released at the same samples in all three runs (at block boundaries of 48 and 37: use multiples of both)
                auto play = [&](V &mod, size_t block) {
                    std::vector<float> out;
                    const size_t step = 48 * 37;
                    for (size_t o = 0; o < noise.size(); o += step) {
                        if ((o / step) % 2 == 0) mod.AlternateFootswitchPressed(); else mod.AlternateFootswitchReleased();
                        const size_t len = std::min(step, noise.size() - o);
                        std::vector<float> part(noise.begin() + o, noise.begin() + o + len);
                        std::vector<float> y = run(mod, part, block);
                        out.insert(out.end(), y.begin(), y.end());
                    }
                    return out;
                };
                const std::vector<float> y1 = play(*a, 1), y48 = play(*b, 48), y37 = play(*s, 37);
                same = same && y1 == y48 && y1 == y37;
                for (float x : y48) bounded = bounded && std::isfinite(x) && std::fabs(x) <= 1.0f;
            }
        check(same, "per-sample, block 48 and block 37 give identical output (every vowel and voice, with the footswitch pressed and released)");
        check(bounded, "the output is finite and never above full scale with loud noise at the maximum Sharp and Level");
        auto z = make(Cfg{});
        const std::vector<float> silent = run(*z, std::vector<float>(N, 0.0f));
        float peak = 0;
        for (float x : silent) peak = std::fmax(peak, std::fabs(x));
        check(peak == 0.0f, "silence in gives silence out");
    }

    std::printf("\n%s\n", fails ? "SOME TESTS FAILED" : "ALL OK");
    return fails ? 1 : 0;
}
