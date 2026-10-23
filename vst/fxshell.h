/* =============================================================================
 * fxshell.h - a hand-written VST2 insert-effect shell for the MPC OS plugin host (Force, MPC Live/One/X/Key),
 * with float audio in and out (no SDK, no wrapper library).
 *
 * Why not mpc-vst-plugins' shared wrapper/vst2_wrap.c: its engine contract is int16 audio, which clips a hot
 * master bus at 0 dBFS and cuts it to 16 bits. A master-bus processor and an analyzer that sits on the master
 * must pass float audio untouched, so this shell keeps the host's float buffers. It follows the rules that the
 * shared wrapper's fix of 2026-10-21 (mpc-vst-plugins #486-#489, docs/HANDOFF_WRAPPER_FIX.md) and its notes set:
 *   - a host write (touch, Q-Link, automation) never sends audioMasterUpdateDisplay, so Q-Links keep working on
 *     the automation screen; values that change for another reason (a preset, a slot load, a project load) are
 *     reported with audioMasterAutomate from processReplacing, never from inside the host's own call;
 *   - at most one audioMasterUpdateDisplay per 50 ms, and only when names or many values changed;
 *   - engine-made values (live meters) are reported with audioMasterAutomate only, no UpdateDisplay (as the shared
 *     wrapper's DISPLAY_REV_NO_UPDATE): MPC moves meters on that, and its PRESET menu stays open;
 *   - option lists and whole numbers step once per data-wheel click or Q-Link event (settle);
 *   - the parameter list comes from mpc-vst-plugins' tools/gen_vst.py (params.h, params.lock: append only);
 *   - VST programs, listed in MPC's PRESET menu; a re-pick of the current program is ignored (JUCE re-picks at
 *     load). With "slot", "load" and "save" parameters: 32 slots, 1..N starting as the factory presets of
 *     presets.json; SAVE writes a slot to <plugin folder>/slots/NN.txt (kept by the installer as user data), LOAD
 *     reads it back. Without them: just the factory presets.
 *
 * The port includes fxhooks.h, defines `static const FxHooks HOOKS = {...}` and then includes this file.
 * Part of mpc-vst-mbdyna / mpc-vst-spectrum (lunarscraper). GNU LGPL v3 or later.
 * ========================================================================== */
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#include <sys/stat.h>

#include "params.h"
#include "popup.h"        /* mpc-vst-plugins wrapper/popup.h, copied into build/ by build.sh */
#include "plugin_dir.h"   /* mpc-vst-plugins wrapper/plugin_dir.h, the same */

/* ---- VST2 ABI (hand-written; no Steinberg SDK) ---------------------------- */
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
enum {
    effOpen = 0, effClose = 1, effSetProgram = 2, effGetProgram = 3, effSetProgramName = 4, effGetProgramName = 5,
    effGetParamLabel = 6, effGetParamDisplay = 7, effGetParamName = 8, effSetSampleRate = 10, effSetBlockSize = 11,
    effMainsChanged = 12, effGetChunk = 23, effSetChunk = 24, effProcessEvents = 25, effCanBeAutomated = 26,
    effGetProgramNameIndexed = 29, effGetPlugCategory = 35, effGetEffectName = 45, effGetVendorString = 47,
    effGetProductString = 48, effGetVendorVersion = 49, effCanDo = 51, effGetVstVersion = 58,
    effBeginSetProgram = 67, effEndSetProgram = 68,
};
enum { audioMasterAutomate = 0, audioMasterUpdateDisplay = 42 };
enum { effFlagsCanReplacing = 1 << 4, effFlagsProgramChunks = 1 << 5 };

#include "fxhooks.h"

#define FX_NSLOTS 32   /* storage; the plugin reports g_nslots of them */

