#!/usr/bin/env bash
# Build Spectrum as a VST2 insert effect for the MPC OS plugin host (armhf).
#   vst/build/spectrum.so             the plugin
#   vst/build/pluginlist-entry.xml   the <PLUGIN> line for MPC.settings' pluginList-arm
#   vst/build/skin/                  the plugin folder for /sdcard/Synths/ (skin; the release adds the .so)
# Steps: skin renderer, params.h + skin (mpc-vst-plugins' gen_vst.py), unpack the vendored LSP code, then compile
# LSP + spectrum_vst.cpp in an arm32v7 container (gcc 11, glibc 2.31: loads on MPC OS 2.x and 3.x).
# MPC_VST: a checkout of sd88me/mpc-vst-plugins (the workflow sets it). Offline test: test.sh.
set -euo pipefail
cd "$(dirname "$0")"
MPC_VST="$(cd "${MPC_VST:-../../mpc-vst-plugins}" && pwd)"
SO=spectrum.so
PLUGIN=lsp-plugins-spectrum-analyzer
U="$(id -u):$(id -g)"
mkdir -p build

# 1. skin artwork renderer (host binary; the renderer vendored in mpc-vst-plugins)
if command -v gcc >/dev/null; then
  gcc -O2 -I"$MPC_VST/tools/vendor/force-shadow/tools" -o build/shadow_art "$MPC_VST/tools/shadow_art.c" -lm
else
  docker run --rm -u "$U" -v "$PWD":/w -v "$MPC_VST":/mv:ro -w /w gcc:12 \
    gcc -O2 -I/mv/tools/vendor/force-shadow/tools -o build/shadow_art /mv/tools/shadow_art.c -lm
fi

# 2. params.h, skin (skin_post.py adds the bars), pluginlist-entry.xml (needs Pillow)
#    Frame titles and option labels in Titillium Web (vst.json "title_font"; the skin builder reads it from SHADOW_TITLE_FONT)
if python3 -c "import PIL" 2>/dev/null; then
  SHADOW_TITLE_FONT="$PWD/fonts/TitilliumWeb-SemiBold.ttf" python3 "$MPC_VST/tools/gen_vst.py" vst.json
else
  docker run --rm -u "$U" -e SHADOW_TITLE_FONT=/w/fonts/TitilliumWeb-SemiBold.ttf -v "$PWD":/w -v "$MPC_VST":/mv:ro -w /w python:3.11-slim sh -c \
    "pip install -q --no-warn-script-location --target /tmp/p pillow >/dev/null 2>&1; PYTHONPATH=/tmp/p python3 /mv/tools/gen_vst.py vst.json"
fi
cp "$MPC_VST/wrapper/popup.h" "$MPC_VST/wrapper/plugin_dir.h" build/

# 3. the vendored LSP code (lsp/VENDORED.md), checked against its checksum
(cd lsp && sha256sum -c lsp-vendor.sha256)
rm -rf build/lsp && tar -xzf lsp/lsp-vendor.tar.gz -C build

# 4. the plugin (armhf). First-generation MPC OS units are Rockchip RK3288 (Cortex-A17, NEON/VFPv4).
docker run --rm --platform linux/arm/v7 -v "$PWD":/b -w /b arm32v7/gcc:11-bullseye bash -euxc '
  bash lsp/compile.sh arm build/lsp '"$PLUGIN"' build/arm
  F="-std=c++17 -O2 -fPIC -fvisibility=hidden -fvisibility-inlines-hidden -ffunction-sections -fdata-sections -fno-math-errno"
  F="$F -march=armv7-a -marm -mfpu=neon-vfpv4 -mfloat-abi=hard -mtune=cortex-a17 -Wall -Wno-unused-parameter"
  INC="-Ibuild -I."; for d in build/lsp/*/include; do INC="$INC -I$d"; done
  D="-DLSP_COMMON_LIB_BUILTIN -DLSP_DSP_LIB_BUILTIN -DLSP_DSP_UNITS_BUILTIN -DLSP_LLTL_LIB_BUILTIN -DLSP_PLUGIN_FW_BUILTIN -DLSP_RUNTIME_LIB_BUILTIN -DLSP_PLUGINS_SHARED_BUILTIN"
  g++ $F $D $INC -c spectrum_vst.cpp -o build/arm/spectrum_vst.o
  g++ $F $D $INC -w -c lsp_shim.cpp -o build/arm/lsp_shim.o
  g++ -shared -o build/'"$SO"' build/arm/spectrum_vst.o build/arm/lsp_shim.o build/arm/plugin/*.o build/arm/liblsp.a \
      -static-libstdc++ -static-libgcc -lpthread -lm -Wl,--gc-sections -Wl,--no-undefined -Wl,--version-script=exports.map
  strip build/'"$SO"'
  echo "-- needed --"; readelf -d build/'"$SO"' | grep NEEDED
  echo "-- exported --"; readelf --dyn-syms -W build/'"$SO"' | awk "\$5 == \"GLOBAL\" && \$7 != \"UND\" {print \$8}"
  echo "-- highest glibc --"; readelf -V build/'"$SO"' | grep -o "GLIBC_[0-9.]*" | sort -uV | tail -1
  chown -R '"$U"' build
'
md5sum build/$SO
