/* Offline x86 test of Spectrum (test.sh builds it, the shell with ASan/UBSan): audio passes bit-exact, a tone lights
 * its own third-octave bar at the right height (TOP, RANGE), VIEW picks the channel, FREEZE holds, RATE OFF clears
 * and stops reporting, bars are reported with audioMasterAutomate only (no UpdateDisplay) and not faster than RATE,
 * host writes to a bar are ignored, the PRESET list, the project chunk, NaN-free output.
 * With "bench" as second argument: CPU load only. Prints PASSED/FAILED. */
#include <chrono>
#include <cstdlib>
#include <random>
#include "vsthost.h"

static int bar_level(AEffect *e, int b) {   /* b: 1..31 */
    char k[16];
    std::snprintf(k, sizeof k, "Band %02d", b);
    return (int)std::lround(pget(e, k) * 20);
}
static int loudest(AEffect *e) {
    int best = 1;
    for (int b = 2; b <= 31; b++) if (bar_level(e, b) > bar_level(e, best)) best = b;
    return best;
}
static void opt(AEffect *e, const char *name, int i, int n) { pset(e, name, n > 1 ? (float)i / (n - 1) : 0.0f); }
/* pink noise (Paul Kellet's filter) at about rms_amp RMS: scaled by 0.11 the filter measures 0.19 RMS (-14.4 dBFS) */
static void pink(AEffect *e, double rms_amp, double sec) {
    std::vector<float> il(BS), ir(BS), ol(BS), orr(BS);
    float *in[2] = {il.data(), ir.data()}, *out[2] = {ol.data(), orr.data()};
    static unsigned rs = 12345;
    static float b0, b1, b2, b3, b4, b5, b6;
    for (int b = 0; b < (int)(sec * 44100 / BS); b++) {
        for (int i = 0; i < BS; i++) {
            rs = rs * 1664525u + 1013904223u;
            float w = ((rs >> 8) / (float)(1 << 24)) * 2 - 1;
            b0 = 0.99886f * b0 + w * 0.0555179f; b1 = 0.99332f * b1 + w * 0.0750759f; b2 = 0.96900f * b2 + w * 0.1538520f;
            b3 = 0.86650f * b3 + w * 0.3104856f; b4 = 0.55000f * b4 + w * 0.5329522f; b5 = -0.7616f * b5 - w * 0.0168980f;
            float p = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f;
            b6 = w * 0.115926f;
            il[i] = ir[i] = (float)(p * 0.11 * rms_amp / 0.19);
        }
        e->processReplacing(e, in, out, BS);
        for (int i = 0; i < BS; i++) for (float v : {ol[i], orr[i]}) if (!std::isfinite(v)) g_bad = true;
    }
}
static void dump(AEffect *e) {
    for (int b = 1; b <= 31; b++) std::printf("%d ", bar_level(e, b));
    std::printf("\n");
}

