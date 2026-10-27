// Tests of the Vowel module (a bank of four formant filters, A E I O U, with a momentary change to another vowel on the second footswitch):
// the formants of every vowel (found from the frequency response), the voice scaling, Sharp, Glide, the footswitch (press, release, the effect
// switched off while held), Mix, the LED, the block path and bounds.
#include "vowel_module.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <random>
#include <string>
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
    float interval = 0.5f;
    int seq3 = V::VOWEL_U, seq4 = V::VOWEL_E, seq5 = V::VOWEL_O, steps = 3, mode = V::TO_VOWEL_2; // the Step mode: Vowel 3 to 5, how many, and the Footswitch
    float glide = 0.4f, sharp = 0.5f, mix = 1.0f, level = 0.5f;
};

static std::unique_ptr<V> make(const Cfg &c) {
    auto m = std::make_unique<V>();
    m->SetParameterRaw(V::VOWEL_1, c.vowel);
    m->SetParameterRaw(V::VOWEL_2, c.hold);
    m->SetParameterRaw(V::VOICE, c.voice);
    m->SetParameterRaw(V::VOWEL_3, c.seq3);
    m->SetParameterRaw(V::VOWEL_4, c.seq4);
    m->SetParameterRaw(V::VOWEL_5, c.seq5);
    m->SetParameterRaw(V::STEPS, c.steps - 2); // the choices are 2 to 5
    m->SetParameterRaw(V::FOOTSWITCH, c.mode);
    m->SetParameterAsFloat(V::INTERVAL, c.interval);
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

// Which vowel a filter has: the gain at five frequencies compared with those of a fresh filter of each vowel (the nearest one)
static std::vector<double> signature(V &m) {
    std::vector<double> s;
    for (float f : {290.0f, 350.0f, 400.0f, 650.0f, 1800.0f}) s.push_back(gainOn(m, f));
    return s;
}
static int whichVowel(V &m) {
    const std::vector<double> sig = signature(m);
    int best = -1;
    double bestD = 1e18;
    for (int v = 0; v < V::VOWEL_COUNT; v++) {
        Cfg c; c.vowel = v;
        auto ref = make(c);
        const std::vector<double> r = signature(*ref);
        double d = 0;
        for (size_t i = 0; i < r.size(); i++) d += (sig[i] - r[i]) * (sig[i] - r[i]);
        if (d < bestD) { bestD = d; best = v; }
    }
    return best;
}
static void settle(V &m) { run(m, std::vector<float>(24000, 0.0f)); } // far longer than the fastest Glide

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
        check(rest > 6.0 && held < -6.0, "held, the filter has Vowel 2 (I: 290 Hz up, 650 Hz down); at rest it has the Vowel (A)");
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
        check(quick > 4.0 * slow, "a short Glide arrives at Vowel 2 within 20 ms, a long one is still on its way");
    }

    std::printf("\nThe Step mode (every press goes to the next vowel):\n");
    {
        Cfg c; c.mode = V::STEP; c.glide = 0.0f; c.vowel = V::VOWEL_A; c.hold = V::VOWEL_I; c.seq3 = V::VOWEL_U; c.seq4 = V::VOWEL_E; c.seq5 = V::VOWEL_O; c.steps = 5;
        auto m = make(c);
        std::vector<int> seen{whichVowel(*m)};
        for (int press = 0; press < 6; press++) {
            m->AlternateFootswitchPressed();
            m->AlternateFootswitchReleased();   // the release does nothing in this mode
            settle(*m);
            seen.push_back(whichVowel(*m));
        }
        std::string text;
        for (int v : seen) text += std::string(names[v]) + " ";
        std::printf("    the vowel at the start and after each press (five steps A I U E O): %s\n", text.c_str());
        check(seen == std::vector<int>{V::VOWEL_A, V::VOWEL_I, V::VOWEL_U, V::VOWEL_E, V::VOWEL_O, V::VOWEL_A, V::VOWEL_I}, "five steps: every press goes to the next vowel (Vowel 1 to Vowel 5) and round to the first again");

        Cfg c3 = c; c3.steps = 3;
        auto m3 = make(c3);
        std::vector<int> seen3{whichVowel(*m3)};
        for (int press = 0; press < 4; press++) {
            m3->AlternateFootswitchPressed();
            settle(*m3);
            seen3.push_back(whichVowel(*m3));
        }
        check(seen3 == std::vector<int>{V::VOWEL_A, V::VOWEL_I, V::VOWEL_U, V::VOWEL_A, V::VOWEL_I}, "three steps: A I U and round (Vowel 4 and 5 are not used)");

        Cfg c2 = c; c2.steps = 2;
        auto m2 = make(c2);
        m2->AlternateFootswitchPressed();
        settle(*m2);
        const int first = whichVowel(*m2);
        m2->AlternateFootswitchPressed();
        settle(*m2);
        check(first == V::VOWEL_I && whichVowel(*m2) == V::VOWEL_A, "two steps: A, I, A");

        // switching the effect off starts it at the first vowel again
        m->AlternateFootswitchPressed();
        settle(*m);
        m->SetEnabled(false);
        m->SetEnabled(true);
        settle(*m);
        check(whichVowel(*m) == V::VOWEL_A, "switching the effect off and on starts at the first vowel again");

        const float led0 = [&] { auto l = make(c); return l->GetBrightnessForLED(1); }();
        auto ledm = make(c);
        for (int i = 0; i < 4; i++) ledm->AlternateFootswitchPressed();
        std::printf("    left LED at the first and at the last of five steps: %.2f, %.2f\n", led0, ledm->GetBrightnessForLED(1));
        check(ledm->GetBrightnessForLED(1) > led0 + 0.5f, "the left LED gets brighter with every step");

        // the Vowel 2 mode is not changed by Steps
        Cfg h; h.mode = V::TO_VOWEL_2; h.steps = 5; h.glide = 0.0f;
        auto hm = make(h);
        hm->AlternateFootswitchPressed();
        settle(*hm);
        const int held = whichVowel(*hm);
        hm->AlternateFootswitchReleased();
        settle(*hm);
        check(held == V::VOWEL_I && whichVowel(*hm) == V::VOWEL_A, "in the Vowel 2 mode a press still goes to Vowel 2 and the release back to Vowel 1");
    }

    std::printf("\nThe Auto mode (the vowel changes by itself, a click holds it):\n");
    {
        // A and I with a steady 290 Hz tone: A passes it weakly, I strongly, so the output level shows which vowel it is. Count the changes in a second.
        auto changes = [&](V &m, float seconds, std::vector<float> *levels = nullptr) {
            const size_t n = size_t(seconds * kSr), win = 480;  // 10 ms windows
            const std::vector<float> y = run(m, sine(290.0f, 0.01f, n));
            std::vector<double> rms;
            for (size_t o = 0; o + win <= n; o += win) {
                double s2 = 0;
                for (size_t i = o; i < o + win; i++) s2 += double(y[i]) * y[i];
                rms.push_back(std::sqrt(s2 / win));
            }
            double lo = 1e9, hi = 0;
            for (double r : rms) { lo = std::fmin(lo, r); hi = std::fmax(hi, r); }
            const double mid = std::sqrt(lo * hi);
            int count = 0;
            bool high = rms[0] > mid;
            for (double r : rms) {
                if (r > mid * 1.0 && !high) { high = true; count++; }
                else if (r < mid * 1.0 && high) { high = false; count++; }
            }
            if (levels) levels->assign(rms.begin(), rms.end());
            return std::make_pair(count, hi / std::fmax(lo, 1e-12));
        };
        Cfg c; c.mode = V::AUTO; c.glide = 0.0f; c.steps = 2; c.vowel = V::VOWEL_A; c.hold = V::VOWEL_I;
        c.interval = 0.4f;   // 60 ms * (2000/60)^0.4 = 247 ms
        {
            auto m = make(c);
            const auto r = changes(*m, 3.0f);
            std::printf("    Interval 247 ms, A and I: %d changes in 3 s, the level differs by a factor %.1f\n", r.first, r.second);
            check(r.second > 4.0, "the output level alternates between the two vowels (I passes the 290 Hz tone much more than A)");
            check(r.first >= 10 && r.first <= 14, "the vowels change every Interval (about 12 changes in 3 s at 247 ms)");
        }
        {
            Cfg slow = c; slow.interval = 1.0f;   // 2 s
            auto m = make(slow);
            const auto r = changes(*m, 5.0f);
            std::printf("    Interval 2 s: %d changes in 5 s\n", r.first);
            check(r.first >= 1 && r.first <= 3, "a longer Interval changes less often (2 s: about 2 changes in 5 s)");
        }
        {
            // the vowel is held while the footswitch is down, and the sequence goes on when it is released
            auto m = make(c);
            changes(*m, 0.5f);
            m->AlternateFootswitchPressed();
            const auto held = changes(*m, 2.0f);   // (the footswitch stays down)
            std::printf("    footswitch down: the level varies by a factor %.2f over 2 s (%.1f while it alternates)\n", held.second, 12.0);
            check(held.second < 2.0, "the vowel is held while the footswitch is down: the level stays the same (it alternates by a factor of about 12 otherwise)");
            check(m->GetBrightnessForLED(1) == 1.0f, "the left LED is steady while the vowel is held");
            m->AlternateFootswitchReleased();
            const auto again = changes(*m, 2.0f);
            std::printf("    footswitch released: %d changes in 2 s\n", again.first);
            check(again.first >= 6, "when the footswitch is released the sequence goes on (not stuck, and it does not run through the steps first)");
        }
        {
            // it goes on from the vowel it was held at: the vowel after the held one is the next
            Cfg c3 = c; c3.steps = 3; c3.interval = 0.4f;
            auto m = make(c3);
            run(*m, std::vector<float>(size_t(0.35f * kSr), 0.0f));   // one change so far (A to I)
            m->AlternateFootswitchPressed();
            run(*m, std::vector<float>(size_t(3.0f * kSr), 0.0f));    // held for a long time
            m->AlternateFootswitchReleased();
            settle(*m);
            run(*m, std::vector<float>(size_t(0.1f * kSr), 0.0f));
            check(m->GetBrightnessForLED(1) < 1.0f, "after the release the LED flashes again instead of being steady");
        }
        {
            // the hold is lost when the effect is switched off, and the sequence starts at the first vowel
            auto m = make(c);
            m->AlternateFootswitchPressed();
            m->SetEnabled(false);
            m->SetEnabled(true);
            const auto r = changes(*m, 1.0f);
            check(r.first >= 3, "switching the effect off and on lets go of a held vowel");
        }
        {
            // the LED flashes at every change
            auto m = make(c);
            float lo = 1.0f, hi = 0.0f;
            for (int i = 0; i < 400; i++) {
                run(*m, std::vector<float>(48, 0.0f));
                const float l = m->GetBrightnessForLED(1);
                lo = std::fmin(lo, l);
                hi = std::fmax(hi, l);
            }
            std::printf("    left LED over 0.4 s: %.2f to %.2f\n", lo, hi);
            check(hi > 0.9f && lo < 0.5f, "the left LED flashes at every change");
        }
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
                Cfg k; k.vowel = v; k.hold = (v + 2) % V::VOWEL_COUNT; k.voice = voice; k.sharp = 1.0f; k.level = 1.0f; k.mode = (v + voice) % 3; k.steps = 5; k.interval = 0.2f; // Hold, Step and Auto
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
        check(same, "per-sample, block 48 and block 37 give identical output (every vowel and voice, Vowel 2, Step and Auto, with the footswitch pressed and released)");
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
