// Tests of the Dirty Verb module (a feedback delay network reverb): the decay time, that the tail gets darker as it fades and Damp and Tone, the level, Mix, the pre-delay,
// Size, that the two channels differ, stability at the extremes, the tail after the input stops, the block path (mono and stereo) and bounds.
// Only one module is used at a time: the delay lines are shared by all instances.
#include "dirty_verb_module.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <random>
#include <vector>
using namespace bkshepherd;
using A = DirtyVerbModule;

static int fails = 0;
static void check(bool ok, const char *what) {
    fails += !ok;
    std::printf("  %-120s %s\n", what, ok ? "OK" : "FAILED");
}

static constexpr float kSr = 48000.0f;

struct Cfg {
    float decay = 0.691f, mix = 1.0f, damp = 0.5f, size = 1.0f, mod = 0.4f, tone = 0.6f, pre = 0.0f, lowcut = 0.43f, rate = 0.49f, diff = 0.7f, dist = 0.0f, loop = 0.0f, fizz = 1.0f, distLevel = 0.4f, comp = 1.0f, blend = 0.0f; // (distLevel 0.4 is 0 dB: the tests before Dist Level was added)
    int place = 0, type = 0; // (the tests of Dist before the types were added use Soft)
};

static std::unique_ptr<A> make(const Cfg &c) {
    auto m = std::make_unique<A>();
    m->SetParameterAsFloat(A::DECAY, c.decay);
    m->SetParameterAsFloat(A::MIX, c.mix);
    m->SetParameterAsFloat(A::DAMP, c.damp);
    m->SetParameterAsFloat(A::SIZE, c.size);
    m->SetParameterAsFloat(A::MOD, c.mod);
    m->SetParameterAsFloat(A::TONE, c.tone);
    m->SetParameterAsFloat(A::PREDELAY, c.pre);
    m->SetParameterAsFloat(A::LOWCUT, c.lowcut);
    m->SetParameterAsFloat(A::MODRATE, c.rate);
    m->SetParameterAsFloat(A::DIFFUSION, c.diff);
    m->SetParameterAsFloat(A::DIST, c.dist);
    m->SetParameterRaw(A::DISTPLACE, c.place);
    m->SetParameterAsFloat(A::LINEDIST, c.loop);
    m->SetParameterRaw(A::DISTTYPE, c.type);
    m->SetParameterAsFloat(A::FIZZFREQ, c.fizz);
    m->SetParameterAsFloat(A::DISTLEVEL, c.distLevel);
    m->SetParameterAsFloat(A::LINECOMP, c.comp);
    m->SetParameterAsFloat(A::CLEANBLEND, c.blend);
    m->Init(kSr);
    m->SetEnabled(true);
    return m;
}

struct Out {
    std::vector<float> l, r;
};
static Out run(A &m, const std::vector<float> &in, size_t block = 48) {
    Out o{std::vector<float>(in.size()), std::vector<float>(in.size())};
    for (size_t p = 0; p < in.size(); p += block) m.ProcessMonoBlock(&in[p], &o.l[p], &o.r[p], std::min(block, in.size() - p));
    return o;
}

static std::vector<float> impulse(size_t n) {
    std::vector<float> v(n, 0.0f);
    v[0] = 1.0f;
    return v;
}

// The time (s) for the tail energy to fall by 60 dB, extrapolated from the first 30 dB of the backward-integrated energy (Schroeder)
static double rt60(const std::vector<float> &y) {
    std::vector<double> e(y.size());
    double s = 0;
    for (size_t i = y.size(); i-- > 0;) {
        s += double(y[i]) * y[i];
        e[i] = s;
    }
    for (size_t i = 0; i < e.size(); i++)
        if (10.0 * std::log10(e[i] / e[0]) < -30.0) return 2.0 * i / kSr;
    return -1.0;
}

static double rmsOf(const std::vector<float> &x, size_t from, size_t to) {
    double s = 0;
    for (size_t i = from; i < to; i++) s += double(x[i]) * x[i];
    return std::sqrt(s / (to - from));
}

// Energy of a signal above and below a frequency, from a one-pole split (a rough "how bright")
static double brightness(const std::vector<float> &x, size_t from, size_t to, double split = 3000.0) {
    double lp = 0, hi = 0, lo = 0;
    const double a = 1.0 - std::exp(-6.2831853 * split / kSr);
    for (size_t i = from; i < to; i++) {
        lp += a * (x[i] - lp);
        lo += lp * lp;
        const double h = x[i] - lp;
        hi += h * h;
    }
    return 10.0 * std::log10(hi / (lo + 1e-30) + 1e-30);
}

// The rms of what is above a frequency (a one-pole split), and of what is below 300 Hz (two one-pole low-passes), from 1 s on
static double rmsAbove(const std::vector<float> &y, double fc) {
    double lp = 0, e = 0;
    const double a = 1.0 - std::exp(-6.2831853 * fc / kSr);
    for (size_t i = 48000; i < y.size(); i++) {
        lp += a * (y[i] - lp);
        const double h = y[i] - lp;
        e += h * h;
    }
    return std::sqrt(e / (y.size() - 48000));
}
static double rmsLows(const std::vector<float> &y) {
    double a = 0, b = 0, e = 0;
    const double k = 1.0 - std::exp(-6.2831853 * 300.0 / kSr);
    for (size_t i = 48000; i < y.size(); i++) {
        a += k * (y[i] - a);
        b += k * (a - b);
        e += b * b;
    }
    return std::sqrt(e / (y.size() - 48000));
}

