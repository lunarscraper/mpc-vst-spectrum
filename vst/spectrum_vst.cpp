/* =============================================================================
 * spectrum_vst.cpp - Spectrum: LSP's Spectrum Analyzer (lsp-plugins-spectrum-analyzer, x2 = stereo) as a VST2 insert
 * effect for the MPC OS plugin host (Force, MPC Live/One/X/Key), armhf. Audio passes through untouched (float).
 *
 * The analysis is LSP's own plugin class, unchanged (lsp/plugin/src/main/plug/spectrum_analyzer.cpp), with its FFT,
 * window, envelope (tilt) and reactivity. It writes its spectrum into a mesh (640 points, 10 Hz .. 24 kHz) as it does
 * for LSP's own UI; this file reads that mesh RATE times a second and turns it into 31 third-octave bars
 * (ISO centres 20 Hz .. 20 kHz, the loudest point in each band), each a whole number 0..20 in parameters b01..b31.
 * MPC draws those as filmstrip meters (skin_post.py adds them to the page) and moves them when the shell reports a
 * changed value (audioMasterAutomate only, no UpdateDisplay). Only bars whose step changed are reported.
 * Level: LSP's mesh value in dB plus a calibration (CAL, TILT_CAL, FFT_CAL below), measured offline with pink noise:
 *   - LSP's tilt envelopes pivot near 100 Hz; TILT_CAL moves the pivot to 1 kHz, so changing TILT turns the bars
 *     around the middle instead of lifting the whole display;
 *   - a bigger FFT spreads noise over more bins (3 dB lower per doubling); FFT_CAL evens that out;
 *   - CAL: pink noise at -14 dBFS RMS (a loud master) reads about -17 dB, two thirds up a 50 dB RANGE at TOP 0.
 *   The bars show the loudest point of each band, as LSP's curve would; a pure high tone between two mesh points
 *   reads lower than it is (the mesh samples LSP's FFT), music and noise read steadily.
 * TOP is the level at the top of the bars, RANGE the span down to the bottom.
 *
 * GNU Lesser General Public License v3 or later (see ../LICENSE), as LSP. Unofficial port; LSP Project:
 * https://lsp-plug.in, (C) Vladimir Sadovnikov and the LSP contributors. Not affiliated with the LSP Project.
 * ========================================================================== */
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "params.h"
#include "lsp_host.h"
#include "fxhooks.h"
#include <private/plugins/spectrum_analyzer.h>
#include <private/meta/spectrum_analyzer.h>

using namespace lsp;

