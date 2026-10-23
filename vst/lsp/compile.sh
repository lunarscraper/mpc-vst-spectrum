#!/usr/bin/env bash
# Compiles the vendored LSP code: the libraries' sources.txt files into <out>/liblsp.a and the plugin's own
# src/main/plug + src/main/meta into <out>/plugin/*.o. Used by build.sh (armhf, inside the arm32v7 container)
# and test.sh (x86-64 host test).
#   compile.sh <arm|x86> <unpacked lsp folder> <plugin repo, e.g. lsp-plugins-mb-dyna-processor> <out folder> [extra flags]
# Flags follow LSP's own build (make/system.mk, src/Makefile): ARM code in ARM mode (-marm), the NEON file with
# -mfpu=neon-vfpv4; x86 SIMD files each with their own -m flags. All libraries are built in (LSP_*_BUILTIN).
set -euo pipefail
ARCH="$1"; LSP="$(cd "$2" && pwd)"; PLUGIN="$3"; OUT="$4"; shift 4
EXTRA="$*"
HERE="$(cd "$(dirname "$0")" && pwd)"
CXX="${CXX:-g++}"
mkdir -p "$OUT/obj" "$OUT/plugin"
OUT="$(cd "$OUT" && pwd)"

INC=""
for d in "$LSP"/*/include; do INC="$INC -I$d"; done
DEFS="-DLSP_COMMON_LIB_BUILTIN -DLSP_DSP_LIB_BUILTIN -DLSP_DSP_UNITS_BUILTIN -DLSP_LLTL_LIB_BUILTIN -DLSP_PLUGIN_FW_BUILTIN \
      -DLSP_RUNTIME_LIB_BUILTIN -DLSP_PLUGINS_SHARED_BUILTIN"
COMMON="-std=c++17 -O2 -fPIC -fvisibility=hidden -fvisibility-inlines-hidden -ffunction-sections -fdata-sections -fno-math-errno -w"
case "$ARCH" in
  arm) COMMON="$COMMON -march=armv7-a -marm -mfpu=neon-vfpv4 -mfloat-abi=hard -mtune=cortex-a17" ;;
  x86) ;;
  *) echo "arch: arm or x86" >&2; exit 1 ;;
esac

flags_for() {   # per-file flags (lsp-dsp-lib / lsp-common-lib src/Makefile)
  case "$1" in
    */arm/neon-d32.cpp) echo "-mfpu=neon-vfpv4" ;;
    */x86/sse.cpp)      echo "-mmmx -msse" ;;
    */x86/sse2.cpp)     echo "-mmmx -msse -msse2" ;;
    */x86/sse3.cpp)     echo "-mmmx -msse -msse2 -msse3 -mssse3" ;;
    */x86/sse4.cpp)     echo "-mmmx -msse -msse2 -msse3 -mssse3 -msse4 -msse4a -msse4.1 -msse4.2" ;;
    */x86/avx.cpp)      echo "-mavx -mvzeroupper" ;;
    */x86/avx_nz.cpp)   echo "-mavx -mno-vzeroupper" ;;
    */x86/avx2.cpp)     echo "-mavx -mvzeroupper -mavx2" ;;
    */x86/avx512.cpp)   echo "-mavx512f -mavx512vl" ;;
    *) echo "" ;;
  esac
}
export -f flags_for
export CXX COMMON DEFS INC EXTRA LSP OUT

list() {
  grep -v '^#' "$HERE/sources.txt" | awk -v a="$ARCH" 'NF && ($2 == "all" || $2 == a) {print $1}'
  for f in "$LSP/$PLUGIN"/src/main/plug/*.cpp "$LSP/$PLUGIN"/src/main/meta/*.cpp; do echo "${f#$LSP/}"; done
}
JOBS="$(nproc 2>/dev/null || echo 2)"
list | xargs -P "$JOBS" -I{} bash -c '
  f="{}"; o="$OUT/obj/$(echo "$f" | tr / _ | sed "s/\.cpp$/.o/")"
  case "$f" in */src/main/plug/*|*/src/main/meta/*) o="$OUT/plugin/$(basename "$(dirname "$f")")_$(basename "${f%.cpp}").o" ;; esac
  $CXX $COMMON $(flags_for "$f") $DEFS $INC $EXTRA -c "$LSP/$f" -o "$o" || { echo "FAILED: $f" >&2; exit 255; }
'
rm -f "$OUT/liblsp.a"
ar rcs "$OUT/liblsp.a" "$OUT"/obj/*.o
echo "compile.sh: $(ls "$OUT"/obj/*.o | wc -l) library objects, $(ls "$OUT"/plugin/*.o | wc -l) plugin objects ($ARCH)"
