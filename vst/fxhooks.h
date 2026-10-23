/* fxhooks.h - what a port gives fxshell.h: its engine, as plain functions. Part of mpc-vst-mbdyna / mpc-vst-spectrum
 * (lunarscraper). GNU LGPL v3 or later. */
#pragma once

struct FxHooks {
    void *(*create)(const char *plugin_dir);
    void (*destroy)(void *dsp);
    void (*rate)(void *dsp, float sr);
    /* every parameter's value in its own units (option index, whole number, dB, Hz ...); audio thread, after a change */
    void (*apply)(void *dsp, const float *val);
    /* one block; in and out never alias (the shell copies the input first) */
    void (*process)(void *dsp, const float *in_l, const float *in_r, float *out_l, float *out_r, int n);
    /* optional: engine-made values of the is_live() params, in their units, written into val[]; audio thread */
    void (*live)(void *dsp, float *val);
    /* optional: value text; return > 0 when handled */
    int (*text)(int idx, float val, char *buf, int len);
    /* optional: 1 = logarithmic knob travel between min and max (both > 0) */
    int (*log_scale)(int idx);
    /* optional: 1 = a display-only parameter the engine sets (a meter): not saved, host writes ignored */
    int (*is_live)(int idx);
};