namespace {

const int NBARS = 31, LEVELS = 20;
const float CENTRES[NBARS] = {20, 25, 31.5f, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000,
                              1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000, 20000};
enum { RATE, VIEW, MODE, REACT, TOP, RANGE, TILT, FFT, FREEZE, NCTL };
const float CAL = 10.0f;                                  /* dB, see the header */
const float TILT_CAL[4] = {10.0f, 0.0f, -5.0f, -10.0f};   /* FLAT, PINK, 4.5 DB, BROWN: pivot at 1 kHz */
const float FFT_CAL[4] = {-6.0f, -3.0f, 0.0f, 3.0f};      /* 1024 .. 8192 points */
const char *CTL_KEYS[NCTL] = {"rate", "view", "mode", "react", "top", "range", "tilt", "fft", "freeze"};

int key_index(const char *key) {
    for (int i = 0; i < NPARAMS; i++) if (!std::strcmp(PARAMS[i].key, key)) return i;
    return -1;
}

struct Engine {
    lsphost::LspHost host;
    lsphost::Mesh mesh;
    int ctl[NCTL];
    int bar[NBARS];             /* VST index of b01..b31 */
    int lo[NBARS], hi[NBARS];   /* mesh point range of each bar (into the 640 data points) */
    float sr = 44100;
    int fps = 8, view = 0;
    float top = 0, range = 50, cal = 0;
    long frame = 0, countdown = 0;
    float level[NBARS] = {0};   /* bar steps 0..LEVELS */
    bool mapped = false;
    std::vector<float> scratch;   /* LSP's own (unused) copy of the audio */
};

void *create(const char *) {
    lsphost::dsp_init_once();
    Engine *e = new Engine();
    for (int i = 0; i < NCTL; i++) e->ctl[i] = key_index(CTL_KEYS[i]);
    for (int b = 0; b < NBARS; b++) {
        char k[16];
        std::snprintf(k, sizeof k, "b%02d", b + 1);
        e->bar[b] = key_index(k);
    }
    const meta::plugin_t *meta = &meta::spectrum_analyzer_x2;
    e->host.bind(new plugins::spectrum_analyzer(meta), meta);
    lsphost::LspPort *spd = e->host.port("spd");   /* rows = step, items = start in the port's metadata */
    e->mesh.alloc((size_t)spd->metadata()->step, (size_t)spd->metadata()->start);
    spd->buf = e->mesh.m;
    e->host.mod->ui_activated();   /* LSP only fills the mesh while its UI is shown */
    return e;
}
void destroy(void *p) { delete (Engine *)p; }
void rate(void *p, float sr) { Engine *e = (Engine *)p; e->sr = sr; e->host.sample_rate(sr); }

void apply(void *p, const float *v) {
    Engine *e = (Engine *)p;
    lsphost::LspHost &h = e->host;
    auto val = [&](int c) { return v[e->ctl[c]]; };
    static const int FPS[] = {0, 4, 8, 12};
    static const int ENV[] = {2, 3, 5, 4};   /* LSP envelopes (compensation): White, Pink, 4.5 dB/oct fall-off, Brown */
    static const float RNG[] = {30, 40, 50, 60};
    e->fps = FPS[std::min(3, std::max(0, (int)val(RATE)))];
    e->view = (int)val(VIEW);
    e->top = val(TOP);
    e->range = RNG[std::min(3, std::max(0, (int)val(RANGE)))];
    e->cal = CAL + TILT_CAL[std::min(3, std::max(0, (int)val(TILT)))] + FFT_CAL[std::min(3, std::max(0, (int)val(FFT)))];
    const bool on = e->fps > 0;
    h.set("on_0", on ? 1.0f : 0.0f);   /* RATE OFF: no analysis at all */
    h.set("on_1", on ? 1.0f : 0.0f);
    h.set("mode", val(MODE) >= 0.5f ? 1.0f : 0.0f);   /* 0 Analyzer, 1 Mastering */
    h.set("ms", (e->view == 3 || e->view == 4) ? 1.0f : 0.0f);
    h.set("react", val(REACT) / 1000.0f);
    h.set("env", (float)ENV[std::min(3, std::max(0, (int)val(TILT)))]);
    h.set("tol", val(FFT));   /* 0..3 = 1024..8192 points */
    h.set("freeze", val(FREEZE) >= 0.5f ? 1.0f : 0.0f);
}

/* which mesh points belong to which third-octave band (the frequencies are in row 0) */
void map_bars(Engine *e) {
    const float *f = e->mesh.m->pvData[0] + 2;
    const int n = (int)meta::spectrum_analyzer::MESH_POINTS;
    for (int b = 0; b < NBARS; b++) {
        const float flo = CENTRES[b] * 0.8909f, fhi = CENTRES[b] * 1.1225f;   /* 2^(-1/6), 2^(1/6) */
        int a = n, z = -1;
        for (int i = 0; i < n; i++) if (f[i] >= flo && f[i] < fhi) { a = std::min(a, i); z = std::max(z, i); }
        if (z < a) {   /* no point inside: take the nearest one */
            int best = 0;
            for (int i = 1; i < n; i++) if (std::fabs(f[i] - CENTRES[b]) < std::fabs(f[best] - CENTRES[b])) best = i;
            a = z = best;
        }
        e->lo[b] = a;
        e->hi[b] = z;
    }
    e->mapped = true;
}

void read_mesh(Engine *e) {
    plug::mesh_t *m = e->mesh.m;
    if (!e->mapped) map_bars(e);
    const float *a, *b = nullptr;
    switch (e->view) {   /* rows: 1 = channel 1, 2 = channel 2 (L/R, or M/S with "ms" on) */
    case 1: case 3: a = m->pvData[1] + 2; break;
    case 2: case 4: a = m->pvData[2] + 2; break;
    default: a = m->pvData[1] + 2; b = m->pvData[2] + 2; break;
    }
    for (int k = 0; k < NBARS; k++) {
        float peak = 0;
        for (int i = e->lo[k]; i <= e->hi[k]; i++) {
            float x = a[i];
            if (b && b[i] > x) x = b[i];
            if (x > peak) peak = x;
        }
        const float db = peak > 1e-9f ? 20.0f * std::log10(peak) + e->cal : -200.0f;
        float steps = std::floor((db - (e->top - e->range)) / e->range * LEVELS + 0.5f);
        e->level[k] = std::min((float)LEVELS, std::max(0.0f, steps));
    }
}

void process(void *p, const float *il, const float *ir, float *ol, float *orr, int n) {
    Engine *e = (Engine *)p;
    lsphost::LspHost &h = e->host;
    std::memcpy(ol, il, sizeof(float) * n);   /* audio passes untouched; LSP writes its own copy into scratch */
    std::memcpy(orr, ir, sizeof(float) * n);
    if ((int)e->scratch.size() < 2 * n) e->scratch.resize(2 * n);   /* first block only: MPC's block size is fixed */
    float *x0 = e->scratch.data(), *x1 = x0 + n;
    h.port("in0")->buf = (void *)il;
    h.port("in1")->buf = (void *)ir;
    h.port("out0")->buf = x0;
    h.port("out1")->buf = x1;
    h.sync();
    h.mod->process((size_t)n);
    if (e->fps <= 0) return;
    e->countdown -= n;
    if (e->countdown > 0) return;
    if (!e->mesh.m->containsData()) return;   /* LSP has not written a new spectrum yet: try next block */
    e->countdown += (long)(e->sr / e->fps);
    if (e->countdown < 0) e->countdown = 0;
    read_mesh(e);
    e->mesh.m->cleanup();   /* ask LSP for the next one */
}

void live(void *p, float *val) {
    Engine *e = (Engine *)p;
    for (int k = 0; k < NBARS; k++) if (e->bar[k] >= 0) val[e->bar[k]] = e->fps > 0 ? e->level[k] : 0.0f;
}
int is_live(int i) { return PARAMS[i].key[0] == 'b' && PARAMS[i].key[1] >= '0' && PARAMS[i].key[1] <= '9'; }
int log_scale(int i) { return !std::strcmp(PARAMS[i].key, "react"); }

int text(int i, float v, char *buf, int len) {
    const char *k = PARAMS[i].key;
    if (!std::strcmp(k, "react")) { std::snprintf(buf, len, "%.0f ms", v); return 1; }
    if (!std::strcmp(k, "top")) { std::snprintf(buf, len, "%+d dB", (int)v); return 1; }
    return 0;
}

} // namespace

static const FxHooks HOOKS = {create, destroy, rate, apply, process, live, text, log_scale, is_live};
#include "fxshell.h"
