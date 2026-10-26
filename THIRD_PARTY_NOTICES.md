# Third-party notices

| Component | Use | Licence | Obligations |
|---|---|---|---|
| React Native 0.87.1, React 19 | UI framework | MIT | keep copyright notice |
| Hermes | JS engine (in React Native) | MIT | |
| react-native-safe-area-context | safe-area insets | MIT | |
| Google Oboe 1.10.0 | low-latency Android audio | Apache-2.0 | keep notice; state changes (none) |
| AndroidX core-ktx | FileProvider, utilities | Apache-2.0 | |
| dr_wav, dr_mp3 (mackron/dr_libs @ a466dbe) | WAV/MP3 decoding | Public domain (Unlicense) or MIT-0 | none |
| nlohmann/json 3.12.0 | JSON | MIT | keep notice |
| VocalSweet DSP (`native/audio-effects/vendor/vocalsweet`) | vocal processing | owner's own code | — |
| MoodShift FX DSP (`native/audio-effects/vendor/moodshift`) | creative effects | owner's own code (proprietary) | — |
| MELOVAB STUDIO YIN (ported) | pitch detection | owner's own code | — |

Not used: JUCE and the VST3 SDK (only the plugin wrappers depended on them), LAME (MP3
encoding is deferred until its LGPL obligations are handled, e.g. as a replaceable shared
library), any audio samples, fonts or artwork from other products.

Algorithms implemented from public descriptions: ITU-R BS.1770-4 loudness (K-weighting
coefficients as derived in libebur128, MIT), YIN (de Cheveigné & Kawahara 2002), WSOLA,
Simper TPT state-variable filters, Kaiser-windowed sinc resampling.
