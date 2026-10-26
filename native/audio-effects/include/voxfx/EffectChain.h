#pragma once
// An ordered chain of effect slots, processed in place on a stereo buffer.
//
// The structure of a chain (which slots, in which order) is immutable once built: the engine
// builds a new chain on the control thread when slots are added, removed or reordered, re-using
// the existing Effect instances (so reverb tails and filter states carry over), and swaps it in
// atomically. Parameters and bypass are changed in place through atomics.
// Bypass crossfades over 10 ms, so toggling it never clicks. A bypassed slot delays the dry
// signal by the effect's latency, so a chain's latency is constant whatever is bypassed (the
// mixer's delay compensation relies on that).

#include "Effect.h"

namespace voxfx
{
class EffectSlot
{
public:
    /** fx must already be prepared (its latency is read here). Control thread. */
    EffectSlot (std::string slotId, EffectPtr fx);

    const std::string id;
    const EffectPtr effect;

    void setBypassed (bool b) noexcept { bypass.store (b, std::memory_order_relaxed); }
    bool isBypassed() const noexcept { return bypass.load (std::memory_order_relaxed); }

    int latency() const noexcept { return latencyFrames; }

    // audio-thread state
    float wetGain = 1.0f;     // 1 = active, 0 = bypassed (ramped)
    bool primed = false;
    std::vector<float> delayL, delayR;   // dry-path delay matching the effect latency
    int delayPos = 0;

private:
    std::atomic<bool> bypass { false };
    int latencyFrames = 0;
};

using EffectSlotPtr = std::shared_ptr<EffectSlot>;

class EffectChain
{
public:
    EffectChain() = default;
    EffectChain (std::vector<EffectSlotPtr> slots, double sampleRate, int maxBlockSize);

    /** Real-time safe. numFrames must be <= the prepared max block size. */
    void process (float* left, float* right, int numFrames, const ProcessInfo& info) noexcept;

    const std::vector<EffectSlotPtr>& slots() const noexcept { return slotList; }
    EffectSlot* findSlot (const std::string& slotId) const noexcept;
    int latencySamples() const noexcept;
    double tailSeconds() const noexcept;
    bool empty() const noexcept { return slotList.empty(); }

private:
    std::vector<EffectSlotPtr> slotList;
    std::vector<float> dryL, dryR;
    double sampleRate = 48000.0;
    int rampSamples = 480;
};

using EffectChainPtr = std::shared_ptr<EffectChain>;

} // namespace voxfx