int main(int argc, char **argv) {
    if (argc < 2) { std::printf("usage: host_test plugin.so [bench]\n"); return 2; }
    AEffect *e = load_plugin(argv[1]);
    if (!e) return 1;
    std::vector<float> L, R;

    if (argc > 2 && !std::strcmp(argv[2], "bench")) {
        for (int r = 1; r <= 3; r++) {
            opt(e, "Rate", r, 4);
            run(e, {{55, 0.4}, {440, 0.2}, {3000, 0.1}}, 0.5, L, R);
            auto t0 = std::chrono::steady_clock::now();
            run(e, {{55, 0.4}, {440, 0.2}, {3000, 0.1}}, 10.0, L, R);
            double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            std::printf("bench rate %s: 10 s of stereo audio in %.3f s = %.2f %% of one x86 core\n", pdisplay(e, "Rate").c_str(), s, s * 10);
        }
        return 0;
    }

    CHECK(e->magic == 0x56737450, "magic");
    CHECK(e->numInputs == 2 && e->numOutputs == 2, "2 in / 2 out");
    CHECK(e->numPrograms == 5, "5 programs, got %d", e->numPrograms);
    CHECK(prog_name(e, 0) == "STAGE" && prog_name(e, 3) == "MASTER CHECK", "program names '%s' '%s'",
          prog_name(e, 0).c_str(), prog_name(e, 3).c_str());
    CHECK(pdisplay(e, "Rate") == "8 FPS" && pdisplay(e, "Range") == "50 DB" && pdisplay(e, "Top") == "+0 dB",
          "defaults: %s %s %s", pdisplay(e, "Rate").c_str(), pdisplay(e, "Range").c_str(), pdisplay(e, "Top").c_str());

    /* 1. audio passes bit-exact */
    {
        std::mt19937 rng(1);
        std::uniform_real_distribution<float> u(-1.5f, 1.5f);
        std::vector<float> il(128), ir(128), ol(128), orr(128);
        float *in[2] = {il.data(), ir.data()}, *out[2] = {ol.data(), orr.data()};
        bool same = true;
        for (int b = 0; b < 400; b++) {
            for (int i = 0; i < 128; i++) { il[i] = u(rng); ir[i] = u(rng); }
            e->processReplacing(e, in, out, 128);
            if (std::memcmp(il.data(), ol.data(), 512) || std::memcmp(ir.data(), orr.data(), 512)) same = false;
        }
        CHECK(same, "audio must pass bit-exact");
    }

    /* 2. pink noise at -14 dBFS RMS (a loud master) stands about two thirds up, evenly (PINK tilt);
     *    a tone lights its own bar; TOP moves the scale */
    opt(e, "Rate", 3, 4);    /* 12 FPS */
    pink(e, 0.2, 2.0);
    std::printf("pink noise -14 dBFS: "); dump(e);
    {
        int lo = 99, hi = -1, sum = 0;
        for (int b = 3; b <= 30; b++) { int v = bar_level(e, b); lo = std::min(lo, v); hi = std::max(hi, v); sum += v; }
        double mean = sum / 28.0;
        CHECK(mean > 11 && mean < 15.5, "pink noise mean step %.1f (about 13)", mean);
        CHECK(hi - lo <= 4, "pink noise should read evenly with PINK tilt: %d..%d", lo, hi);
    }
    opt(e, "Tilt", 0, 4);    /* FLAT: pink noise falls towards the highs, pivot at 1 kHz */
    pink(e, 0.2, 2.0);
    std::printf("pink noise, FLAT: "); dump(e);
    CHECK(bar_level(e, 8) >= bar_level(e, 18) + 2 && bar_level(e, 28) <= bar_level(e, 18) - 3, "FLAT tilt");
    CHECK(std::abs(bar_level(e, 18) - 13) <= 2, "FLAT keeps 1 kHz where PINK has it: %d", bar_level(e, 18));
    opt(e, "Tilt", 1, 4);
    pset(e, "Top", (float)((-12.0 + 30) / 36));   /* TOP -12 dB: everything about 5 steps higher */
    pink(e, 0.2, 1.5);
    CHECK(pdisplay(e, "Top") == "-12 dB", "top text '%s'", pdisplay(e, "Top").c_str());
    CHECK(bar_level(e, 18) >= 16, "TOP -12: 1 kHz bar higher, got %d", bar_level(e, 18));
    pset(e, "Top", (float)(30.0 / 36));
    for (auto ft : {std::pair<double, int>{1000, 18}, {100, 8}, {50, 5}, {10000, 28}}) {
        run(e, {{ft.first, 0.5}}, 1.5, L, R);
        CHECK(loudest(e) == ft.second, "%.0f Hz should light bar %d, got %d", ft.first, ft.second, loudest(e));
        CHECK(bar_level(e, ft.second) >= 12, "%.0f Hz at -6 dBFS: bar %d up, got %d", ft.first, ft.second, bar_level(e, ft.second));
    }
    run(e, {{1000, 0.5}}, 1.5, L, R);
    std::printf("1 kHz sine: "); dump(e);
    CHECK(bar_level(e, 8) <= 2 && bar_level(e, 30) <= 2, "far bars dark: %d %d", bar_level(e, 8), bar_level(e, 30));

    /* 3. bars are reported with automate only, at most RATE times a second each, no UpdateDisplay */
    g_automate = 0; g_update = 0; g_auto_log.clear();
    run(e, {{60, 0.5}, {2000, 0.3}}, 2.0, L, R);
    int bar_reports = 0;
    for (auto &a : g_auto_log) { char nm[64] = {0}; e->dispatcher(e, 8, a.first, 0, nm, 0); if (!std::strncmp(nm, "Band ", 5)) bar_reports++; }
    std::printf("2 s at 12 FPS: %d bar reports, %d UpdateDisplay\n", bar_reports, g_update);
    CHECK(g_update == 0, "no UpdateDisplay from the bars, got %d", g_update);
    CHECK(bar_reports > 0 && bar_reports <= 31 * 25, "bar reports %d", bar_reports);

    /* 4. VIEW: a tone only on the right */
    {
        auto right_only = [&](double sec) { run(e, {{1000, 0.5}}, sec, L, R, -1.0); };   /* side=-1: left 0, right 2x */
        opt(e, "View", 1, 5); right_only(1.0);
        int left = bar_level(e, 18);
        opt(e, "View", 2, 5); right_only(1.0);
        int right = bar_level(e, 18);
        opt(e, "View", 4, 5); right_only(1.0);
        int side = bar_level(e, 18);
        opt(e, "View", 0, 5);
        std::printf("right-only tone: LEFT %d, RIGHT %d, SIDE %d\n", left, right, side);
        CHECK(left <= 1 && right >= 18 && side >= 14, "VIEW channels");
    }

    /* 5. FREEZE holds, RATE OFF clears and goes quiet */
    run(e, {{250, 0.5}}, 1.0, L, R);
    opt(e, "Freeze", 1, 2);
    run(e, {}, 1.5, L, R);
    CHECK(bar_level(e, 12) >= 15, "FREEZE should hold the 250 Hz bar, got %d", bar_level(e, 12));
    opt(e, "Freeze", 0, 2);
    run(e, {}, 2.0, L, R);
    CHECK(bar_level(e, 12) <= 1, "after FREEZE off and silence the bar falls, got %d", bar_level(e, 12));
    run(e, {{250, 0.5}}, 1.0, L, R);
    opt(e, "Rate", 0, 4);
    run(e, {{250, 0.5}}, 0.5, L, R);
    CHECK(bar_level(e, 12) == 0, "RATE OFF: bars at 0, got %d", bar_level(e, 12));
    g_automate = 0;
    run(e, {{250, 0.5}}, 1.0, L, R);
    CHECK(g_automate == 0, "RATE OFF: no reports, got %d", g_automate);
    opt(e, "Rate", 2, 4);

    /* 6. a touch on a bar is ignored and the bar is put back */
    run(e, {{1000, 0.5}}, 1.0, L, R);
    int was = bar_level(e, 18);
    pset(e, "Band 18", 0.0f);
    CHECK(bar_level(e, 18) == was, "host write to a bar must be ignored");

    /* 7. presets and chunk */
    e->dispatcher(e, 2, 0, 3, nullptr, 0);   /* MASTER CHECK */
    CHECK(pdisplay(e, "Mode") == "MASTERING" && pdisplay(e, "Rate") == "4 FPS", "MASTER CHECK: %s %s",
          pdisplay(e, "Mode").c_str(), pdisplay(e, "Rate").c_str());
    opt(e, "Tilt", 3, 4);
    void *chunk = nullptr;
    intptr_t len = e->dispatcher(e, 23, 0, 0, &chunk, 0);
    std::string saved((const char *)chunk, (size_t)len);
    CHECK(saved.find("b01=") == std::string::npos, "bars are not saved");
    opt(e, "Tilt", 1, 4);
    e->dispatcher(e, 24, 0, (intptr_t)saved.size(), (void *)saved.data(), 0);
    CHECK(pdisplay(e, "Tilt") == "BROWN", "chunk restore: '%s'", pdisplay(e, "Tilt").c_str());
    e->dispatcher(e, 2, 0, 0, nullptr, 0);

    /* 8. loud and silent input stay finite; every FFT size works */
    for (int f = 0; f < 4; f++) { opt(e, "FFT", f, 4); run(e, {{40, 4.0}, {15000, 2.0}}, 0.5, L, R); }
    run(e, {}, 2.0, L, R);
    CHECK(!g_bad, "NaN, Inf or denormal in the output");

    e->dispatcher(e, 1, 0, 0, nullptr, 0);
    std::printf(g_fails ? "FAILED (%d)\n" : "PASSED\n", g_fails);
    return g_fails ? 1 : 0;
}
