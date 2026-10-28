# Progress log and implementation checklist

Legend: ✅ implemented and covered by automated tests · 🟡 implemented, needs real-device
validation · ⏳ not started / deferred · ❌ known gap

## 2026-10-26: Milestones 1–2 (and most of 3–6 in the engine)

* Audited the three plugin repositories (integrations/vst3-audit/AUDIT.md): ran their DSP tests
  without JUCE (VocalSweet 40/40, MoodShift 43/43) and cross-compiled both cores for arm64.
* Built the native engine and effect library with a 24-suite host test run (2 560 checks).
* Built the Android layer (Oboe device, JNI), the React Native app and a debug APK
  (all three ABIs, JS bundle embedded, 97 MB debug build).
* Bugs found by the tests and fixed: off-by-one dry delay in pitch correction; pitch correction
  not transparent at ratio 1 (now crossfades to the delayed dry signal); infinite effect loop in
  the app's startup (unstable toast callback).

## 2026-10-28: Round 2 (0.2.0): corrections from the first phone test

* **Recording crackle, root causes found and fixed.** (1) On many phones the full-duplex
  callback delivers 0 microphone frames in one callback and twice as many in the next; the
  engine padded the gap with silence, so takes had tiny dropouts (clicks). The recorder now
  writes only the frames the microphone delivered. (2) Input monitoring dropped whole blocks
  in the same situation; it now runs through a small jitter buffer. Covered by
  `Recording_JitteryInputIsCapturedWithoutGapsOrDrift` and `Monitoring_IsContinuousWithJitteryInput`.
* **Sync**: the record offset now comes from Oboe's hardware timestamps of both streams
  (exact round trip per take), falling back to reported latencies; the input stream is larger
  so the mic cannot overrun. Each take reports dropped frames, overruns, CPU overloads and
  clipping ("clean take" or a warning).
* **Noise**: 40 Hz low-cut on the mic (rumble/DC), mic mode (Raw / Voice / Auto / Clean, mapped
  to Android input presets), latency mode (Lowest / Balanced / Safe), live input meter with a
  "too loud" warning, and **Clean noise** on a clip (spectral gating, Light/Medium/Strong;
  non-destructive and undoable; measured −12.8 dB noise with the voice unchanged).
* **Loop a section**: loop region on the timeline (long-press the ruler, Loop sheet: 4/8 bars,
  selected clip, set start/end), click-free 2 ms boundary fades.
* **Preview while editing effects**: a transport bar (play/pause, back to start, loop, solo,
  A/B compare) on Layer FX, Bus FX, Master FX, Mastering and the effect editor. A/B switches off
  the destination's effects (or just the edited effect) without changing the project.
* **Effect editor redesign**: an effect opens on its own screen with pages (e.g. Vocal Sweetener:
  Quick / EQ / Compressor / De-Ess / Color / Doubler / Output), at most 6 knobs per page,
  effect presets, reset, remove. Knobs/sliders only take a horizontal or vertical drag gesture
  once it is clearly theirs, and the page cannot scroll while one is moving, so scrolling no
  longer moves a control by accident. Double-tap resets a control.
* **Import a vocal as a layer** (from "+ Layer"), or into an existing layer at the playhead.
  M4A/AAC/FLAC/OGG/AMR imports are converted with Android's decoders.
* **Key and tempo detection** of an imported beat (spectral-flux tempo, Krumhansl–Schmuckler key,
  ported from MELOVAB): applied to the project tempo, shown in the Studio header, editable in
  Project Settings, and used as the default key/scale of Pitch Correction.
* **Launcher icon**: adaptive icon (with Android 13 themed/monochrome layer) and in-app logo.
* Tests: 32 native tests / 2 675 checks; Jest flow test extended to the new screens.

## Definition of the first release (brief §23)