static std::vector<float> noise(size_t n, float rms, unsigned seed = 3) {
    std::mt19937 g(seed);
    std::normal_distribution<float> d(0.0f, rms);
    std::vector<float> v(n);
    for (float &x : v) x = d(g);
    return v;
}

int main() {
    std::printf("Dirty Verb\n");

    { // decay time
        double t[3];
        const float knobs[3] = {0.2f, 0.4f, 0.6f};
        for (int k = 0; k < 3; k++) {
            Cfg c;
            c.decay = knobs[k];
            c.damp = 1.0f;
            c.tone = 1.0f;
            auto m = make(c);
            t[k] = rt60(run(*m, impulse(static_cast<size_t>(kSr) * 40)).l);
            std::printf("    Decay %.0f%% (%.1f s): measured %.2f s\n", knobs[k] * 100, ParamDisplay::Exp(knobs[k], 0.4f, 30.0f), t[k]);
        }
        const double set[3] = {ParamDisplay::Exp(0.2f, 0.4f, 30.0f), ParamDisplay::Exp(0.4f, 0.4f, 30.0f), ParamDisplay::Exp(0.6f, 0.4f, 30.0f)};
        check(t[0] > 0 && t[1] > t[0] && t[2] > t[1], "a longer Decay gives a longer tail");
        check(std::fabs(t[0] / set[0] - 1.0) < 0.15 && std::fabs(t[1] / set[1] - 1.0) < 0.25, "the tail lasts as long as the Decay says (the damping shortens it a little at long times)");
        check(t[2] > 0.7 * set[2] && t[2] < 1.1 * set[2], "also at a long Decay (within 30%)");
    }

    { // dark: the tail loses its highs as it fades; Damp and Tone move the brightness
        Cfg c;
        c.decay = 0.7f;
        auto m = make(c);
        const Out y = run(*m, noise(static_cast<size_t>(kSr), 0.2f));
        const size_t n = y.l.size();
        std::vector<float> tail = run(*m, std::vector<float>(static_cast<size_t>(kSr) * 6, 0.0f)).l;
        const double early = brightness(tail, 0, 24000), late = brightness(tail, 24000 * 8, 24000 * 12);
        std::printf("    highs against lows (dB): early in the tail %.1f, later %.1f\n", early, late);
        check(late < early - 3.0, "the tail gets darker as it fades");
        (void)n;
        auto bright = [&](float damp, float tone, size_t from = 24000) { // the tail from 0.5 s to 3 s after an impulse (the first echoes have not been through the loops many times)
            Cfg d;
            d.damp = damp;
            d.tone = tone;
            d.decay = 0.7f;
            auto mm = make(d);
            const Out o = run(*mm, impulse(static_cast<size_t>(kSr) * 3));
            return brightness(o.l, from, o.l.size());
        };
        const double dark = bright(0.0f, 0.6f), light = bright(1.0f, 0.6f);
        const double dull = bright(1.0f, 0.0f, 0), open = bright(1.0f, 1.0f, 0); // (Tone is heard on the whole tail when the loops are open)
        std::printf("    highs against lows (dB): Damp 0%% %.1f, Damp 100%% %.1f; Tone 0%% %.1f, Tone 100%% %.1f\n", dark, light, dull, open);
        check(light > dark + 6.0, "Damp: a higher cutoff in the loops gives a brighter reverb");
        check(open > dull + 4.0, "Tone: a higher cutoff on the output gives a brighter reverb");
        check(bright(0.5f, 0.6f) < -6.0, "the default setting is dark (the highs above 3 kHz are well below the lows)");
    }

    { // level, Mix
        auto m = make(Cfg{});
        const std::vector<float> x = noise(static_cast<size_t>(kSr) * 20, 0.1f);
        const Out y = run(*m, x);
        const double g = 20.0 * std::log10(rmsOf(y.l, 240000, x.size()) / rmsOf(x, 240000, x.size()));
        std::printf("    the wet level for noise at the default settings: %+.1f dB\n", g);
        check(std::fabs(g) < 3.0, "the wet level is about the level of the input (within 3 dB)");

        Cfg c0;
        c0.mix = 0.0f;
        auto m0 = make(c0);
        const std::vector<float> s = noise(24000, 0.3f);
        const Out y0 = run(*m0, s);
        float diff = 0;
        for (size_t i = 0; i < s.size(); i++) diff = std::fmax(diff, std::fmax(std::fabs(y0.l[i] - s[i]), std::fabs(y0.r[i] - s[i])));
        check(diff < 1e-6f, "Mix 0%: the input passes through unchanged");

        Cfg ch;
        ch.mix = 0.5f;
        auto mh = make(ch);
        const Out yh = run(*mh, impulse(2400));
        check(std::fabs(yh.l[0] - 0.5f) < 0.05f, "Mix 50%: half of the dry signal remains (a crossfade)");
    }

    { // pre-delay
        Cfg c;
        c.pre = 0.2f; // 100 ms
        c.lowcut = 0.0f;
        auto m = make(c);
        const Out y = run(*m, impulse(static_cast<size_t>(kSr)));
        const double before = rmsOf(y.l, 0, 4000), after = rmsOf(y.l, 4800, 9600); // 83 ms and from 100 ms
        std::printf("    the wet rms before 83 ms %.2e, from 100 ms to 200 ms %.2e\n", before, after);
        check(before < after * 1e-3, "PreDelay: nothing arrives before the pre-delay");
    }

    { // Size: the echo density changes the sound of the tail (the first reflection comes later with a bigger size)
        auto firstEcho = [&](float size) {
            Cfg c;
            c.size = size;
            c.diff = 0.0f;
            c.lowcut = 0.0f;
            c.damp = 1.0f;
            c.tone = 1.0f;
            auto m = make(c);
            const Out y = run(*m, impulse(24000));
            for (size_t i = 0; i < y.l.size(); i++)
                if (std::fabs(y.l[i]) > 1e-6f) return i - 1460; // (the four diffusers delay the input by 229 + 173 + 611 + 447 samples; with Diffusion 0 they are plain delays)
            return size_t(0);
        };
        const size_t small = firstEcho(0.0f), big = firstEcho(1.0f);
        std::printf("    the first echo arrives after %.1f ms at Size 0.5x and %.1f ms at 2x\n", small * 1000 / kSr, big * 1000 / kSr);
        check(big > small * 3, "Size: the delay lines are about four times longer from 0.5x to 2x");
    }

    { // Dist: the reverb is distorted, the dry signal is not
        const std::vector<float> x = noise(static_cast<size_t>(kSr) * 6, 0.1f);
        auto wetOf = [&](float dist) {
            Cfg c;
            c.dist = dist;
            auto m = make(c);
            return run(*m, x).l;
        };
        const std::vector<float> clean = wetOf(0.0f), mid = wetOf(0.5f), full = wetOf(1.0f);
        auto crest = [&](const std::vector<float> &y) {
            float pk = 0;
            for (size_t i = 48000; i < y.size(); i++) pk = std::fmax(pk, std::fabs(y[i]));
            return 20.0 * std::log10(pk / rmsOf(y, 48000, y.size()));
        };
        const double lvl = 20.0 * std::log10(rmsOf(full, 48000, full.size()) / rmsOf(clean, 48000, clean.size()));
        std::printf("    crest factor of the reverb (dB): Dist 0%% %.1f, 50%% %.1f, 100%% %.1f; level at 100%%: %+.1f dB\n", crest(clean), crest(mid), crest(full), lvl);
        check(crest(mid) < crest(clean) - 0.5 && crest(full) < crest(mid), "Dist clips the reverb: the peaks come down towards the average");
        { // a low note (220 Hz) gets a third harmonic (660 Hz)
            std::vector<float> sine(static_cast<size_t>(kSr) * 4);
            for (size_t i = 0; i < sine.size(); i++) sine[i] = 0.2f * std::sin(6.2831853f * 220.0f * i / kSr);
            Cfg c0, c1;
            c1.dist = 1.0f;
            auto m0 = make(c0);
            const std::vector<float> y0 = run(*m0, sine).l;
            auto m1 = make(c1);
            const std::vector<float> y1 = run(*m1, sine).l;
            auto line = [&](const std::vector<float> &y, double f) { // the amplitude at one frequency, over the last 2 s
                double re = 0, im = 0;
                for (size_t i = 96000; i < y.size(); i++) {
                    re += y[i] * std::cos(6.2831853 * f * i / kSr);
                    im += y[i] * std::sin(6.2831853 * f * i / kSr);
                }
                return std::sqrt(re * re + im * im);
            };
            const double h0 = 20 * std::log10(line(y0, 660) / line(y0, 220)), h1 = 20 * std::log10(line(y1, 660) / line(y1, 220));
            std::printf("    third harmonic against the note (dB): Dist 0%% %.1f, 100%% %.1f\n", h0, h1);
            check(h1 > h0 + 10.0, "Dist adds harmonics to a low note");
        }
        check(std::fabs(lvl) < 6.0, "the level stays within 6 dB of the clean reverb");

        // the dry signal: before the first echoes (the diffusers delay the reverb by 1460 samples) the outputs are the same with and without distortion
        Cfg a, b;
        a.mix = b.mix = 0.5f;
        b.dist = 1.0f;
        auto ma = make(a);
        const Out ya = run(*ma, noise(1400, 0.8f, 9));
        auto mb = make(b);
        const Out yb = run(*mb, noise(1400, 0.8f, 9));
        check(ya.l == yb.l && ya.r == yb.r, "the dry signal is not distorted: the output before the reverb arrives is the same");

        Cfg d;
        d.dist = 1.0f;
        d.decay = 0.0f;
        auto dm = make(d);
        run(*dm, noise(static_cast<size_t>(kSr), 0.5f));
        const Out t = run(*dm, std::vector<float>(static_cast<size_t>(kSr) * 5, 0.0f));
        std::printf("    rms 4 to 5 s after the input stopped, Dist 100%%: %.2e\n", rmsOf(t.l, 4 * 48000, 5 * 48000));
        check(rmsOf(t.l, 4 * 48000, 5 * 48000) < 1e-4, "with distortion the tail still dies away");
    }

    { // Dist on the input of the reverb (Dist Place: Input)
        Cfg in;
        in.dist = 1.0f;
        in.place = 1;
        // the dry signal is not distorted
        Cfg a = in, b = in;
        a.mix = b.mix = 0.5f;
        a.dist = 0.0f;
        auto ma = make(a);
        const Out ya = run(*ma, noise(1400, 0.8f, 9));
        auto mb = make(b);
        const Out yb = run(*mb, noise(1400, 0.8f, 9));
        check(ya.l == yb.l && ya.r == yb.r, "Input: the dry signal is not distorted either");

        std::vector<float> sine(static_cast<size_t>(kSr) * 4);
        for (size_t i = 0; i < sine.size(); i++) sine[i] = 0.2f * std::sin(6.2831853f * 220.0f * i / kSr);
        auto line = [&](const std::vector<float> &y, double f) {
            double re = 0, im = 0;
            for (size_t i = 96000; i < y.size(); i++) {
                re += y[i] * std::cos(6.2831853 * f * i / kSr);
                im += y[i] * std::sin(6.2831853 * f * i / kSr);
            }
            return std::sqrt(re * re + im * im);
        };
        Cfg c0;
        auto m0 = make(c0);
        const std::vector<float> y0 = run(*m0, sine).l;
        auto m1 = make(in);
        const std::vector<float> y1 = run(*m1, sine).l;
        const double h0 = 20 * std::log10(line(y0, 660) / line(y0, 220)), h1 = 20 * std::log10(line(y1, 660) / line(y1, 220));
        std::printf("    third harmonic against the note (dB): Dist 0%% %.1f, Input 100%% %.1f\n", h0, h1);
        check(h1 > h0 + 10.0, "Input: the reverb of a low note has harmonics");

        const std::vector<float> x = noise(static_cast<size_t>(kSr) * 6, 0.1f);
        auto mc = make(Cfg{});
        const std::vector<float> clean = run(*mc, x).l;
        auto mi = make(in);
        const std::vector<float> dirty = run(*mi, x).l;
        const double lvl = 20.0 * std::log10(rmsOf(dirty, 48000, dirty.size()) / rmsOf(clean, 48000, clean.size()));
        std::printf("    level of the reverb of noise, Input 100%% against off: %+.1f dB\n", lvl);
        check(std::fabs(lvl) < 6.0, "Input: the level stays within 6 dB of the clean reverb");

        // the decay time is the same: the tail is not distorted again and again
        Cfg d = in;
        d.decay = 0.2f; // 0.9 s
        d.damp = 1.0f;
        d.tone = 1.0f;
        d.dist = 0.5f;
        auto md = make(d);
        const double t = rt60(run(*md, impulse(static_cast<size_t>(kSr) * 20)).l);
        std::printf("    Decay 0.9 s with Input distortion: measured %.2f s\n", t);
        check(t > 0.7 && t < 1.1, "Input: the Decay time is not changed");

        auto ms = make(in);
        run(*ms, noise(static_cast<size_t>(kSr), 0.5f));
        const Out tl = run(*ms, std::vector<float>(static_cast<size_t>(kSr) * 30, 0.0f));
        std::printf("    rms 28 to 30 s after the input stopped (Decay 7.9 s): %.2e\n", rmsOf(tl.l, 28 * 48000, 30 * 48000));
        check(rmsOf(tl.l, 28 * 48000, 30 * 48000) < 1e-3, "Input: the tail dies away");
    }

    { // Feedback Dist: a separate distortion inside the feedback loops, tapped to the output at the Dist Level
        Cfg lp;
        lp.loop = 1.0f;
        // the dry signal is not distorted
        Cfg a = lp, b = lp;
        a.mix = b.mix = 0.5f;
        a.loop = 0.0f;
        auto ma = make(a);
        const Out ya = run(*ma, noise(1400, 0.8f, 9));
        auto mb = make(b);
        const Out yb = run(*mb, noise(1400, 0.8f, 9));
        check(ya.l == yb.l && ya.r == yb.r, "Feedback Dist: the dry signal is not distorted");

        std::vector<float> saw(static_cast<size_t>(kSr) * 6);
        for (size_t i = 0; i < saw.size(); i++) saw[i] = 0.2f * (2.0f * std::fmod(110.0f * i / kSr, 1.0f) - 1.0f);
        // audible: the highs, for each type, against the clean reverb (Damp and Tone open a little so that the highs are not filtered away)
        for (int type = 0; type < 3; type++) {
            auto outOf = [&](float loop, float lvl) {
                Cfg c;
                c.type = type;
                c.loop = loop;
                c.distLevel = lvl; // 0.0 is -12 dB, 0.6 is +6 dB (the default), 1.0 is +18 dB
                c.damp = 0.8f;
                c.tone = 0.9f;
                auto m = make(c);
                return run(*m, saw).l;
            };
            const std::vector<float> off = outOf(0.0f, 0.6f), quiet = outOf(0.8f, 0.0f), def = outOf(0.8f, 0.6f), loud = outOf(0.8f, 1.0f);
            auto db = [&](const std::vector<float> &y) { return 20.0 * std::log10(rmsAbove(y, 1500.0) / rmsAbove(off, 1500.0)); };
            auto lvl = [&](const std::vector<float> &y) { return 20.0 * std::log10(rmsOf(y, 48000, y.size()) / rmsOf(off, 48000, off.size())); };
            const char *names[3] = {"Soft", "Hard", "Fizz"};
            std::printf("    %s, Feedback Dist 80%%: the highs against the clean reverb: Dist Level -12 dB %+.1f dB, +6 dB %+.1f dB, +18 dB %+.1f dB; the level at +6 dB %+.1f dB\n", names[type], db(quiet),
                        db(def), db(loud), lvl(def));
            check(db(def) > 4.0, "Feedback Dist is audible at the default Dist Level: clearly more highs than the clean reverb");
            check(db(quiet) < db(def) - 4.0 && db(loud) > db(def) + 6.0, "and Dist Level sets how loud it is");
            check(std::fabs(lvl(def)) < 12.0, "and does not make the reverb much louder or quieter");
        }

        // Line Comp: the distortion persists as the tail rings out (a note for 1 s, then silence); without it the distortion clears up as the tail fades
        for (int type : {0, 2}) {
            std::vector<float> note(static_cast<size_t>(kSr) * 7, 0.0f);
            for (size_t i = 0; i < 48000; i++) note[i] = saw[i];
            auto tail = [&](float amount, float comp) {
                Cfg c;
                c.type = type;
                c.loop = amount;
                c.comp = comp;
                c.distLevel = 0.6f; // the default, +6 dB
                c.damp = 0.8f;
                c.tone = 0.9f;
                auto m = make(c);
                return run(*m, note).l;
            };
            const std::vector<float> clean = tail(0.0f, 1.0f), fixed = tail(0.6f, 0.0f), follow = tail(0.6f, 1.0f);
            auto db = [&](const std::vector<float> &y, size_t from, size_t to) {
                std::vector<float> a(y.begin() + from, y.begin() + to), b(clean.begin() + from, clean.begin() + to);
                return 20.0 * std::log10((rmsAbove(a, 1500.0) + 1e-12) / (rmsAbove(b, 1500.0) + 1e-12));
            };
            // (rmsAbove skips the first second of what it is given, so the windows start one second before the part that is looked at)
            const double f1 = db(fixed, 48000, 96000 * 2), f3 = db(fixed, 48000 * 3, 48000 * 6);
            const double c1 = db(follow, 48000, 96000 * 2), c3 = db(follow, 48000 * 3, 48000 * 6);
            std::printf("    %s, Feedback Dist 60%%: the highs against the clean tail, 2 to 4 s and 4 to 6 s into it: fixed drive %+.1f, %+.1f dB; Line Comp 100%% %+.1f, %+.1f dB\n", type ? "Fizz" : "Soft", f1, f3, c1, c3);
            const double need = type ? 0.5 : 3.0; // (Fizz adds only the highs of the distortion: less)
            check(c3 > need && c3 > f3 + need, "Line Comp: the distortion is still there late in the tail (and clears up without it)");
        }

        // small signals are not distorted (the loops are linear below the clipping level), so the Decay time is as set
        for (int type = 0; type < 3; type++) {
            Cfg d;
            d.type = type;
            d.loop = 0.5f;
            d.decay = 0.2f; // 0.9 s
            d.damp = 1.0f;
            d.tone = 1.0f;
            auto md = make(d);
            std::vector<float> small = impulse(static_cast<size_t>(kSr) * 20);
            small[0] = 1e-5f;
            const double t = rt60(run(*md, small).l);
            check(t > 0.7 && t < 1.1, "Feedback Dist: the Decay time is as set (the delay lines are not changed)");
            if (type == 2) std::printf("    Decay 0.9 s with Feedback Dist at a small signal: measured %.2f s\n", t);
        }

        // loud input: the tails die away, for every type, with the Dist Level at the maximum
        bool dies = true, bounded = true;
        for (int type = 0; type < 3; type++) {
            Cfg d;
            d.type = type;
            d.loop = 1.0f;
            d.distLevel = 1.0f;
            auto ms = make(d);
            const Out loud = run(*ms, noise(static_cast<size_t>(kSr) * 2, 0.9f));
            const Out tl = run(*ms, std::vector<float>(static_cast<size_t>(kSr) * 30, 0.0f));
            for (size_t i = 0; i < loud.l.size(); i++) bounded = bounded && std::isfinite(loud.l[i]) && std::fabs(loud.l[i]) < 50.0f;
            dies = dies && rmsOf(tl.l, 28 * 48000, 30 * 48000) < 1e-3;
        }
        check(bounded, "Feedback Dist: the output stays bounded with loud noise at the maximum Dist Level");
        check(dies, "Feedback Dist: the tail dies away, for every type");

        // harmonics for a low note (Soft)
        std::vector<float> sine(static_cast<size_t>(kSr) * 4);
        for (size_t i = 0; i < sine.size(); i++) sine[i] = 0.2f * std::sin(6.2831853f * 220.0f * i / kSr);
        auto line = [&](const std::vector<float> &y, double f) {
            double re = 0, im = 0;
            for (size_t i = 96000; i < y.size(); i++) {
                re += y[i] * std::cos(6.2831853 * f * i / kSr);
                im += y[i] * std::sin(6.2831853 * f * i / kSr);
            }
            return std::sqrt(re * re + im * im);
        };
        Cfg c0;
        auto m0 = make(c0);
        const std::vector<float> y0 = run(*m0, sine).l;
        auto m1 = make(lp);
        const std::vector<float> y1 = run(*m1, sine).l;
        const double h0 = 20 * std::log10(line(y0, 660) / line(y0, 220)), h1 = 20 * std::log10(line(y1, 660) / line(y1, 220));
        std::printf("    third harmonic against the note (dB): Feedback Dist 0%% %.1f, Soft 100%% %.1f\n", h0, h1);
        check(h1 > h0 + 10.0, "Feedback Dist: the tail of a low note has harmonics");
    }

    { // Dist Type: Hard and Fizz clip with more gain and make more highs; Fizz leaves the lows alone (a sawtooth at 110 Hz, like a low guitar note)
        std::vector<float> x(static_cast<size_t>(kSr) * 6);
        for (size_t i = 0; i < x.size(); i++) x[i] = 0.2f * (2.0f * std::fmod(110.0f * i / kSr, 1.0f) - 1.0f);
        for (int place = 0; place < 2; place++) {
            auto outOf = [&](int type, float dist) {
                Cfg c;
                c.place = place;
                c.type = type;
                c.dist = dist;
                c.distLevel = 0.6f; // the default, +6 dB
                c.damp = 0.8f; // the loops are brighter, so that the highs can be heard
                c.tone = 0.9f;
                auto m = make(c);
                return run(*m, x).l;
            };
            const std::vector<float> off = outOf(0, 0.0f), soft = outOf(0, 0.8f), hard = outOf(1, 0.8f), fizz = outOf(2, 0.8f);
            auto hi = [&](const std::vector<float> &y) { return 20.0 * std::log10(rmsAbove(y, 1500.0) / rmsAbove(off, 1500.0)); }; // the highs against the clean reverb
            auto level = [&](const std::vector<float> &y) { return 20.0 * std::log10(rmsOf(y, 48000, y.size()) / rmsOf(off, 48000, off.size())); };
            std::printf("    %s: the highs (above 1.5 kHz) against the clean reverb (dB): Soft %.1f, Hard %.1f, Fizz %.1f; level: Soft %+.1f, Hard %+.1f, Fizz %+.1f dB; lows of Fizz %+.1f dB\n",
                        place ? "Input" : "Output", hi(soft), hi(hard), hi(fizz), level(soft), level(hard), level(fizz), 20 * std::log10(rmsLows(fizz) / rmsLows(off)));
            check(hi(fizz) > 6.0 && hi(fizz) > hi(soft) + 3.0, "Fizz makes a good deal more highs (above 1.5 kHz) than the clean reverb and than Soft");
            check(hi(hard) > 1.0, "Hard makes some more highs than the clean reverb (it clips the whole band, so the lows take most of the clipping)");
            check(std::fabs(20 * std::log10(rmsLows(fizz) / rmsLows(off))) < (place ? 7.0 : 2.0), "Fizz: the lows stay about as they were (at the Output, almost exactly; at the Input the clipped highs of a note also make low products that the reverb rings with)");
            check(std::fabs(level(hard)) < 9.0 && std::fabs(level(fizz)) < 9.0, "the level stays within 9 dB of the clean reverb");
        }
        // the dry signal is not distorted, and the tail dies away, for every type
        bool dry = true, dies = true;
        for (int type = 0; type < 3; type++) for (int place = 0; place < 2; place++) {
            Cfg a, b;
            a.mix = b.mix = 0.5f;
            a.type = b.type = type;
            a.place = b.place = place;
            b.dist = 1.0f;
            auto ma = make(a);
            const Out ya = run(*ma, noise(1400, 0.8f, 9));
            auto mb = make(b);
            const Out yb = run(*mb, noise(1400, 0.8f, 9));
            dry = dry && ya.l == yb.l && ya.r == yb.r;
            Cfg d;
            d.type = type;
            d.place = place;
            d.dist = 1.0f;
            d.decay = 0.0f;
            auto md = make(d);
            run(*md, noise(static_cast<size_t>(kSr), 0.5f));
            const Out t = run(*md, std::vector<float>(static_cast<size_t>(kSr) * 5, 0.0f));
            dies = dies && rmsOf(t.l, 4 * 48000, 5 * 48000) < 1e-4;
        }
        check(dry, "every type and place: the dry signal is not distorted");
        check(dies, "every type and place: the tail dies away (no DC or ringing builds up)");
    }

    { // Dist Level: the distorted part is louder, the clean lows are not changed (Fizz)
        std::vector<float> x(static_cast<size_t>(kSr) * 6);
        for (size_t i = 0; i < x.size(); i++) x[i] = 0.2f * (2.0f * std::fmod(110.0f * i / kSr, 1.0f) - 1.0f);
        for (int place = 0; place < 2; place++) {
            auto outOf = [&](float lvl) {
                Cfg c;
                c.place = place;
                c.type = 2;
                c.dist = 0.8f;
                c.distLevel = lvl; // 0.4 is 0 dB, 0.6 is +6 dB (the default), 1.0 is +18 dB
                c.damp = 0.8f;
                c.tone = 0.9f;
                auto m = make(c);
                return run(*m, x).l;
            };
            const std::vector<float> lo = outOf(0.4f), def = outOf(0.6f), hi = outOf(1.0f);
            const double up = 20 * std::log10(rmsAbove(def, 1500.0) / rmsAbove(lo, 1500.0)), up2 = 20 * std::log10(rmsAbove(hi, 1500.0) / rmsAbove(def, 1500.0)), drift = 20 * std::log10(rmsLows(def) / rmsLows(lo));
            std::printf("    %s: the highs (above 1.5 kHz) at +6 dB against 0 dB: %+.1f dB, at +18 dB against +6 dB: %+.1f dB; the lows change by %+.1f dB (+6 dB against 0 dB)\n", place ? "Input" : "Output", up, up2, drift);
            check(up > 4.0 && up2 > 8.0, "Dist Level makes the distorted highs louder");
            check(std::fabs(drift) < 6.0, "and the lows change much less than the highs (some low products of the clipping)");
        }
    }

    { // Clean Blend: a second reverb with the same settings and no distortion, in parallel
        const std::vector<float> x = noise(static_cast<size_t>(kSr) * 3, 0.3f, 21);
        Cfg dirty;
        dirty.dist = 0.8f;
        dirty.loop = 0.8f;
        dirty.place = 1;
        dirty.type = 1;
        dirty.distLevel = 0.6f;
        Cfg off = dirty; // the same, with no distortion at all
        off.dist = 0.0f;
        off.loop = 0.0f;
        Cfg a = dirty, b = dirty, c = dirty;
        a.blend = 0.0f;
        b.blend = 1.0f;
        c.blend = 0.5f;
        auto ma = make(a);
        const Out ya = run(*ma, x);
        auto mb = make(b);
        const Out yb = run(*mb, x);
        auto mc = make(c);
        const Out yc = run(*mc, x);
        auto mo = make(off);
        const Out yo = run(*mo, x);
        check(yb.l == yo.l && yb.r == yo.r, "Clean Blend 100%: the output is the same as that of the reverb with no distortion");
        check(ya.l != yo.l, "Clean Blend 0%: it is the distorted reverb");
        float diff = 0;
        for (size_t i = 0; i < x.size(); i++) diff = std::fmax(diff, std::fmax(std::fabs(yc.l[i] - 0.5f * (ya.l[i] + yb.l[i])), std::fabs(yc.r[i] - 0.5f * (ya.r[i] + yb.r[i]))));
        std::printf("    Clean Blend 50%%: the largest difference from the mean of the two reverbs: %.2e\n", diff);
        check(diff < 1e-5f, "Clean Blend 50%: the level is in the middle between the two reverbs (a crossfade)");
        const double hiA = rmsAbove(ya.l, 1500.0), hiB = rmsAbove(yb.l, 1500.0), hiC = rmsAbove(yc.l, 1500.0);
        std::printf("    the highs (above 1.5 kHz), rms: distorted reverb %.4f, 50%% %.4f, clean reverb %.4f\n", hiA, hiC, hiB);
        check(hiC > std::fmin(hiA, hiB) && hiC < std::fmax(hiA, hiB), "and so the highs are in between too");

        // A reverb that is not heard is cleared, so that it does not give a stale tail when it is needed again: the main reverb is loaded with noise (blend 0), then put out
        // of use (blend 100%), then silence for a while, and then it is used again (blend 0%) with silence at the input: nothing should come out.
        auto m = std::make_unique<A>();
        {
            Cfg d = dirty;
            d.blend = 0.0f;
            m = make(d);
        }
        run(*m, noise(static_cast<size_t>(kSr) * 2, 0.5f, 5));
        m->SetParameterAsFloat(A::CLEANBLEND, 1.0f);
        run(*m, std::vector<float>(static_cast<size_t>(kSr) / 2, 0.0f)); // (the main reverb fades out, stops, and is cleared in about 33 ms after that)
        m->SetParameterAsFloat(A::CLEANBLEND, 0.0f);
        const Out back = run(*m, std::vector<float>(static_cast<size_t>(kSr), 0.0f));
        std::printf("    rms of the main reverb when it is used again after being out of use (silent input): %.2e\n", rmsOf(back.l, 0, back.l.size()));
        check(rmsOf(back.l, 0, back.l.size()) < 1e-9, "a reverb that was out of use starts from silence (no stale tail)");

        // moving the blend while playing: identical for per-sample, block 48 and block 37 (the decision to run a reverb is made on the stream, not the calls)
        auto play = [&](size_t block) {
            auto mm = make(dirty);
            std::vector<float> outv;
            const size_t step = 48 * 37;
            const float blends[6] = {0.0f, 1.0f, 0.5f, 0.0f, 1.0f, 0.2f};
            const std::vector<float> in = noise(step * 24, 0.5f, 33);
            for (size_t o = 0, k = 0; o < in.size(); o += step, k++) {
                mm->SetParameterAsFloat(A::CLEANBLEND, blends[(k / 2) % 6]);
                std::vector<float> part(in.begin() + o, in.begin() + o + step);
                const Out y = run(*mm, part, block);
                outv.insert(outv.end(), y.l.begin(), y.l.end());
                outv.insert(outv.end(), y.r.begin(), y.r.end());
            }
            return outv;
        };
        check(play(1) == play(48) && play(1) == play(37), "moving Clean Blend while playing: per-sample, block 48 and block 37 give identical output");
    }

    { // the two channels differ
        auto m = make(Cfg{});
        const Out y = run(*m, noise(static_cast<size_t>(kSr) * 4, 0.2f));
        double ll = 0, rr = 0, lr = 0;
        for (size_t i = 48000; i < y.l.size(); i++) {
            ll += double(y.l[i]) * y.l[i];
            rr += double(y.r[i]) * y.r[i];
            lr += double(y.l[i]) * y.r[i];
        }
        const double corr = lr / std::sqrt(ll * rr);
        std::printf("    correlation of left and right: %.2f; level difference %.1f dB\n", corr, 10 * std::log10(ll / rr));
        check(std::fabs(corr) < 0.4, "the two channels are decorrelated");
        check(std::fabs(10 * std::log10(ll / rr)) < 2.0, "and equally loud");
    }

    { // stability and the tail
        Cfg c;
        c.decay = 1.0f;
        c.damp = 1.0f;
        c.tone = 1.0f;
        c.mod = 1.0f;
        c.rate = 1.0f;
        c.size = 1.0f;
        c.pre = 1.0f;
        c.diff = 1.0f;
        c.dist = 1.0f;
        c.loop = 1.0f;
        auto m = make(c);
        const Out y = run(*m, noise(static_cast<size_t>(kSr) * 10, 0.9f));
        bool ok = true;
        float peak = 0;
        for (size_t i = 0; i < y.l.size(); i++) {
            ok = ok && std::isfinite(y.l[i]) && std::isfinite(y.r[i]);
            peak = std::fmax(peak, std::fmax(std::fabs(y.l[i]), std::fabs(y.r[i])));
        }
        std::printf("    peak with loud noise at the maximum of every setting: %.2f\n", peak);
        check(ok && peak < 20.0f, "the maximum of every setting stays finite and bounded");

        Cfg s;
        s.decay = 0.0f; // 0.4 s
        auto sm = make(s);
        run(*sm, noise(static_cast<size_t>(kSr), 0.5f));
        const Out t = run(*sm, std::vector<float>(static_cast<size_t>(kSr) * 5, 0.0f));
        const double late = rmsOf(t.l, 4 * 48000, 5 * 48000);
        std::printf("    rms 4 to 5 s after the input stopped (Decay 0.4 s): %.2e\n", late);
        check(late < 1e-4, "the tail dies away after the input stops");

        Cfg z;
        auto zm = make(z);
        const Out sil = run(*zm, std::vector<float>(48000, 0.0f));
        check(rmsOf(sil.l, 0, 48000) < 1e-10 && rmsOf(sil.r, 0, 48000) < 1e-10, "silence in gives (practically) silence out");
    }

    { // the block path and bounds, mono and stereo
        std::mt19937 rng(11);
        std::uniform_real_distribution<float> u(0.0f, 1.0f);
        const std::vector<float> inL = noise(48000 * 3, 0.5f, 5), inR = noise(48000 * 3, 0.5f, 6);
        bool same = true, sameSt = true;
        for (int k = 0; k < 4; k++) {
            Cfg c;
            c.decay = u(rng), c.mix = u(rng), c.damp = u(rng), c.size = u(rng), c.mod = u(rng), c.tone = u(rng), c.pre = u(rng) * 0.5f, c.lowcut = u(rng), c.rate = u(rng), c.diff = u(rng), c.dist = u(rng) < 0.5f ? 0.0f : u(rng), c.place = u(rng) < 0.5f ? 0 : 1, c.loop = u(rng) < 0.5f ? 0.0f : u(rng), c.type = static_cast<int>(u(rng) * 2.999f), c.fizz = u(rng), c.distLevel = u(rng), c.comp = u(rng), c.blend = u(rng) < 0.3f ? 0.0f : (u(rng) < 0.3f ? 1.0f : u(rng));
            auto mono = [&](size_t block) {
                auto m = make(c);
                return run(*m, inL, block);
            };
            const Out a = mono(1), b = mono(48), d = mono(37);
            same = same && a.l == b.l && a.r == b.r && a.l == d.l && a.r == d.r;
            auto stereo = [&](size_t block) {
                auto m = make(c);
                Out o{std::vector<float>(inL.size()), std::vector<float>(inL.size())};
                for (size_t p = 0; p < inL.size(); p += block) m->ProcessStereoBlock(&inL[p], &inR[p], &o.l[p], &o.r[p], std::min(block, inL.size() - p));
                return o;
            };
            const Out sa = stereo(1), sb = stereo(64), sd = stereo(37);
            sameSt = sameSt && sa.l == sb.l && sa.r == sb.r && sa.l == sd.l && sa.r == sd.r;
        }
        check(same, "mono: per-sample, block 48 and block 37 give identical output (random settings)");
        check(sameSt, "stereo: per-sample, block 64 and block 37 give identical output (random settings)");

        auto m = make(Cfg{});
        std::vector<float> in = inL;
        std::vector<float> l(in.size()), r(in.size());
        m->ProcessMonoBlock(in.data(), in.data(), r.data(), in.size()); // in place
        auto m2 = make(Cfg{});
        m2->ProcessMonoBlock(inL.data(), l.data(), r.data(), inL.size());
        check(in == l, "processing in place gives the same result as processing into other arrays");
    }

    std::printf("\n%s\n", fails ? "SOME TESTS FAILED" : "ALL OK");
    return fails ? 1 : 0;
}
