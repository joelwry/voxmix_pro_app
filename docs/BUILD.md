# Building VoxMix Pro

## Get the APK without building

Every push runs `.github/workflows/android.yml`. Open the repository on GitHub → **Actions** →
**Android build** → the latest run → **Artifacts** → `VoxMixPro-debug-apk` (a zip containing
`app-debug.apk`).

### Install on your phone

1. Copy `app-debug.apk` to the phone (USB, Google Drive, e-mail…) or download it on the phone.
2. Open it from the Files app. Android asks to allow installing from that app ("Install unknown
   apps"): allow it once.
3. Install, open **VoxMix Pro**. Android 7.0 (API 24) or newer; arm64, armv7 and x86_64.
4. If an older VoxMix test build is installed with a different signature, uninstall it first.

The debug APK embeds the JavaScript bundle, so it runs without a computer or Metro server. It is
signed with the public React Native debug key: fine for testing, **not for distribution**.

## Build locally

Requirements: JDK 17+ (21 tested), Node 22, Android SDK with `platforms;android-37.0`,
`build-tools;37.0.0`, `ndk;27.1.12297006`, `cmake;3.22.1` (set `ANDROID_HOME` or
`apps/mobile/android/local.properties` with `sdk.dir=`).

```bash
scripts/build-apk.sh                      # npm ci, typecheck, jest, gradle assembleDebug
# or manually:
cd apps/mobile && npm ci
cd android && ./gradlew :app:assembleDebug
```

Output: **`apps/mobile/android/app/build/outputs/apk/debug/app-debug.apk`**.
Faster single-ABI build for a modern phone: `./gradlew :app:assembleDebug -PreactNativeArchitectures=arm64-v8a`.

Native engine tests (host, no Android needed): `scripts/test-native.sh` (CMake ≥ 3.22, Ninja, a C++17 compiler).

The native engine is always compiled with optimisation (`-O3`, Release CMake), also in debug
APKs: an unoptimised DSP build would glitch on phones.

## Debug vs release

| | Debug (`assembleDebug`) | Release (`assembleRelease`) |
|---|---|---|
| JS | embedded bundle (dev menu available) | embedded, minified, no dev tools |
| Signing | public debug key | your key, supplied at build time |
| Use | testing on your phone | distribution |

Release signing never lives in the repository. Create a keystore once:

```bash
keytool -genkeypair -v -keystore ~/voxmix-release.jks -alias voxmix -keyalg RSA -keysize 4096 -validity 10000
```

then build with environment variables (or CI secrets of the same names):

```bash
export VOXMIX_KEYSTORE=~/voxmix-release.jks VOXMIX_KEYSTORE_PASSWORD=... VOXMIX_KEY_ALIAS=voxmix VOXMIX_KEY_PASSWORD=...
./gradlew :app:assembleRelease     # -> app/build/outputs/apk/release/app-release.apk
```

Without `VOXMIX_KEYSTORE` the release APK is left unsigned (it cannot be installed).

## Versions (verified 2026-10-26)

| Component | Version | Note |
|---|---|---|
| React Native | 0.87.1 | New Architecture (TurboModule via codegen), Hermes. 0.88.0 was 11 days old and not chosen |
| Gradle / AGP | 9.4.1 / from RN template | |
| Kotlin | 2.2.0 | |
| compileSdk / targetSdk / minSdk | 37 / 36 / 24 | |
| NDK / CMake | 27.1.12297006 / 3.22.1 | |
| Oboe | 1.10.0 (Maven, prefab) | Apache-2.0 |
| dr_wav / dr_mp3 | 0.14.6 / 0.7.4 (commit a466dbe) | public domain / MIT-0 |
| nlohmann/json | 3.12.0 | MIT |
