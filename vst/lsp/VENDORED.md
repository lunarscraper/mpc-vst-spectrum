# Vendored LSP code

`lsp-vendor.tar.gz` holds the LSP (Linux Studio Plugins, <https://lsp-plug.in>) code this plugin is built from,
unchanged, at the commits in `UPSTREAM.txt`. The build unpacks it into `vst/build/lsp/` and checks it against
`lsp-vendor.sha256` first; nothing is downloaded at build time. It is one archive rather than a folder tree so
the repository stays small and can be uploaded through GitHub's web page (several hundred header files otherwise).

Per repository the archive has:

- `COPYING`, `COPYING.LESSER` (GNU LGPL v3 or later, which builds on the GNU GPL v3);
- the whole `include/` tree;
- the `.cpp` files listed in `sources.txt` (found by linking the plugin against the complete libraries and keeping
  what the linker pulled in), and for the plugin repository `src/main/plug/`, `src/main/meta/`, `README.md` and
  `CHANGELOG`.

What this port does differently from LSP's own build:

- `lsp-plugin-fw/src/main/plug/IWrapper.cpp` is not compiled. `vst/lsp_shim.cpp` defines the same `IWrapper`
  methods for what a module calls while processing (no executor, no KVT, no inline-display canvas), and
  `dump_plugin_state()` does nothing: the original writes JSON files to a temporary folder and pulls in file,
  process and thread code a plugin inside MPC does not need.
- All libraries are built into the one `.so` (`LSP_*_BUILTIN`), with hidden symbols; only `VSTPluginMain` is
  exported (`vst/exports.map`), so two LSP-based plugins in one MPC process do not share or clash.
- Compiler flags follow LSP's (`make/system.mk`, `src/Makefile`): ARM mode, the NEON file with
  `-mfpu=neon-vfpv4`; `vst/lsp/compile.sh` has them.

To move to a newer LSP release: put the new commits into `UPSTREAM.txt` (the plugin's `modules.mk` names the
library versions it wants), run `vst/lsp/vendor.sh` (it clones those commits and writes the archive and its
checksum), build, and run `vst/test.sh`. If the link fails on a missing symbol, add the `.cpp` that defines it to
`sources.txt` and run `vendor.sh` again.
