# Vendored DSP sources

Copied unchanged from the owner's plugin repositories. Do not edit these files here: fix them
upstream and copy again, so the plugins and the app keep the same DSP.

| Folder | Source repository | Commit | Files |
|---|---|---|---|
| `vocalsweet/` | `joelwry/vocalsweetTesta` | `c77604e3b123521c6cf8707f6d1cc92e37788916` | `VocalSweet/Source/dsp/*.h` |
| `moodshift/Effects/` | `joelwry/moodfx_plugin` | `64084b24221bc0ffb2b2e9ce7eda5b933ee25e12` | `Source/Effects/**` (without its CMakeLists.txt) |
| `moodshift/Parameters/` | same | same | `Source/Parameters/ParameterIDs.h` |
| `moodshift/Presets/` | same | same | `Source/Presets/FactoryPresets.{h,cpp}` |

The MELOVAB STUDIO YIN estimator (`joelwry/melovab-studio` @ `6376bb8a41cadb69247875e6b112b2d0328884f2`,
`Source/Core/Analysis.cpp`) was ported by hand into `../src/PitchCorrection.cpp`.

Update procedure: `scripts/sync-vendor.sh <path-to-vocalsweet> <path-to-moodfx>`, then run the
host tests (`scripts/test-native.sh`).
