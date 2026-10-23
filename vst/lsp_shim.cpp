/* =============================================================================
 * lsp_shim.cpp - a small stand-in for lsp-plugin-fw's src/main/plug/IWrapper.cpp.
 *
 * The real file also writes state dumps (JSON files in a temporary folder) and creates inline-display canvases,
 * which pull in file, process and thread code a plugin inside MPC does not need. The methods below are the same
 * as LSP's for everything a module calls while processing audio (no executor, no KVT, no canvas, nothing to
 * redraw), and dump_plugin_state() does nothing. Declarations: lsp-plugin-fw include/lsp-plug.in/plug-fw/plug/IWrapper.h.
 *
 * Part of mpc-vst-mbdyna / mpc-vst-spectrum (lunarscraper). GNU LGPL v3 or later, like the LSP code it replaces.
 * ========================================================================== */
#include <lsp-plug.in/plug-fw/plug.h>

namespace lsp {
namespace plug {

IWrapper::IWrapper(Module *plugin, resource::ILoader *loader) {
    pPlugin = plugin;
    pLoader = loader;
    pCanvas = NULL;
    position_t::init(&sPosition);
}
IWrapper::~IWrapper() {
    pPlugin = NULL;
    pLoader = NULL;
    pCanvas = NULL;
}
ipc::IExecutor *IWrapper::executor() { return NULL; }
void IWrapper::query_display_draw() {}
const position_t *IWrapper::position() { return &sPosition; }
core::KVTStorage *IWrapper::kvt_lock() { return NULL; }
core::KVTStorage *IWrapper::kvt_trylock() { return NULL; }
bool IWrapper::kvt_release() { return false; }
void IWrapper::state_changed() {}
void IWrapper::request_settings_update() {}
void IWrapper::dump_plugin_state() {}
const meta::package_t *IWrapper::package() const { return NULL; }
const meta::plugin_t *IWrapper::metadata() const { return pPlugin->metadata(); }
plug::ICanvas *IWrapper::create_canvas(size_t, size_t) { return NULL; }
meta::plugin_format_t IWrapper::plugin_format() const { return meta::PLUGIN_UNKNOWN; }
const core::ShmState *IWrapper::shm_state() { return NULL; }

} // namespace plug
} // namespace lsp
