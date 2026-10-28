#pragma once
// Offline music analysis (tempo and key) and noise reduction for recorded takes.
//
// Tempo and key are ported from MELOVAB STUDIO's analysis engine (Source/Core/Analysis.cpp:
// multi-band spectral-flux onset envelope -> autocorrelation with a mild tempo prior -> comb
// refinement; 8192-point chroma -> Krumhansl-Schmuckler key profiles), with JUCE replaced by a
// small FFT. Results are estimates with confidences: the UI always lets the user override them.

#include "AudioSource.h"
#include <string>
#include <vector>

namespace vox
{
struct MusicAnalysis
{
    bool valid = false;
    double bpm = 0.0;                 // best tempo (0 = not detected)
    float bpmConfidence = 0.0f;       // 0..1
    std::vector<double> bpmAlternatives;
    double beatOffsetSec = 0.0;       // first beat
    int keyRoot = -1;                 // 0 = C .. 11 = B, -1 = not detected
    bool keyMinor = false;
    float keyConfidence = 0.0f;       // 0..1
    int altKeyRoot = -1;              // runner-up (often the relative major/minor)
    bool altKeyMinor = false;
};

/** Analyses a source (any channel count). Heavy: control/worker thread only. */
MusicAnalysis analyseMusic (const AudioSource& src, float* progress = nullptr);

/** "A minor", "F# major" ... */
std::string keyName (int root, bool minor);

/** Spectral noise reduction for a recorded take. The noise profile is learned from the quietest
    parts of the audio itself (pauses between phrases), then every STFT bin is attenuated by up to
    `reductionDb` where it does not rise clearly above that profile. Voice components are kept;
    gains are smoothed over time and frequency to avoid "musical noise". Returns planar channels
    of the same length. */
std::vector<std::vector<float>> reduceNoise (const std::vector<std::vector<float>>& planar, double sampleRate,
                                             float reductionDb, float* progress = nullptr);

} // namespace vox
