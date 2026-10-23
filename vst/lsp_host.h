/* =============================================================================
 * lsp_host.h - runs one unchanged LSP plugin module (lsp-plugins.in) inside our own VST2 shell.
 *
 * LSP plugins are written against lsp-plugin-fw: a plug::Module that reads its controls from plug::IPort
 * objects, one per port in the plugin's metadata (meta::plugin_t::ports). LSP's own wrappers (LV2, VST2/3,
 * CLAP, JACK) build those ports and call update_settings() and process(). This header does the same, small:
 *   - LspPort: one port; a control's value, or a buffer pointer for audio and mesh ports;
 *   - LspHost: creates every port with its metadata default, binds the module, sets values by port id.
 * The plugin's own .cpp files (src/main/plug, src/main/meta) are compiled as they are; nothing in them changed.
 *
 * Part of mpc-vst-mbdyna / mpc-vst-spectrum (lunarscraper). GNU LGPL v3 or later, like the LSP code.
 * ========================================================================== */
#pragma once
#include <lsp-plug.in/dsp/dsp.h>
#include <lsp-plug.in/plug-fw/plug.h>
#include <lsp-plug.in/plug-fw/meta/func.h>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace lsphost {

using namespace lsp;

struct LspPort : public plug::IPort {
    float v;
    void *buf = nullptr;
    explicit LspPort(const meta::port_t *m) : plug::IPort(m), v(m->start) {}
    float value() override { return v; }
    void set_value(float x) override { v = x; }
    void *buffer() override { return buf; }
};

/* The wrapper object a module asks for redraws and the like; everything is a no-op here (lsp_shim.cpp). */
struct NullWrapper : public plug::IWrapper {
    explicit NullWrapper(plug::Module *m) : plug::IWrapper(m, NULL) {}
};

/* A mesh port's buffer (plug::mesh_t has a flexible array member, so it is allocated by hand). */
struct Mesh {
    plug::mesh_t *m = nullptr;
    std::vector<float> store;
    void alloc(size_t rows, size_t items) {
        store.assign(rows * items, 0.0f);
        m = (plug::mesh_t *)std::calloc(1, sizeof(plug::mesh_t) + rows * sizeof(float *));
        m->init(store.data(), rows, items);
    }
    ~Mesh() { std::free(m); }
};

struct LspHost {
    plug::Module *mod = nullptr;
    NullWrapper *wrap = nullptr;
    std::vector<LspPort *> ports;
    std::vector<plug::IPort *> raw;
    bool dirty = true;

    /* Create every port of `meta`, then bind the module (it reads its port pointers in init()). */
    void bind(plug::Module *m, const meta::plugin_t *meta) {
        for (const meta::port_t *p = meta->ports; p->id != NULL; ++p) {
            ports.push_back(new LspPort(p));
            raw.push_back(ports.back());
        }
        mod = m;
        wrap = new NullWrapper(m);
        mod->init(wrap, raw.data());
    }
    ~LspHost() {
        if (mod) { mod->destroy(); delete mod; }
        delete wrap;
        for (LspPort *p : ports) delete p;
    }
    LspPort *port(const char *id) {
        for (LspPort *p : ports) if (!std::strcmp(p->id(), id)) return p;
        return nullptr;
    }
    /* A control value in the port's own units (as LSP's UI would send it). Marks the module for update_settings(). */
    void set(const char *id, float v) {
        LspPort *p = port(id);
        if (p && p->v != v) { p->v = v; dirty = true; }
    }
    float get(const char *id) { LspPort *p = port(id); return p ? p->v : 0.0f; }
    void sample_rate(float sr) { mod->set_sample_rate((uint32_t)sr); dirty = true; }
    /* Call before process(): applies changed controls the way LSP's wrappers do (update_settings on the audio thread). */
    void sync() { if (dirty) { dirty = false; mod->update_settings(); } }
};

/* lsp-dsp-lib picks its NEON / SSE / AVX code paths once per process. */
inline void dsp_init_once() {
    static bool done = false;
    if (!done) { dsp::init(); done = true; }
}

} // namespace lsphost
