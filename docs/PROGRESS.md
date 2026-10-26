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
autosave ✅ · recovery ✅ · input monitoring 🟡 · device disconnect reopen 🟡 · audio focus /
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
level-matched A/B ❌ (plain on/off compare only, documented in the UI)

Export: WAV 16/24/32f, 44.1/48 kHz, mono/stereo, tail option, estimates, progress, cancel,
validation ✅ · destination choice (system "save as") 🟡 · share 🟡 · **MP3 ⏳** (needs an
encoder decision: LAME as a replaceable LGPL shared library, or AAC/M4A via MediaCodec instead)

Import formats: WAV, MP3 ✅ · AAC/M4A/FLAC/OGG ⏳ (plan: Android MediaExtractor/MediaCodec)

UI: welcome, home/projects, new project + import, studio timeline (zoom, scroll, follow,
waveforms, record state), layer controls sheet, layers management, layer/bus/master effect
screens with destination badges, effect browser, preset browser, mastering, export/share,
project settings (monitoring, offset, latency calibration, device info), library, app settings ✅
(render-tested with a fake engine; 🟡 never seen on a phone screen) · pinch zoom ⏳ ·
tempo estimation ⏳ · landscape-specific layout ⏳

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
