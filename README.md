# VoxMix Pro

A mobile vocal studio for Android: import a beat, record up to 10 synchronised vocal layers,
process them with real effects (pitch correction, the VocalSweet vocal chain, EQ, dynamics,
reverb, delay and 40 MoodShift FX creative effects), mix with layer / bus / master effects,
master with loudness matching and export validated WAV files.

React Native (TypeScript) draws the interface; all audio runs in a native C++17 engine on Google
Oboe. No audio passes through JavaScript.

**Status: second test build (0.2.0, debug).** The engine is covered by 32 native test
suites and the UI by render tests, but **nothing has been tested on a real phone yet**: see
[docs/TESTING.md](docs/TESTING.md) for the phone checklist and [docs/PROGRESS.md](docs/PROGRESS.md)
for what is done and what is not.

| | |
|---|---|
| Get the APK | GitHub → Actions → *Android build* → artifact `VoxMixPro-debug-apk` ([docs/BUILD.md](docs/BUILD.md)) |
| Build | `scripts/build-apk.sh` → `apps/mobile/android/app/build/outputs/apk/debug/app-debug.apk` |
| Engine tests | `scripts/test-native.sh` |
| Architecture | [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) |
| VST3 plugin audit | [integrations/vst3-audit/AUDIT.md](integrations/vst3-audit/AUDIT.md) |
| Performance | [docs/PERFORMANCE.md](docs/PERFORMANCE.md) |
| Licences | [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) |

```
apps/mobile/               React Native app (Android); android/ holds the Gradle project
native/audio-engine/       C++ engine: transport, mixer, recording, export, projects (+ tests/)
native/audio-effects/      C++ effects; vendor/ = DSP reused from VocalSweet and MoodShift FX
native/android/voxengine/  Android library: Oboe device + JNI (libvoxengine.so)
native/third_party/        dr_wav, dr_mp3, nlohmann/json
integrations/vst3-audit/   audit of the existing plugin repositories
docs/  scripts/  .github/workflows/
```

Supported: Android 7.0+ (API 24), arm64-v8a / armeabi-v7a / x86_64. Best timing with wired
headphones or a USB audio interface; Bluetooth adds delay that cannot be fully removed.
