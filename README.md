# mpc-vst-spectrum

Spectrum: a **spectrum analyzer** as a VST2 **insert effect** for the Akai MPC OS plugin host (Force, MPC
Live/One/X/Key): 31 third-octave bars (20 Hz .. 20 kHz) on the MPC touchscreen. Audio passes through untouched
(float, bit-exact), so it can sit on the master. An unofficial port of the
[LSP Spectrum Analyzer](https://github.com/lsp-plugins/lsp-plugins-spectrum-analyzer)
(`lsp-plugins-spectrum-analyzer` 1.0.41, x2 = stereo) by Vladimir Sadovnikov and the LSP Project.

**This version is a test of the display:** whether MPC redraws the 31 bars live on the Force, how smoothly, and
what it costs. The analysis is LSP's own plugin class, unchanged (FFT, window, tilt, reactivity); it fills the same
640-point curve it draws in LSP's own window, and the plugin turns that into the bars RATE times a second.

## Pages

**SPECTRUM**: the bars; on the right **TOP** (level at the top of the bars, -30..+6 dB), **RANGE** (30, 40, 50 or
60 dB down to the bottom), **REACT** (LSP's reactivity: how fast the bars follow, 20 ms..1 s) and **FREEZE**.
Q-Links: TOP, RANGE, REACT.

**SETUP**: **RATE** (OFF, 4, 8 or 12 bar updates a second; OFF also stops the analysis), **VIEW** (L+R = the
louder of both, LEFT, RIGHT, MID, SIDE), **MODE** (LSP's ANALYZER or MASTERING), **TILT** (FLAT, PINK = LSP's
default, 4.5 DB, BROWN dB/oct; turns around 1 kHz) and **FFT** size (1024..8192).

PRESET menu: STAGE, STAGE FAST, CALM, MASTER CHECK, SIDE CHECK (display settings only; never FREEZE).

## Levels

Calibrated offline with pink noise: with TILT PINK, pink noise at -14 dBFS RMS (a loud master) stands evenly about
two thirds up a 50 dB RANGE at TOP 0. A bar shows the loudest point of its third octave on LSP's curve, as LSP's
own display would; a pure high tone can read a little low (the curve samples LSP's FFT), music and noise read
steadily.

## What the display costs (read before testing)

- The bars are filmstrip meters (one shared strip, 21 frames of 21 x 440 px). MPC moves them when the plugin
  reports a changed value (`audioMasterAutomate`, as mpc-vst-plugins' verified meters), only for bars whose step
  changed, at most RATE times a second, and never with `audioMasterUpdateDisplay` (MPC's PRESET menu stays usable).
- Redrawing costs MPC's screen thread, not the audio (mpc-vst-plugins NOTES 2026-10-07: a 48-column display at
  10 fps took about half a core). Start with 8 FPS; drop to 4 if the Force's screen gets sluggish.
- Every time the plugin page opens, MPC unpacks filmstrips into `/var/tmp/filmstrips/` and keeps them until a
  reboot (NOTES: "filmstrip cache"). If many reloads fill the internal storage:
  `rm -f /var/tmp/filmstrips/temp_*.img` (over SSH, MPC can keep running).
- The analysis itself is light (x86-64: about 0.5 % of a core).

## Build

`vst/build.sh` builds `vst/build/spectrum.so` (armhf, gcc 11 / glibc 2.31 in an `arm32v7` container) and the skin;
`vst/skin_post.py` adds the bars to the page (the layout's own `meter` widget needs mpc-vst-plugins' browser
renderer). `vst/test.sh` runs the offline test on x86-64 (bit-exact pass-through, pink noise even and at the right
height, tones on their own bars, TILT, TOP, VIEW, FREEZE, RATE OFF, reports only as automates, presets, chunk) and
a CPU benchmark. The workflow under `.github/workflows` does both and makes a draft release. LSP's code is
vendored: `vst/lsp/VENDORED.md`. The VST2 shell (`vst/fxshell.h`) is the same as in mpc-vst-mbdyna: float audio,
following the shared wrapper's 2026-10-21 rules (see that README).

## License and credits

GNU Lesser General Public License v3 or later ([LICENSE](LICENSE), which builds on the GNU GPL v3 in
[COPYING](COPYING)), as LSP. LSP Plugins: Copyright (C) Vladimir Sadovnikov and the LSP contributors,
<https://lsp-plug.in>. The LSP code in `vst/lsp/lsp-vendor.tar.gz` is unchanged; see `vst/lsp/VENDORED.md`.
Not affiliated with the LSP Project. The font is Titillium Web (SIL Open Font License, `vst/fonts/OFL.txt`).

## Hinweis

Entwickelt mit Unterstützung von Claude (Anthropic)
