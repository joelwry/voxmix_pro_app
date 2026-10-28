# VoxMix Pro architecture

```
apps/mobile (React Native 0.87.1, TypeScript, New Architecture)
  App.tsx, src/screens/*        UI: state-driven screens, no audio work
  src/engine/engine.ts          typed command API + 30 Hz status poller
  src/native/NativeVoxEngine.ts TurboModule spec (codegen)
  android/app/.../VoxEngineModule.kt
                                TurboModule: command executor thread, file picker, save-as,
                                share, audio focus, headphone unplug, app lifecycle
        │ JNI: nativeCall(method, json) -> json, nativeStatus() -> json
native/android/voxengine        Android library: libvoxengine.so
  OboeDevice.cpp                AudioDeviceManager on Oboe (AAudio / OpenSL ES)
  VoxEngineJni.cpp              JNI bridge
        │
native/audio-engine (voxengine, C++17, no Android / RN dependency)
  Engine            command dispatcher, real-time callback, graph publication, undo, autosave
  MixGraph          MultitrackMixer + PlaybackEngine: Track/Clip/Bus/Master rendering
  GraphBuilder      Project -> immutable render graph (reuses effect instances)
  RecordingManager  lock-free ring buffer + writer thread, latency-compensated placement
  OfflineRenderer   OfflineRenderer + AudioExporter (WAV, validation by decoding)
  Metering          PeakMeter, LoudnessMeter (BS.1770 / R128, true peak)
  AudioSource       decoded audio (dr_wav / dr_mp3), sinc resampler, waveform peaks
  Project + ProjectSerializer   versioned JSON, atomic writes, backup, recovery
  Presets           factory + user effect-chain presets, mastering presets
native/audio-effects (voxfx, C++17)
  Effect / EffectRegistry / EffectChain    EffectProcessor interface, catalogue, slots
  CoreEffects, VocalSweetEffect, MoodShiftEffects, PitchCorrection, MasteringEffect
  vendor/vocalsweet, vendor/moodshift      DSP reused unchanged from the plugins
```

The module names requested in the brief map as follows: AudioDeviceManager = `IAudioDevice` /
`OboeDevice`; AudioTransport + AudioClock = the engine's frame-counter transport
(`position`, `playing`, `pendingSeek`); RecordingManager; PlaybackEngine + MultitrackMixer =
`MixRenderer`; Track / Clip = `TrackSpec`/`ClipSpec` (model) and `TrackRT`/`ClipRT` (real time);
AudioBuffer = planar float blocks inside `MixRenderer`; EffectProcessor = `voxfx::Effect`;
EffectChain; BusProcessor / MasterBus = bus and master stages of `MixRenderer`; OfflineRenderer;
AudioExporter = `renderProject` + `WavWriter`; ProjectSerializer; MeteringEngine = `Metering.h`.

## Threads

| Thread | Work | Never |
|---|---|---|
| Oboe callback (real time) | read input, render graph, capture recording into ring buffer, update transport and meters | allocate, lock, file I/O, log |
| Engine command thread (Kotlin executor) | all commands: edits, imports (decode + resample), save, analysis | touch audio buffers in use |
| Recording writer | drains the ring buffer to a 32-bit float WAV + memory copy | |
| Export worker | offline render + WAV write + validation | |
| Housekeeping | autosave (2 s after the last change, never while recording), freeing retired graphs | |
| Oboe latency monitor | timestamps -> measured input/output latency | |
| JS thread | UI, 30 Hz status polling (one JSON snapshot, no per-buffer events) | timing, audio |

## Real-time safety

* The project is compiled into an immutable `MixGraph`; the audio thread reads it through an
  atomic pointer. A replaced graph goes to a graveyard and is destroyed on the control thread
  only after the audio thread has provably left it (odd/even callback sequence counter).
* Effect instances live in an `InstanceCache` and are shared between successive graphs, so a
  structural edit (adding an effect, moving a clip) does not reset reverb tails.
* Parameters, bypass, volume, pan, mute and solo are atomics: changing them needs no rebuild.
  Gains ramp per sample; bypass crossfades for 10 ms; filter parameters are smoothed.
* All buffers are allocated in `prepare()` / graph build. Callbacks larger than 256 frames are
  processed in 256-frame blocks.

## Processing order (live and export use the same code)

```
Layer:  clips (gain, fades) [+ live input when monitoring] -> Layer FX -> volume -> pan -> mute/solo
Bus:    sum of routed layers -> Bus FX -> volume -> pan -> mute
Master: sum of layers + buses -> Master FX -> master volume -> Mastering chain -> output
```

Delay compensation: every effect reports a constant latency (bypassed slots delay their dry
signal by the same amount). Clips are read ahead by the layer's chain latency plus its bus
latency, so all layers arrive aligned. Master latency is added to the recording compensation and
removed from the start of exports.

## Timing and recording sync

The only clock is the audio frame counter. When recording starts, the transport position of the
block that captured the first input frame is stored; input frame *k* was captured *k* frames
later. The take is placed at

```
clip.start = startPosition - (inputLatency + outputLatency + masterChainLatency) + userOffset
```

