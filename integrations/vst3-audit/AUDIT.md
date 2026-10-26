# VST3 repository audit and integration report

Date: 2026-10-26. Audited by reading the source, building it, and running its tests in the
development container (Linux x86_64, clang 18 / gcc 13, Android NDK 27.1). The three source
repositories were **not modified**: code was copied into this repository (see
`native/audio-effects/vendor/PROVENANCE.md` for the exact commits).

## Summary

| Repository | Purpose | Reusable on Android | What VoxMix uses |
|---|---|---|---|
| `vocalsweetTesta` (VocalSweet 1.0) | One-knob vocal enhancer VST3 | **Yes, unchanged.** `Source/dsp/` is plain C++17 with no JUCE | Whole chain as the **Vocal Sweetener** effect; its SVF, compressor, de-esser, saturator, look-ahead limiter and delay line as building blocks of EQ, Compressor, De-Esser, Saturation, Limiter and the Mastering chain; its 9 factory presets inside VoxMix presets |
| `moodfx_plugin` (MoodShift FX 0.1.1) | 40 creative sound-design effects VST3 | **Yes, unchanged.** `Source/Effects`, `Parameters/ParameterIDs.h`, `Presets/FactoryPresets.*` form the JUCE-free `msfx_core` library | All 40 effects as individual creative effects (`ms_*`), its FDN reverb, delay lines and chorus inside the Reverb/Delay/Chorus effects, its WSOLA granular pitch shifter inside Pitch Correction |
| `melovab-studio` (MELOVAB STUDIO 0.1.0) | Arrangement / composition plugin (analysis, generation, synths) | **Partly.** `Source/Core` depends on JUCE (`juce::String`, `juce::var`, `juce::dsp::FFT`, audio formats) | The YIN pitch estimator algorithm, ported (JUCE call `jlimit` replaced) into Pitch Correction. Tempo/key analysis is a candidate for later (needs an FFT replacement) |

Evidence collected during the audit:

* VocalSweet `tests/DspTests.cpp` built without JUCE on Linux: **40 tests, 0 failed** (its own
  CPU check: 3.95 % of one core, stereo 48 kHz, every module on).
* MoodShift FX DSP + preset tests built without JUCE: **43 tests, 3 121 checks, 0 failed**.
* Both cores cross-compiled for `arm64-v8a` with NDK 27.1 (`android-24`): **0 errors, 0 warnings**.
* MELOVAB `Source/Core` was not built for Android: it includes `juce_core`, `juce_audio_basics`,
  `juce_audio_formats` and `juce_dsp`.

## 1. VocalSweet (`vocalsweetTesta/VocalSweet`)

**Features.** Signal chain: input gain → HPF 12/24 dB → mud/box cuts + tone/warmth shelves →
soft-knee stereo-linked compressor → ADAA tanh saturation → presence/air EQ → exciter →
split-band de-esser → 2-voice doubler → dry/wet → output → 1 ms look-ahead limiter → bypass.
Macro layer (Sweeten / Tone / Warmth / Presence / Control) mapped to coordinated offsets.

**Build system / dependencies.** CMake; JUCE 8.0.4 for the plugin, editor and preset files.
The DSP (`Source/dsp/*.h`, header-only) needs only the C++ standard library.

**Licences.** VocalSweet's own code is the user's. JUCE (AGPLv3 / commercial) and the VST3 SDK
are used only by the plugin wrapper and are **not** used by VoxMix. The Inter font (OFL) is
not used.

**Reusable for Android.** All of `Source/dsp`. Buffers are planar float, 1–2 channels,
`process(float* const* channels, int numChannels, int numSamples)`, allocation only in
`prepare()`: this matches the VoxMix engine directly.

**Changes needed.** None to the DSP. The VoxMix wrapper (`native/audio-effects/src/VocalSweetEffect.cpp`)
maps the parameter table to the VoxMix parameter interface, drops the host-bypass parameter
(VoxMix bypasses per slot) and adds a dual-mono path (mono vocal layers are processed once).

**Windows/VST3-only parts (not reused).** `PluginProcessor`, `PluginEditor`, `PresetManager`
(JUCE XML, `%APPDATA%` paths), `ui/`, the clang-cl/xwin toolchain, pluginval logs.

**Risks.** Peak-sensing compressor (no RMS option). 1 ms limiter latency (compensated by the
VoxMix mixer). No listening tests were done on the plugin; same applies here.

