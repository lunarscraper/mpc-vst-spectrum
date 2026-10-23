#!/usr/bin/env bash
# Offline x86-64 test of Spectrum: the same sources as the device build (LSP at -O2, the shell and test with
# ASan/UBSan), then a CPU benchmark at -O2. Run after build.sh (needs build/params.h, popup.h, plugin_dir.h and
# the unpacked build/lsp). Uses the host's g++ (GitHub's runner has one), else a gcc:12 container.
# Prints PASSED/FAILED; the exit code follows.
set -euo pipefail
cd "$(dirname "$0")"
run() {
  if command -v g++ >/dev/null; then bash -euc "$1"; else docker run --rm -v "$PWD":/b -w /b gcc:12 bash -euc "$1"; fi
}
run '
  bash lsp/compile.sh x86 build/lsp lsp-plugins-spectrum-analyzer build/x86
  INC="-Ibuild -I."; for d in build/lsp/*/include; do INC="$INC -I$d"; done
  D="-DLSP_COMMON_LIB_BUILTIN -DLSP_DSP_LIB_BUILTIN -DLSP_DSP_UNITS_BUILTIN -DLSP_LLTL_LIB_BUILTIN -DLSP_PLUGIN_FW_BUILTIN -DLSP_RUNTIME_LIB_BUILTIN -DLSP_PLUGINS_SHARED_BUILTIN"
  SAN="-fsanitize=address,undefined -fno-sanitize-recover=undefined"
  rm -rf build/x86/san build/x86/o2 && mkdir -p build/x86/san build/x86/o2
  g++ -std=c++17 -O1 -g -fPIC $SAN $D $INC -c spectrum_vst.cpp -o build/x86/san/spectrum_vst.o
  g++ -std=c++17 -O1 -g -fPIC -w $D $INC -c lsp_shim.cpp -o build/x86/san/lsp_shim.o
  g++ -shared $SAN -o build/x86/san/spectrum.so build/x86/san/*.o build/x86/plugin/*.o build/x86/liblsp.a -lpthread -Wl,--no-undefined
  g++ -std=c++17 -O1 -g $SAN -I. -o build/x86/host_test host_test.cpp -ldl
  ASAN_OPTIONS=detect_leaks=0 ./build/x86/host_test ./build/x86/san/spectrum.so
  g++ -std=c++17 -O2 -fPIC -fvisibility=hidden $D $INC -c spectrum_vst.cpp -o build/x86/o2/spectrum_vst.o
  g++ -std=c++17 -O2 -fPIC -w $D $INC -c lsp_shim.cpp -o build/x86/o2/lsp_shim.o
  g++ -shared -o build/x86/o2/spectrum.so build/x86/o2/*.o build/x86/plugin/*.o build/x86/liblsp.a -lpthread -Wl,--gc-sections -Wl,--version-script=exports.map
  g++ -std=c++17 -O2 -I. -o build/x86/bench host_test.cpp -ldl
  ./build/x86/bench ./build/x86/o2/spectrum.so bench
  rm -rf build/x86/san/slots build/x86/o2/slots
'
