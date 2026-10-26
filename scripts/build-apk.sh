#!/usr/bin/env bash
# Builds the installable debug APK. Needs JDK 17+, Node 22, Android SDK (ANDROID_HOME) with
# platform 37, build-tools 37.0.0, NDK 27.1.12297006 and CMake 3.22.1.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT/apps/mobile"
npm ci
npx tsc --noEmit
npx jest --ci
cd android
./gradlew :app:assembleDebug
echo "APK: $ROOT/apps/mobile/android/app/build/outputs/apk/debug/app-debug.apk"
