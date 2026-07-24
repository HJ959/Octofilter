#pragma once

// ── Plugin identity ───────────────────────────────────────────────────────────
#define DISTRHO_PLUGIN_NAME    "Octofilter"
#define DISTRHO_PLUGIN_URI     "https://github.com/henrydrp10/Octofilter"
#define DISTRHO_PLUGIN_CLAP_ID "com.henry.octofilter"
#define DISTRHO_PLUGIN_BRAND_ID  Hnry
#define DISTRHO_PLUGIN_UNIQUE_ID Octo

// ── I/O layout ────────────────────────────────────────────────────────────────
#define DISTRHO_PLUGIN_NUM_INPUTS   2   // stereo in
#define DISTRHO_PLUGIN_NUM_OUTPUTS  2   // stereo out
#define DISTRHO_PLUGIN_IS_RT_SAFE   1   // we promise no allocs/locks on audio thread

// ── Features ──────────────────────────────────────────────────────────────────
#define DISTRHO_PLUGIN_HAS_UI       1   // NanoVG UI enabled (Phase 6)

// ── UI size ───────────────────────────────────────────────────────────────────
#define DISTRHO_UI_DEFAULT_WIDTH    700
#define DISTRHO_UI_DEFAULT_HEIGHT   450
#define DISTRHO_UI_USE_NANOVG       1
#define DISTRHO_PLUGIN_IS_SYNTH     0
#define DISTRHO_PLUGIN_WANT_MIDI_INPUT  0
#define DISTRHO_PLUGIN_WANT_MIDI_OUTPUT 0
#define DISTRHO_PLUGIN_WANT_TIMEPOS     0
#define DISTRHO_PLUGIN_WANT_LATENCY     0
#define DISTRHO_PLUGIN_WANT_STATE       1
#define DISTRHO_PLUGIN_WANT_FULL_STATE  1
#define DISTRHO_PLUGIN_WANT_PARAMETER_VALUE_CHANGE_REQUEST 1

// ── VST3 category ─────────────────────────────────────────────────────────────
#define DISTRHO_PLUGIN_VST3_CATEGORIES "Fx|Filter|Spatial"

// ── CLAP features ─────────────────────────────────────────────────────────────
#define DISTRHO_PLUGIN_CLAP_FEATURES "audio-effect", "filter", "stereo"