namespace fxs {

struct Slot { std::string name; bool user = false; float norm[NPARAMS]; };

struct Plugin {
    AEffect fx;
    audioMasterCallback master = nullptr;
    void *dsp = nullptr;
    std::atomic<float> norm[NPARAMS];
    std::atomic<int> notify[NPARAMS];
    float open[NPARAMS] = {0};
    volatile int release[NPARAMS] = {0};
    std::atomic<bool> dirty{true};
    std::atomic<bool> need_update{false};
    int update_wait = 0;
    float live_val[NPARAMS] = {0};
    float live_sent[NPARAMS];
    float val[NPARAMS];
    int prog = 0;
    Slot slots[FX_NSLOTS];
    std::string dir;          /* plugin folder, "" if unknown */
    std::mutex slot_lock;
    std::vector<uint8_t> chunk;
    std::vector<float> in_l, in_r;
};

static int IDX_SLOT = -1, IDX_LOAD = -1, IDX_SAVE = -1;
/* programs MPC lists: 32 slots with a SLOT/LOAD/SAVE page, else just the factory presets (or one) */
static int g_nslots = FX_NSLOTS;

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static void copy_str(void *dst, const char *s, size_t max) {
    std::strncpy((char *)dst, s, max - 1);
    ((char *)dst)[max - 1] = 0;
}
static int param_index(const char *key) {
    for (int i = 0; i < NPARAMS; i++) if (!std::strcmp(PARAMS[i].key, key)) return i;
    return -1;
}
static bool is_log(int i) { return HOOKS.log_scale && HOOKS.log_scale(i) && PARAMS[i].min > 0 && PARAMS[i].max > PARAMS[i].min; }
static bool is_live(int i) { return HOOKS.is_live && HOOKS.is_live(i); }
static bool is_whole(int i) { return PARAMS[i].int_display && !PARAMS[i].nopts; }
static bool saved(int i) { return !PARAMS[i].momentary && !popup_is(i) && !is_live(i) && i != IDX_SLOT; }

/* normalized host value <-> the parameter's own units */
static float to_val(int i, float n) {
    const param_t *p = &PARAMS[i];
    n = clamp01(n);
    if (p->nopts) return (float)std::lround(n * (p->nopts - 1));
    float v = is_log(i) ? p->min * std::pow(p->max / p->min, n) : p->min + (p->max - p->min) * n;
    return is_whole(i) ? (float)std::lround(v) : v;
}
static float to_norm(int i, float v) {
    const param_t *p = &PARAMS[i];
    if (p->nopts) return p->nopts > 1 ? clamp01(v / (p->nopts - 1)) : 0.0f;
    if (p->max <= p->min) return 0.0f;
    if (is_log(i)) return clamp01(std::log(std::max(v, p->min) / p->min) / std::log(p->max / p->min));
    return clamp01((v - p->min) / (p->max - p->min));
}
/* steps of an option list or a whole-number parameter (0 = continuous) */
static int steps_of(int i) {
    const param_t *p = &PARAMS[i];
    if (p->nopts > 1) return p->nopts - 1;
    if (is_whole(i) && !is_log(i)) return (int)std::lround(p->max - p->min);
    return 0;
}

/* ---- slots (VST programs) ------------------------------------------------- */
static void slot_path(Plugin *w, int k, char *buf, size_t n) { std::snprintf(buf, n, "%s/slots/%02d.txt", w->dir.c_str(), k + 1); }

static void slots_init(Plugin *w) {
    for (int k = 0; k < g_nslots; k++) {
        Slot &s = w->slots[k];
        for (int i = 0; i < NPARAMS; i++) s.norm[i] = PARAMS[i].def;
#if defined(NPRESETS)
        const preset_t &pr = PRESETS[k < NPRESETS ? k : 0];
        for (int j = 0; j < pr.n; j++) s.norm[pr.values[j].param] = to_norm(pr.values[j].param, (float)std::atof(pr.values[j].value));
        char nm[40];
        if (k < NPRESETS && IDX_SLOT < 0) std::snprintf(nm, sizeof nm, "%s", pr.name);
        else if (k < NPRESETS) std::snprintf(nm, sizeof nm, "%02d %s", k + 1, pr.name);
        else std::snprintf(nm, sizeof nm, "%02d EMPTY", k + 1);
        s.name = nm;
#else
        char nm[16];
        std::snprintf(nm, sizeof nm, "%02d EMPTY", k + 1);
        s.name = nm;
#endif
        s.user = false;
        if (w->dir.empty() || IDX_SLOT < 0) continue;
        char path[600];
        slot_path(w, k, path, sizeof path);
        FILE *f = std::fopen(path, "r");
        if (!f) continue;
        char line[256];
        while (std::fgets(line, sizeof line, f)) {
            line[std::strcspn(line, "\r\n")] = 0;
            char *eq = std::strchr(line, '=');
            if (!eq) continue;
            *eq = 0;
            if (!std::strcmp(line, "name")) { s.name = eq + 1; continue; }
            int i = param_index(line);
            if (i >= 0 && saved(i)) s.norm[i] = clamp01(std::atol(eq + 1) / 100000.0f);
        }
        std::fclose(f);
        s.user = true;
    }
}

static bool slot_save(Plugin *w, int k) {
    if (w->dir.empty()) return false;
    char path[600], tmp[620];
    std::snprintf(path, sizeof path, "%s/slots", w->dir.c_str());
    mkdir(path, 0755);
    slot_path(w, k, path, sizeof path);
    std::snprintf(tmp, sizeof tmp, "%s.new", path);
    FILE *f = std::fopen(tmp, "w");
    if (!f) return false;
    char nm[40];
    std::snprintf(nm, sizeof nm, "%02d USER", k + 1);
    std::fprintf(f, "name=%s\n", nm);
    Slot &s = w->slots[k];
    for (int i = 0; i < NPARAMS; i++) {
        if (!saved(i)) continue;
        s.norm[i] = w->norm[i].load();
        std::fprintf(f, "%s=%ld\n", PARAMS[i].key, std::lround(s.norm[i] * 100000.0f));
    }
    bool ok = std::fclose(f) == 0 && std::rename(tmp, path) == 0;
    if (ok) { s.name = nm; s.user = true; }
    return ok;
}

/* Sets every saved parameter from slot k; the host hears it from processReplacing. */
static void slot_load(Plugin *w, int k) {
    k = clampi(k, 0, g_nslots - 1);
    for (int i = 0; i < NPARAMS; i++) {
        if (!saved(i)) continue;
        w->norm[i].store(w->slots[k].norm[i]);
        w->notify[i].store(1);
    }
    w->prog = k;
    if (IDX_SLOT >= 0) { w->norm[IDX_SLOT].store(to_norm(IDX_SLOT, (float)(k + 1))); w->notify[IDX_SLOT].store(1); }
    w->dirty.store(true);
    w->need_update.store(true);
}

/* ---- audio ------------------------------------------------------------------ */
struct NoDenormals {
#if defined(__arm__) && defined(__ARM_FP)
    uint32_t old = 0;
    NoDenormals() { asm volatile("vmrs %0, fpscr" : "=r"(old)); asm volatile("vmsr fpscr, %0" : : "r"(old | (1u << 24))); }
    ~NoDenormals() { asm volatile("vmsr fpscr, %0" : : "r"(old)); }
#elif defined(__aarch64__)
    uint64_t old = 0;
    NoDenormals() { asm volatile("mrs %0, fpcr" : "=r"(old)); asm volatile("msr fpcr, %0" : : "r"(old | (1ull << 24))); }
    ~NoDenormals() { asm volatile("msr fpcr, %0" : : "r"(old)); }
#elif defined(__x86_64__) || defined(__i386__)
    unsigned old = __builtin_ia32_stmxcsr();
    NoDenormals() { __builtin_ia32_ldmxcsr(old | 0x8040); }
    ~NoDenormals() { __builtin_ia32_ldmxcsr(old); }
#endif
};

static void processReplacing(AEffect *e, float **in, float **out, int32_t n) {
    Plugin *w = (Plugin *)e->object;
    NoDenormals nd;
    if (w->dirty.exchange(false)) {
        for (int i = 0; i < NPARAMS; i++) w->val[i] = to_val(i, w->norm[i].load());
        HOOKS.apply(w->dsp, w->val);
    }
    if (n > 0) {
        if ((int)w->in_l.size() < n) { w->in_l.resize(n); w->in_r.resize(n); }   /* first block only: MPC's is fixed */
        const bool have = in && in[0] && in[1];
        for (int j = 0; j < n; j++) { w->in_l[j] = have ? in[0][j] : 0.0f; w->in_r[j] = have ? in[1][j] : 0.0f; }
        HOOKS.process(w->dsp, w->in_l.data(), w->in_r.data(), out[0], out[1], n);
    }
    if (HOOKS.live) {   /* meters: report what moved, nothing else (no UpdateDisplay) */
        HOOKS.live(w->dsp, w->live_val);
        for (int i = 0; i < NPARAMS; i++) {
            if (!is_live(i)) continue;
            float nv = to_norm(i, w->live_val[i]);
            if (std::fabs(nv - w->live_sent[i]) < 1e-4f) continue;
            w->live_sent[i] = nv;
            w->norm[i].store(nv);
            w->master(&w->fx, audioMasterAutomate, i, 0, 0, nv);
        }
    }
    for (int i = 0; i < NPARAMS; i++) {
        if (w->release[i]) { w->release[i] = 0; w->master(&w->fx, audioMasterAutomate, i, 0, 0, 0.0f); }
        if (w->notify[i].exchange(0)) w->master(&w->fx, audioMasterAutomate, i, 0, 0, w->norm[i].load());
    }
    if (w->update_wait > 0) w->update_wait -= n;
    if (w->update_wait <= 0 && w->need_update.exchange(false)) {
        w->update_wait = 2205;   /* 50 ms */
        w->master(&w->fx, audioMasterUpdateDisplay, 0, 0, 0, 0.0f);
    }
}
static void process_acc(AEffect *e, float **in, float **out, int32_t n) {   /* legacy accumulating call; MPC uses processReplacing */
    float l[256], r[256];
    for (int32_t at = 0; at < n; at += 256) {
        int32_t m = n - at < 256 ? n - at : 256;
        float *o[2] = {l, r}, *ii[2] = {in[0] + at, in[1] + at};
        processReplacing(e, ii, o, m);
        for (int32_t j = 0; j < m; j++) { out[0][at + j] += l[j]; out[1][at + j] += r[j]; }
    }
}

/* ---- parameters ------------------------------------------------------------- */
static void setParameter(AEffect *e, int32_t i, float n) {
    Plugin *w = (Plugin *)e->object;
    if (i < 0 || i >= NPARAMS) return;
    const param_t *p = &PARAMS[i];
    if (popup_set(w->open, i, n)) { if (w->open[i] > 0.5f) popup_close_others(w->open, w->release, i); return; }
    if (is_live(i)) { w->notify[i].store(1); return; }   /* display only: put the meter back */
    if (p->momentary) {
        if (n > 0.5f) {
            std::lock_guard<std::mutex> g(w->slot_lock);
            const int k = IDX_SLOT >= 0 ? clampi((int)to_val(IDX_SLOT, w->norm[IDX_SLOT].load()) - 1, 0, g_nslots - 1) : w->prog;
            if (i == IDX_LOAD) slot_load(w, k);
            else if (i == IDX_SAVE && slot_save(w, k)) { w->prog = k; w->need_update.store(true); }   /* the PRESET menu re-reads names */
            w->release[i] = 1;
        }
        return;
    }
    n = clamp01(n);
    bool nudge = false;
    const int st = steps_of(i);
    if (st > 0) {   /* a wheel click or Q-Link event lands between two steps: move one step that way */
        const float pos = n * st, cur = w->norm[i].load() * st;
        if (std::fabs(pos - std::round(pos)) > 0.001f && std::fabs(pos - cur) < 1.0f) {
            n = (float)clampi((int)std::lround(cur) + (pos > cur ? 1 : -1), 0, st) / st;
            nudge = true;
        }
    }
    w->norm[i].store(n);
    w->dirty.store(true);
    if (!nudge) popup_picked(w->open, w->release, i);
}
static float getParameter(AEffect *e, int32_t i) {
    Plugin *w = (Plugin *)e->object;
    if (i < 0 || i >= NPARAMS) return 0.0f;
    if (popup_is(i)) return w->open[i];
    return w->norm[i].load();
}

static void format_value(int i, float n, char *buf, size_t size) {
    const param_t *p = &PARAMS[i];
    const float v = to_val(i, n);
    if (HOOKS.text && HOOKS.text(i, v, buf, (int)size) > 0) return;
    if (p->nopts) std::snprintf(buf, size, "%s", p->opts[clampi((int)v, 0, p->nopts - 1)]);
    else if (is_whole(i)) std::snprintf(buf, size, "%d%s%s", (int)v, p->unit[0] ? " " : "", p->unit);
    else std::snprintf(buf, size, std::fabs(v) < 10 ? "%.2f%s%s" : std::fabs(v) < 100 ? "%.1f%s%s" : "%.0f%s%s", v, p->unit[0] ? " " : "", p->unit);
}

/* The chunk keeps each knob's full position (x 100000 as an integer: no decimal point, no locale trouble). */
static intptr_t get_chunk(Plugin *w, void **ptr) {
    std::string t = "FXS1;";
    char buf[80];
    std::snprintf(buf, sizeof buf, "prog=%d;", w->prog);
    t += buf;
    for (int i = 0; i < NPARAMS; i++) {
        if (!saved(i) && i != IDX_SLOT) continue;
        std::snprintf(buf, sizeof buf, "%s=%ld;", PARAMS[i].key, std::lround(w->norm[i].load() * 100000.0f));
        t += buf;
    }
    w->chunk.assign(t.begin(), t.end());
    *ptr = w->chunk.data();
    return (intptr_t)w->chunk.size();
}
static intptr_t set_chunk(Plugin *w, const void *data, intptr_t len) {
    std::string t((const char *)data, (size_t)len);
    if (t.compare(0, 5, "FXS1;")) return 0;
    for (size_t pos = 5; pos < t.size();) {
        size_t semi = t.find(';', pos);
        if (semi == std::string::npos) break;
        std::string kv = t.substr(pos, semi - pos);
        pos = semi + 1;
        size_t eq = kv.find('=');
        if (eq == std::string::npos) continue;
        std::string k = kv.substr(0, eq);
        if (k == "prog") { w->prog = clampi(std::atoi(kv.c_str() + eq + 1), 0, g_nslots - 1); continue; }
        int i = param_index(k.c_str());
        if (i < 0 || (!saved(i) && i != IDX_SLOT)) continue;
        w->norm[i].store(clamp01(std::atol(kv.c_str() + eq + 1) / 100000.0f));
        w->notify[i].store(1);
    }
    w->dirty.store(true);
    w->need_update.store(true);
    return 1;
}

static intptr_t dispatcher(AEffect *e, int32_t op, int32_t idx, intptr_t v, void *p, float o) {
    Plugin *w = (Plugin *)e->object;
    switch (op) {
    case effOpen: return 1;
    case effClose: HOOKS.destroy(w->dsp); delete w; return 1;
    case effGetPlugCategory: return 1;   /* kPlugCategEffect */
    case effGetEffectName:
    case effGetProductString: copy_str(p, PLUG_NAME, 32); return 1;
    case effGetVendorString: copy_str(p, PLUG_VENDOR, 32); return 1;
    case effGetVendorVersion: return PLUG_VERSION;
    case effGetVstVersion: return 2400;
    case effCanBeAutomated: return idx >= 0 && idx < NPARAMS && !is_live(idx);
    case effGetParamName: if (idx >= 0 && idx < NPARAMS) copy_str(p, PARAMS[idx].name, 32); return 1;
    case effGetParamLabel: if (p) ((char *)p)[0] = 0; return 1;   /* the unit is in the value text */
    case effGetParamDisplay: {
        if (idx < 0 || idx >= NPARAMS) return 0;
        char buf[40];
        format_value(idx, popup_is(idx) ? w->open[idx] : w->norm[idx].load(), buf, sizeof buf);
        copy_str(p, buf, 24);
        return 1;
    }
    case effSetSampleRate:
        if (o > 0) { HOOKS.rate(w->dsp, o); w->dirty.store(true); }
        return 1;
    case effSetBlockSize: case effMainsChanged: return 1;
    case effCanDo: return -1;
    case effGetChunk: return get_chunk(w, (void **)p);
    case effSetChunk: return set_chunk(w, p, v);
    case effGetProgram: return w->prog;
    case effSetProgram: {
        std::lock_guard<std::mutex> g(w->slot_lock);
        const int k = (int)v;
        if (k >= 0 && k < g_nslots && k != w->prog) slot_load(w, k);   /* a re-pick of the current one is ignored */
        return 1;
    }
    case effGetProgramName: { std::lock_guard<std::mutex> g(w->slot_lock); copy_str(p, w->slots[w->prog].name.c_str(), 24); return 1; }
    case effGetProgramNameIndexed: {
        if (idx < 0 || idx >= g_nslots) return 0;
        std::lock_guard<std::mutex> g(w->slot_lock);
        copy_str(p, w->slots[idx].name.c_str(), 24);
        return 1;
    }
    case effSetProgramName: case effBeginSetProgram: case effEndSetProgram: return 0;
    default: return 0;
    }
}

} // namespace fxs

