#!/usr/bin/env bash
# Copies the reusable DSP from the plugin repositories into native/audio-effects/vendor.
# Usage: scripts/sync-vendor.sh <vocalsweet repo> <moodfx repo>
set -euo pipefail
VS="${1:?path to vocalsweetTesta}"; MS="${2:?path to moodfx_plugin}"
DST="$(cd "$(dirname "$0")/.." && pwd)/native/audio-effects/vendor"
cp "$VS"/VocalSweet/Source/dsp/*.h "$DST/vocalsweet/"
rsync -a --delete --exclude CMakeLists.txt "$MS/Source/Effects/" "$DST/moodshift/Effects/"
cp "$MS/Source/Parameters/ParameterIDs.h" "$DST/moodshift/Parameters/"
cp "$MS"/Source/Presets/FactoryPresets.* "$DST/moodshift/Presets/"
echo "vocalsweet: $(git -C "$VS" rev-parse HEAD)"
echo "moodshift:  $(git -C "$MS" rev-parse HEAD)"
echo "Update vendor/PROVENANCE.md with these commits, then run scripts/test-native.sh"