| # | Requirement | Status |
|---|---|---|
| 1 | Install the APK on a phone | 🟡 APK built and signed (debug key); install not yet tried on a device |
| 2 | Import a backing track | ✅ WAV/MP3 decode, resample, store; 🟡 Android picker |
| 3 | Record vocals in sync | ✅ in simulation (±1 frame through a 1000-frame loopback) · 🟡 real phones: run the calibration + clap test |
| 4 | Up to 10 vocal layers | ✅ |
| 5 | Real effect on one selected layer | ✅ (isolation tested) |
| 6 | Separate effects on the overall mix | ✅ Master FX + bus FX |
| 7 | Save and reopen a project | ✅ incl. backup and crash recovery |
| 8 | Export a playable mixed song | ✅ WAV, decoded back and verified · 🟡 save/share on Android |
| 9 | Master with functioning controls | ✅ EQ/comp/width/limiter + loudness match |

## Checklist by area

Engine: transport ✅ · frame clock ✅ · multitrack mixer ✅ · volume/pan/mute/solo ✅ ·
non-destructive clips (move, trim, split, duplicate, gain, fades) ✅ · delay compensation ✅ ·
layer/bus/master chains ✅ · bypass crossfades ✅ · peak + clip metering ✅ · LUFS/true peak ✅ ·
offline render ✅ · undo/redo (structural edits; slider drags snapshot once per gesture) ✅ ·
autosave ✅ · recovery ✅ · input monitoring (jitter-buffered) ✅ sim / 🟡 phone · loop region ✅ · noise reduction ✅ · device disconnect reopen 🟡 · audio focus /
headphone unplug pause 🟡 · underrun latency tuner 🟡

Effects: Vocal Sweetener (VocalSweet) ✅ · Pitch Correction (key, scale, speed, strength,
reference, humanize) ✅ · formant control ❌ · EQ ✅ · Compressor ✅ · De-esser ✅ · Gate ✅ ·
Saturation ✅ · Limiter ✅ · Reverb ✅ · Delay/Echo (sync, ping-pong) ✅ · Slapback ✅ · Chorus ✅ ·
Stereo width ✅ · 40 MoodShift FX (underwater, telephone, radio, wah, lo-fi, filter sweeps,
modulation, octave/pitch doubles, delay spirals, atmospheres, choir, doubler…) ✅ · effect
automation ⏳

Presets: all 14 requested + Hard Tune, Telephone, Octave Double, Slapback ✅ (tuned on synthetic
signals only, ❌ no listening tests) · user save/rename/duplicate/delete ✅ · search ✅

Mastering: chain ✅ · presets (Default, Streaming, Club, Social, Podcast) ✅ · loudness match ✅ ·
A/B compare on every effect screen ✅ · level-matched A/B ❌

Export: WAV 16/24/32f, 44.1/48 kHz, mono/stereo, tail option, estimates, progress, cancel,
validation ✅ · destination choice (system "save as") 🟡 · share 🟡 · **MP3 ⏳** (needs an
encoder decision: LAME as a replaceable LGPL shared library, or AAC/M4A via MediaCodec instead)

Import formats: WAV, MP3 ✅ · AAC/M4A/FLAC/OGG/AMR 🟡 (converted with MediaExtractor/MediaCodec; not yet tried on a phone)

UI: welcome, home/projects, new project + import, studio timeline (zoom, scroll, follow,
waveforms, record state), layer controls sheet, layers management, layer/bus/master effect
screens with destination badges, effect browser, preset browser, mastering, export/share,
project settings (monitoring, offset, latency calibration, device info), library, app settings ✅
(render-tested with a fake engine; 🟡 never seen on a phone screen) · pinch zoom ⏳ ·
tempo + key detection ✅ (synthetic beats) 🟡 (real songs) · landscape-specific layout ⏳

Build: reproducible Gradle + CMake ✅ · GitHub Actions with APK artifact ✅ (first run pending
on push) · secure release signing via environment ✅

## Risks and open questions

1. **Real-device latency.** Device-reported latency is often wrong; the calibration tool
   exists but has only run against a simulated loopback.
2. **CPU on low-end phones** with many heavy layers (see docs/PERFORMANCE.md).
3. **Memory**: all audio is in RAM (16-bit); very long sessions with 10 full-length layers use
   ~200 MB.
4. **WSOLA pitch correction** can shift the voice by up to ~6 ms while it corrects; formants
   move with large corrections.
5. Input/output clock drift on external USB interfaces with separate clocks is not corrected.
