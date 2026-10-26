#include "voxfx/EffectChain.h"
#include <algorithm>
#include <cmath>

namespace voxfx
{
EffectSlot::EffectSlot (std::string slotId, EffectPtr fx) : id (std::move (slotId)), effect (std::move (fx))
{
    latencyFrames = effect ? std::max (0, effect->latencySamples()) : 0;
    delayL.assign ((size_t) latencyFrames + 1, 0.0f);
    delayR.assign ((size_t) latencyFrames + 1, 0.0f);
}

EffectChain::EffectChain (std::vector<EffectSlotPtr> slots, double sr, int maxBlockSize)
    : slotList (std::move (slots))
{
    dryL.assign ((size_t) std::max (1, maxBlockSize), 0.0f);
    dryR.assign ((size_t) std::max (1, maxBlockSize), 0.0f);
    rampSamples = std::max (1, int (0.010 * sr));
}

EffectSlot* EffectChain::findSlot (const std::string& slotId) const noexcept
{
    for (const auto& s : slotList)
        if (s->id == slotId)
            return s.get();
    return nullptr;
}

int EffectChain::latencySamples() const noexcept
{
    int total = 0;
    for (const auto& s : slotList)
        total += s->latency();
    return total;
}

double EffectChain::tailSeconds() const noexcept
{
    double t = 0.0;
    for (const auto& s : slotList)
        t += s->effect->tailSeconds();
    return t;
}

void EffectChain::process (float* L, float* R, int n, const ProcessInfo& info) noexcept
{
    n = std::min (n, (int) dryL.size());
    for (const auto& sp : slotList)
    {
        auto& slot = *sp;
        const float target = slot.isBypassed() ? 0.0f : 1.0f;
        if (! slot.primed)
        {
            slot.wetGain = target;
            slot.primed = true;
        }
        const bool ramping = slot.wetGain != target;
        const bool bypassed = target == 0.0f && slot.wetGain == 0.0f;
        const int d = slot.latency();

        // dry path, delayed by the effect's latency (kept running so it is ready for bypass)
        if (d > 0)
        {
            const int size = d + 1;
            int pos = slot.delayPos;
            for (int i = 0; i < n; ++i)
            {
                slot.delayL[(size_t) pos] = L[i];
                slot.delayR[(size_t) pos] = R[i];
                int r = pos - d; if (r < 0) r += size;
                dryL[(size_t) i] = slot.delayL[(size_t) r];
                dryR[(size_t) i] = slot.delayR[(size_t) r];
                pos = (pos + 1) % size;
            }
            slot.delayPos = pos;
        }
        else if (ramping)
        {
            std::copy (L, L + n, dryL.begin());
            std::copy (R, R + n, dryR.begin());
        }

        if (bypassed)
        {
            if (d > 0)
            {
                std::copy (dryL.begin(), dryL.begin() + n, L);
                std::copy (dryR.begin(), dryR.begin() + n, R);
            }
            continue;   // fully bypassed: the effect is not processed at all
        }

        slot.effect->process (L, R, n, info);

        // guard: a misbehaving effect is reset and its output replaced by silence
        bool finite = true;
        for (int i = 0; i < n; ++i)
            if (! std::isfinite (L[i]) || ! std::isfinite (R[i])) { finite = false; break; }
        if (! finite)
        {
            slot.effect->reset();
            std::fill (L, L + n, 0.0f);
            std::fill (R, R + n, 0.0f);
        }

        if (ramping)
        {
            const float step = 1.0f / float (rampSamples);
            float g = slot.wetGain;
            for (int i = 0; i < n; ++i)
            {
                g = target > g ? std::min (target, g + step) : std::max (target, g - step);
                L[i] = dryL[(size_t) i] + g * (L[i] - dryL[(size_t) i]);
                R[i] = dryR[(size_t) i] + g * (R[i] - dryR[(size_t) i]);
            }
            slot.wetGain = g;
        }
    }
}

} // namespace voxfx