The round trip is computed per take from the stream clock captured with the first recorded
block (output frames written, input frames read) and the latest hardware timestamps of both
Oboe streams: `rt = W − (outFrame + (inTime + (R − inFrame)/sr − outTime)·sr)`. When timestamps
are unavailable it falls back to the reported input + output latencies (AAudio averaged
timestamps, or buffer-size estimates on OpenSL ES). Because device reports can be wrong, Project Settings
has a loopback **latency calibration** (8 clicks through the speaker, matched-filter detection in
the mic signal, consistency check) that measures the real round trip and suggests the offset.
The full path is tested with a simulated loopback device (`Recording_SyncWithSimulatedLoopback`,
`Calibration_MeasuresLoopbackRoundTrip`): takes land within ±1 frame. **This has not yet been
validated on real phones.**

Input frames are read every callback (all available frames, non-blocking) and only the frames
the microphone actually delivered are recorded: a callback with 0 input frames adds nothing
(padding it with silence was the cause of the 0.1.0 crackle), so the take's sample count is
exact even when input and output deliveries jitter. Input monitoring goes through a small
jitter FIFO (`RtHelpers.h`) for the same reason. The mic path has a 40 Hz Butterworth low-cut
and a peak/clip meter; mic mode maps to Android input presets and latency mode to the Oboe
buffer size.

## Loop, compare and analysis

* **Loop**: the callback splits a block at the loop end, jumps to the loop start and applies a
  2 ms fade either side of the seam. Recording auto-stop is disabled while looping.
* **Compare (A/B)**: `setCompare` bypasses a destination's chain (or one slot) in the live graph
  only; nothing is written to the project or undo history.
* **Analysis** (`Analysis.cpp`): tempo from spectral-flux onset autocorrelation with octave
  disambiguation, key from a chroma profile correlated with Krumhansl–Schmuckler profiles.
  Runs on import of a beat; results stored in `project.analysis`.
* **Noise reduction**: STFT spectral gating with a noise profile learned from the quietest 15 %
  of frames, over-subtraction and recursive gain smoothing; writes a new 32-bit source so the
  original take stays (undoable). Ring-buffer overflow is
counted and reported with the take, never silently dropped. A new take replaces the overlapped
region of older clips on the same layer non-destructively (clips are split/trimmed; the media
files stay, undo restores them).

## Project format and recovery

`<files>/voxmix/projects/<id>/project.json` (format `voxmix-project`, `formatVersion` 1) plus
`media/` (imported audio normalised to 16-bit WAV at the project rate, recordings as 32-bit float
WAV). Saves write `project.json.tmp`, fsync, keep the last valid file as `project.json.bak` and
rename atomically. Loading falls back to the backup and reports missing media. A recording
writes a sidecar `rec-*.json`; if the app dies mid-take the WAV header is repaired on the next
open and the take is placed back on its layer. Newer formats are refused rather than silently
downgraded.

## Native bridge API

Commands (`cmd(method, args)`), each returning `{ok, ...}` and, for edits, the updated project:

* Engine/device: `getInfo`, `getEffectCatalog`, `startAudio`, `stopAudio`, `onAppBackground`, `onAppForeground`
* Projects: `listProjects`, `createProject`, `openProject`, `getProject`, `saveProject`, `closeProject`, `deleteProject`, `updateProject`
* Media: `importAudio`, `getWaveform`
* Transport: `play`, `pause`, `stop`, `seek`
* Recording: `startRecording`, `stopRecording`, `setMonitoring`, `startLatencyTest`, `finishLatencyTest`
* Layers/buses: `addTrack`, `removeTrack`, `duplicateTrack`, `updateTrack`, `moveTrack`, `addBus`, `removeBus`, `updateBus`
* Clips: `moveClip`, `trimClip`, `splitClip`, `duplicateClip`, `setClipGain`, `setClipFades`, `deleteClip`
* Effects: `addEffect`, `removeEffect`, `moveEffect`, `setChain`, `setEffectParam(s)`, `setEffectBypass`, `getEffectState`
* Mastering: `updateMaster`, `listMasteringPresets`, `applyMasteringPreset`, `analyzeMix`, `autoMaster`
* Presets: `listPresets`, `applyPreset`, `savePreset`, `renamePreset`, `duplicatePreset`, `deletePreset`
* Edit history: `undo`, `redo`
* Export/library: `estimateExport`, `exportMix`, `cancelExport`, `getExportResult`, `listExports`, `deleteExport`
* App settings: `getSettings`, `setSettings`

Effect destinations are explicit strings: `track:<id>` (Layer FX), `bus:<id>` (Bus FX),
`master` (Master FX), `mastering` (Mastering chain).

Status (`getStatus`, synchronous): position, playing, recording, per-layer and master peak/clip,
loudness (momentary, short-term, integrated, true peak), recording frames/dropouts/input peak,
export progress/result, CPU load of the callback, device errors.

## Extensibility

* New effect: one `EffectDescriptor` + class + `register*` entry; it appears in the browser,
  presets and projects automatically.
* More tracks/buses: limits are constants (`kMaxVocalLayers`, `kMaxBuses`); the graph has no
  fixed track count.
* Instruments: a track type rendering into the same `TrackRT` buffer stage.
* Streaming sources: `AudioSource::addTo` is the only read path.
