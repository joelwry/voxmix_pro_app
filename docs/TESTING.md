# Testing

## Automated

| Suite | Command | What it covers |
|---|---|---|
| Native engine + DSP (host) | `scripts/test-native.sh` | 32 tests, ~2 675 checks (see below) |
| JS (Jest) | `cd apps/mobile && npx jest` | Every main screen renders against a fake engine; onboarding → home → studio → Layer FX / Master FX / Mastering / Export / Layers / Settings |
| Typecheck | `cd apps/mobile && npx tsc --noEmit` | |
| CI | `.github/workflows/android.yml` | all of the above + debug APK artifact |

Native tests use synthetic signals (sines, noise, impulses, click tracks):

* **Effects**: every registered effect (51 types) stays finite on silence, impulses, a 0 dBFS
  sine with 1-sample blocks, odd block sizes, 44.1 and 48 kHz, random/extreme/NaN parameters,
  and audibly changes the signal with its default settings. EQ bell gain and HPF slope, compressor
  static curve (±1 dB), limiter ceiling and latency, chain ordering, bypass latency, VocalSweet
  preset values, parameter clamping.
* **Pitch correction**: a 452 Hz tone is corrected to 440 Hz (±3 Hz) in chromatic and C major;
  strength 0 leaves it untouched.
* **Mixer**: frame-exact clip placement (an impulse at frame 12 345 appears at 12 345), clip
  gain, volume (-6.02 dB → ×0.5), constant-power pan, solo/mute, an effect on one layer does
  not leak into another, Master FX applies to the whole mix, delay compensation with latency
  effects on layer and master (impulse exactly on its frame).
* **Recording sync** with a simulated full-duplex device whose microphone hears the speaker
  1 000 frames late: two takes (from 0 s and from 2 s) are placed within ±1 frame of the beat's
  clicks, survive a reload, and a retake replaces the overlapped region non-destructively
  (undo restores it).
* **Jittery full-duplex input** (0 frames, then 2×): takes have no gaps or drift and stay
  on the beat; monitoring output is continuous.
* **Loop region** repeats seamlessly; **vocal import** onto a new layer; **A/B compare**
  bypasses without saving; **mic low-cut** removes DC and meters the input.
* **Analysis**: a synthetic 100 BPM A-minor beat is detected as 100 BPM / A minor.
  **Noise reduction**: −12.8 dB noise, voice level unchanged (±0.1 dB).
* **Latency calibration**: measures a 777-frame loopback exactly, computes the offset, and a
  following recording lands on the beat; silence reports failure instead of a wrong number.
* **Projects**: save/reopen round-trip is identical; corrupt `project.json` falls back to the
  backup; missing media is reported; garbage, truncated and missing files are rejected on import;
  44.1 kHz imports are resampled; crash recovery of an unclosed take; undo/redo; split, trim,
  duplicate, delete without deleting media; 10-layer limit.
* **Presets**: all requested factory presets exist and load real parameters; user presets
  save, rename, persist across restarts; factory presets are read-only.
* **Export**: WAV 16/24/32-float at 44.1/48 kHz, stereo and mono: decoded back and checked for
  duration, channel count, sample rate, peak and exact file size; empty projects report an error.
* **Mastering**: loudness match reaches -14 LUFS (±1) with true peak ≤ 0 dBTP; the loudness
  meter reads EBU Tech 3341's -23 dBFS 1 kHz reference as -23 LUFS (±0.2).
* **Long session / CPU**: 60 s, 10 layers × (pitch correction + Vocal Sweetener + reverb +
  delay) + mastering, offline and through the real-time callback path at 192-frame blocks.

## Manual checklist on a real Android phone

Use the debug APK. Record results (phone model, Android version, headphones) in `docs/DEVICE_RESULTS.md`.

1. **Install & launch**: welcome screen appears; "Get Started" → Home.
2. **New project + import**: create a project, import an MP3 beat and a WAV beat. Waveform and
   duration look right; a non-audio file shows an error.
3. **Playback**: play / pause / stop / tap the ruler to seek. Playhead follows. Headphones
   unplugged while playing → playback pauses. A phone call / other media app → playback pauses.
4. **Permission**: first record shows the explanation and Android dialog. Deny → message, no crash.
   Allow later in Settings → works.
5. **Sync (wired headphones)**: Project Settings → Measure latency (headphones unplugged,
   volume up) → apply the offset. Plug wired headphones, record a take clapping on the beat's
   kick for 30 s. Mute the beat, solo the take, play: claps must line up with the beat when
   both are unmuted (zoom in: offset should be under ~5 ms). Repeat at 1 min into the song.
6. **10 layers**: add layers up to 10; the 11th is refused. Record short takes on several
   layers; mute/solo/volume/pan each; rename, recolour, duplicate, delete, undo.
7. **Clip editing**: select a clip, drag to move, drag edges to trim, split at playhead, clip
   gain ±, delete (keep media) then undo.
8. **Layer FX**: on Harmony 1 add Reverb at 100 % mix → only Harmony 1 gets reverb (solo lead
   to confirm). Toggle bypass while playing: no clicks. Apply "Afrobeat Lead" preset; change a
   parameter; save as a user preset; rename; reopen the app → preset still there.
9. **Master FX**: add an EQ with a deep low cut on Master FX → whole song changes.
10. **Bus**: add a bus, route two layers to it, add a compressor on the bus.
11. **Mastering**: choose Streaming, Match loudness → report shows ~-14 LUFS. Toggle mastering
    while playing; meters move; clip indicator lights if you push master volume up.
12. **Export**: WAV 24-bit 48 kHz stereo → "verified" result; Save to device → file opens in a
    music player; Share → WhatsApp / Drive. Export mono 16-bit 44.1 kHz.
13. **Persistence**: force-stop the app while a project is open → reopen: all layers, clips,
    effects and settings are there. Force-stop *during* a recording → reopen → "recovered take".
14. **Lifecycle**: background the app while recording → take is saved, playback stops. Rotate
    the phone; return from background; play again.
15. **Performance**: 6–10 layers with presets; watch for crackles; note the CPU value
    (debug overlay not yet exposed — report audible glitches).
16. **Round 2 checks**: record 1 min with wired headphones, listen for clicks (should be none;
    the toast should say "Clean take"). Long-press the ruler → 4-bar loop; open Layer FX →
    an effect → play, turn knobs, A/B. Scroll the effect editor without touching knobs: no
    value changes. Import a beat → key/BPM toast is plausible. "+ Layer" → import a vocal.
    Clean noise (Medium) on a take recorded with a fan running. Home screen shows the icon.
17. **Storage**: with < 20 MB free, recording is refused with a message.

## Not yet verified (be aware)

* Nothing has been run on a physical Android device or emulator yet (no emulator/KVM in the
  build environment). The engine logic is tested natively; the Android audio path (Oboe),
  permissions, pickers and UI on a phone need the checklist above.
* No listening tests: sound quality claims are limited to the measurements above.
* Bluetooth: latency is measured/reported by Android but can be inaccurate; expect offsets.
