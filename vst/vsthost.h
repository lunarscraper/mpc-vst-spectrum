/* vsthost.h - the bits of a VST2 host the offline tests need (hand-written ABI, as fxshell.h). Test code only. */
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <dlfcn.h>

struct AEffect;
typedef intptr_t (*audioMasterCallback)(AEffect *, int32_t, int32_t, intptr_t, void *, float);
struct AEffect {
    int32_t magic;
    intptr_t (*dispatcher)(AEffect *, int32_t, int32_t, intptr_t, void *, float);
    void (*process)(AEffect *, float **, float **, int32_t);
    void (*setParameter)(AEffect *, int32_t, float);
    float (*getParameter)(AEffect *, int32_t);
    int32_t numPrograms, numParams, numInputs, numOutputs, flags;
    intptr_t resvd1, resvd2;
    int32_t initialDelay, realQualities, offQualities;
    float ioRatio;
    void *object, *user;
    int32_t uniqueID, version;
    void (*processReplacing)(AEffect *, float **, float **, int32_t);
    void (*processDoubleReplacing)(AEffect *, double **, double **, int32_t);
    char future[56];
};

static int g_automate = 0, g_update = 0;
static std::vector<std::pair<int, float>> g_auto_log;
static intptr_t host_cb(AEffect *, int32_t op, int32_t idx, intptr_t, void *, float opt) {
    if (op == 0) { g_automate++; g_auto_log.push_back({idx, opt}); }
    if (op == 42) g_update++;
    if (op == 1) return 2400;   /* audioMasterVersion */
    return 0;
}
static int g_fails = 0;
#define CHECK(c, ...) do { if (!(c)) { g_fails++; std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); } } while (0)

static AEffect *load_plugin(const char *path) {
    void *h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!h) { std::printf("FAIL: dlopen %s: %s\n", path, dlerror()); return nullptr; }
    auto entry = (AEffect * (*)(audioMasterCallback)) dlsym(h, "VSTPluginMain");
    if (!entry) { std::printf("FAIL: no VSTPluginMain\n"); return nullptr; }
    AEffect *e = entry(host_cb);
    e->dispatcher(e, 0, 0, 0, nullptr, 0);          /* effOpen */
    e->dispatcher(e, 10, 0, 0, nullptr, 44100.0f);  /* effSetSampleRate */
    e->dispatcher(e, 11, 0, 128, nullptr, 0);       /* effSetBlockSize */
    e->dispatcher(e, 12, 0, 1, nullptr, 0);         /* effMainsChanged on */
    return e;
}
static int pidx(AEffect *e, const char *name) {
    char buf[64];
    for (int i = 0; i < e->numParams; i++) { buf[0] = 0; e->dispatcher(e, 8, i, 0, buf, 0); if (!std::strcmp(buf, name)) return i; }
    std::printf("FAIL: no parameter %s\n", name); g_fails++; return 0;
}
static std::string pdisplay(AEffect *e, const char *name) { char buf[64] = {0}; e->dispatcher(e, 7, pidx(e, name), 0, buf, 0); return buf; }
static void pset(AEffect *e, const char *name, float n) { e->setParameter(e, pidx(e, name), n); }
static float pget(AEffect *e, const char *name) { return e->getParameter(e, pidx(e, name)); }
static std::string prog_name(AEffect *e, int k) { char buf[64] = {0}; e->dispatcher(e, 29, k, 0, buf, 0); return buf; }

static const int BS = 128;
static bool g_bad = false;   /* NaN, Inf or a denormal in the output */
/* sum of sines through the effect; returns both outputs */
static void run(AEffect *e, const std::vector<std::pair<double, double>> &tones, double sec, std::vector<float> &ol_all, std::vector<float> &or_all,
                double side = 0.0) {
    std::vector<float> il(BS), ir(BS), ol(BS), orr(BS);
    float *in[2] = {il.data(), ir.data()}, *out[2] = {ol.data(), orr.data()};
    static double t = 0;
    ol_all.clear(); or_all.clear();
    for (int b = 0; b < (int)(sec * 44100 / BS); b++) {
        for (int i = 0; i < BS; i++) {
            double s = 0;
            for (auto &tn : tones) s += tn.second * std::sin(2 * M_PI * tn.first * t);
            il[i] = (float)(s * (1 + side)); ir[i] = (float)(s * (1 - side));
            t += 1.0 / 44100;
        }
        e->processReplacing(e, in, out, BS);
        for (int i = 0; i < BS; i++) {
            for (float v : {ol[i], orr[i]}) if (!std::isfinite(v) || std::fpclassify(v) == FP_SUBNORMAL) g_bad = true;
            ol_all.push_back(ol[i]); or_all.push_back(orr[i]);
        }
    }
}
/* amplitude of frequency f in a[t0..] (Goertzel) */
static double tone_amp(const std::vector<float> &a, double f, double t0) {
    size_t i0 = (size_t)(t0 * 44100), i1 = a.size();
    double w = 2 * M_PI * f / 44100, c = std::cos(w), s1 = 0, s2 = 0;
    for (size_t i = i0; i < i1; i++) { double s0 = a[i] + 2 * c * s1 - s2; s2 = s1; s1 = s0; }
    return 2 * std::sqrt(std::max(0.0, s1 * s1 + s2 * s2 - 2 * c * s1 * s2)) / (double)(i1 - i0);
}
static double db(double x) { return 20 * std::log10(std::max(x, 1e-12)); }
