#pragma once
// Compiles a Project into a MixGraph (control thread).
//
// The InstanceCache keeps effect slots and channel controls alive between rebuilds so the
// real-time graph keeps its DSP state (reverb tails, filter memories) when the structure
// changes. The offline renderer uses a fresh cache, i.e. fresh effect instances with the same
// parameters, so an export never disturbs live playback.

#include "MixGraph.h"
#include "Project.h"
#include <map>

namespace vox
{
using SourcePool = std::map<std::string, AudioSourcePtr>;

struct InstanceCache
{
    double sampleRate = 48000.0;
    int maxBlock = 256;
    std::map<std::string, voxfx::EffectSlotPtr> slots;   // by slot id
    std::map<std::string, ControlsPtr> controls;         // by track / bus id, "master"
};

/** Builds a new graph. Unknown effect types and missing sources are skipped and reported. */
MixGraphPtr buildGraph (const Project& p, const SourcePool& sources, InstanceCache& cache,
                        std::vector<std::string>* warnings = nullptr);

/** Pushes volume/pan/mute/solo and every effect parameter/bypass from the project into the
    cached instances (no rebuild needed). */
void applyControls (const Project& p, InstanceCache& cache);

/** Pushes one slot's parameters and bypass. */
void applySlot (const EffectSlotSpec& spec, voxfx::EffectSlot& slot);

/** Removes cache entries that the project no longer references. */
void pruneCache (const Project& p, InstanceCache& cache);

float dbToLinear (float db);

} // namespace vox