extern "C" __attribute__((visibility("default"))) AEffect *VSTPluginMain(audioMasterCallback master) {
    using namespace fxs;
    static std::once_flag once;
    std::call_once(once, [] {
        IDX_SLOT = param_index("slot"); IDX_LOAD = param_index("load"); IDX_SAVE = param_index("save");
#if defined(NPRESETS)
        g_nslots = IDX_SLOT >= 0 ? FX_NSLOTS : (NPRESETS < FX_NSLOTS ? NPRESETS : FX_NSLOTS);
#else
        g_nslots = IDX_SLOT >= 0 ? FX_NSLOTS : 1;
#endif
    });
    Plugin *w = new Plugin();
    w->master = master;
    char dir[512];
    if (mpc_plugin_dir(dir, sizeof dir)) w->dir = dir;
    for (int i = 0; i < NPARAMS; i++) { w->norm[i].store(PARAMS[i].def); w->notify[i].store(0); w->live_sent[i] = -1.0f; }
    slots_init(w);
    for (int i = 0; i < NPARAMS; i++) if (saved(i)) w->norm[i].store(w->slots[0].norm[i]);   /* start as slot 1 */
    w->prog = 0;
    w->dsp = HOOKS.create(w->dir.c_str());
    HOOKS.rate(w->dsp, 44100.0f);
    AEffect *e = &w->fx;
    std::memset(e, 0, sizeof *e);
    e->magic = 0x56737450;
    e->dispatcher = dispatcher;
    e->process = process_acc;
    e->setParameter = setParameter;
    e->getParameter = getParameter;
    e->processReplacing = processReplacing;
    e->numPrograms = g_nslots;
    e->numParams = NPARAMS;
    e->numInputs = 2;
    e->numOutputs = 2;
    e->flags = effFlagsCanReplacing | effFlagsProgramChunks;
    e->uniqueID = PLUG_UID;
    e->version = PLUG_VERSION;
    e->object = w;
    return e;
}