## 2. MoodShift FX (`moodfx_plugin`)

**Features.** 40 effects in 8 categories (underwater, wah, phaser, reverse, glitch, robot,
tape, vinyl, slapback, ping-pong, octave, formant, choir, stutter, pitch dive, …), 5 macros
and 19 advanced parameters, tempo sync, crossfaded effect switching, safety limiter.

**Build system / dependencies.** CMake, JUCE 9.0.4 for the plugin; `msfx_core` static library is
JUCE-free (C++17, `<array>`, `<vector>`, `<cmath>`, `<atomic>`, `<memory>`).

**Licences.** The source is marked proprietary to its owner (the user). JUCE is not used by VoxMix.

**Reusable for Android.** The 40 `EffectProcessor` implementations, `EffectRegistry`,
`FactoryPresets`, `ParameterIDs.h` and every DSP module (filters, delay lines, LFOs, FDN reverb,
granular pitch shifter, LPC formant shifter, Hilbert shifter).

**Changes needed.** None to the DSP. The plugin's `EffectChain` (which instantiates **all 40
effects** at once) is *not* used: on a phone with 10 layers that would multiply memory and
preparation time. VoxMix creates one effect per slot through the registry's factory function
and reproduces the chain's behaviour in `MoodShiftEffects.cpp`: control-rate parameter
smoothing on a 32-sample grid, the per-effect mix law (linear / equal power) and level trim, and
the non-finite guard. Tempo/PPQ come from the VoxMix transport instead of a DAW host.

**Windows/VST3-only parts.** Plugin processor/editor, state manager, preset browser UI,
NSIS installer, Windows CI, cross toolchain.

**Risks.** Each MoodShift effect allocates its buffers per instance (reverb/delay lines up to a
few MB at 48 kHz); fine for a handful of slots, but very large chains on 10 layers use more memory.
CPU cost per effect measured on the host (see `docs/PERFORMANCE.md`): 0.2–7 % of one core.

## 3. MELOVAB STUDIO (`melovab-studio`)

**Features.** Audio analysis (tempo, key, chords, melody), rule-based generation, 19 synthesised
instruments, piano roll, MIDI/WAV export.

**Build system / dependencies.** CMake, JUCE 8.0.6, including inside `Source/Core`.

**Reusable for Android.** Algorithms rather than files:
* **YIN** pitch estimator: ported into `PitchCorrection.cpp` (only `juce::jlimit` needed replacing).
* Tempo estimation (spectral flux + autocorrelation/comb) and key detection (Krumhansl-Schmuckler):
  good candidates for an editable "estimate tempo" feature. They need `juce::dsp::FFT` replaced by
  a small FFT (e.g. pffft, BSD) — **deferred**.
* Synth engine / generator: out of scope for a vocal studio.

**Risks.** Porting `Session/Model` would drag in `juce::String`/`juce::var`; not worth it.

## Effects that were written new (not available in the plugins)

| Effect | Why new |
|---|---|
| Pitch Correction | None of the plugins does scale-aware correction (VocalSweet's docs list it as out of scope). Built from MELOVAB's YIN + MoodShift's WSOLA shifter + new note-snapping/retune logic |
| Gate / Expander | Not present |
| Parametric EQ (5 band) | VocalSweet's EQ is macro-driven with fixed bands; a free EQ reuses its SVF |
| Delay (synced, ping-pong) | MoodShift's delays are character effects; a plain delay reuses its delay lines |
| Stereo Width, Mastering chain | Not present as standalone processors |

## Effects that should later be rewritten or replaced

* **Formant-preserving pitch correction**: the WSOLA shifter moves formants with the pitch;
  fine for corrections ≤ 1 semitone, audible for large shifts. MoodShift's LPC formant shifter is
  a possible base.
* **Compressor detector**: add RMS / program-dependent mode.
* **Mastering compare**: level-matched A/B is not implemented yet.

## Estimated effort (remaining)

| Item | Effort |
|---|---|
| Real-device latency/sync validation and tuning | 1–2 days with 3–4 phones |
| MP3 export (LAME as a separate LGPL shared library, or AAC via MediaCodec) | 1–2 days + licence review |
| Tempo estimation from MELOVAB (FFT replacement) | 1 day |
| Formant-preserving correction | 3–5 days |
| Disk-streaming sources for very long sessions | 2–3 days |
