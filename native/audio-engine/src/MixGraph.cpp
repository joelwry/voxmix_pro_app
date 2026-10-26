#include "vox/MixGraph.h"
#include <algorithm>
#include <cmath>

namespace vox
{
namespace
{
inline void panGains (float pan, bool balance, float& l, float& r) noexcept
{
    pan = std::clamp (pan, -1.0f, 1.0f);
    if (balance)
    {
        l = pan > 0.0f ? 1.0f - pan : 1.0f;
        r = pan < 0.0f ? 1.0f + pan : 1.0f;
    }
    else
    {
        // constant power, normalised to unity in the centre
        const float th = (pan + 1.0f) * 0.78539816f;
        l = 1.41421356f * std::cos (th);
        r = 1.41421356f * std::sin (th);
    }
}

/** Applies volume/pan/mute ramps from the channel's previous gains to its targets. */
inline void applyFader (ChannelControls& c, bool audible, bool balance, float* L, float* R, int n) noexcept
{
    float tl, tr;
    panGains (c.pan.load (std::memory_order_relaxed), balance, tl, tr);
    const float g = audible ? c.gain.load (std::memory_order_relaxed) : 0.0f;
    tl *= g; tr *= g;
    if (! c.primed) { c.curL = tl; c.curR = tr; c.primed = true; }
    const float sl = (tl - c.curL) / float (n), sr = (tr - c.curR) / float (n);
    float gl = c.curL, gr = c.curR;
    if (sl == 0.0f && sr == 0.0f)
    {
        for (int i = 0; i < n; ++i) { L[i] *= gl; R[i] *= gr; }
    }
    else
    {
        for (int i = 0; i < n; ++i)
        {
            gl += sl; gr += sr;
            L[i] *= gl; R[i] *= gr;
        }
    }
    c.curL = tl; c.curR = tr;
}

inline float clipGainAt (const ClipRT& c, int64_t r) noexcept
{
    float g = c.gain;
    if (c.fadeIn > 0 && r < c.fadeIn) g *= float (r) / float (c.fadeIn);
    if (c.fadeOut > 0 && r > c.length - c.fadeOut) g *= float (c.length - r) / float (c.fadeOut);
    return g;
}

/** Adds the part of clip c that overlaps [pos, pos+n) into L/R. Returns true if anything was added. */
bool renderClip (const ClipRT& c, int64_t pos, int n, float* L, float* R) noexcept
{
    const int64_t cs = c.start, ce = c.start + c.length;
    const int64_t s = std::max (pos, cs), e = std::min (pos + n, ce);
    if (s >= e || ! c.source) return false;
    // split at fade boundaries so the piecewise-linear gain is exact
    int64_t cuts[4] = { s, e, e, e };
    int numCuts = 1;
    const int64_t fi = cs + c.fadeIn, fo = ce - c.fadeOut;
    if (c.fadeIn > 0 && fi > s && fi < e) cuts[numCuts++] = fi;
    if (c.fadeOut > 0 && fo > s && fo < e && fo != fi) cuts[numCuts++] = fo;
    cuts[numCuts++] = e;
    std::sort (cuts, cuts + numCuts);
    for (int k = 0; k + 1 < numCuts; ++k)
    {
        const int64_t a = cuts[k], b = cuts[k + 1];
        if (b <= a) continue;
        const float g0 = clipGainAt (c, a - cs), g1 = clipGainAt (c, b - cs);
        c.source->addTo (L + (a - pos), R + (a - pos), c.offset + (a - cs), int (b - a), g0, g1);
    }
    return true;
}
} // namespace

void MixRenderer::prepare (int maxBlock, int maxBuses)
{
    const size_t m = (size_t) std::max (1, maxBlock);
    tL.assign (m, 0.0f); tR.assign (m, 0.0f);
    mL.assign (m, 0.0f); mR.assign (m, 0.0f);
    bL.assign ((size_t) std::max (1, maxBuses), std::vector<float> (m, 0.0f));
    bR.assign ((size_t) std::max (1, maxBuses), std::vector<float> (m, 0.0f));
}

void MixRenderer::render (MixGraph& g, int64_t pos, int n, bool playing, const MonitorInput& monitor,
                          float* outL, float* outR, LoudnessMeter* loudness) noexcept
{
    n = std::min (n, (int) tL.size());
    voxfx::ProcessInfo info;
    info.sampleRate = g.sampleRate;
    info.bpm = g.bpm;
    info.playing = playing;
    info.beatPosition = double (pos) / g.sampleRate * g.bpm / 60.0;

    std::fill (mL.begin(), mL.begin() + n, 0.0f);
    std::fill (mR.begin(), mR.begin() + n, 0.0f);
    const int numBuses = std::min ((int) g.buses.size(), (int) bL.size());
    for (int b = 0; b < numBuses; ++b)
    {
        std::fill (bL[(size_t) b].begin(), bL[(size_t) b].begin() + n, 0.0f);
        std::fill (bR[(size_t) b].begin(), bR[(size_t) b].begin() + n, 0.0f);
    }

    bool anySolo = false;
    for (const auto& t : g.tracks)
        if (t.controls->solo.load (std::memory_order_relaxed)) { anySolo = true; break; }

    for (int ti = 0; ti < (int) g.tracks.size(); ++ti)
    {
        auto& t = g.tracks[(size_t) ti];
        auto& c = *t.controls;
        std::fill (tL.begin(), tL.begin() + n, 0.0f);
        std::fill (tR.begin(), tR.begin() + n, 0.0f);

        bool signal = false;
        if (playing)
        {
            const int64_t readPos = pos + t.readAhead;
            for (const auto& clip : t.clips)
            {
                if (clip.start >= readPos + n) break;   // sorted by start
                signal |= renderClip (clip, readPos, n, tL.data(), tR.data());
            }
        }
        if (monitor.mono && monitor.trackIndex == ti)
        {
            for (int i = 0; i < n; ++i) { tL[(size_t) i] += monitor.mono[i]; tR[(size_t) i] += monitor.mono[i]; }
            signal = true;
        }

        c.silentFrames = signal ? 0 : c.silentFrames + n;
        const bool chainIdle = c.silentFrames > t.tailFrames + n;   // input silent and tails done
        if (t.chain && ! t.chain->empty() && ! chainIdle)
            t.chain->process (tL.data(), tR.data(), n, info);

        const bool audible = ! c.mute.load (std::memory_order_relaxed)
                             && (! anySolo || c.solo.load (std::memory_order_relaxed));
        applyFader (c, audible, t.stereoBalance, tL.data(), tR.data(), n);
        c.meter.process (tL.data(), tR.data(), n);

        float* dl = t.bus >= 0 && t.bus < numBuses ? bL[(size_t) t.bus].data() : mL.data();
        float* dr = t.bus >= 0 && t.bus < numBuses ? bR[(size_t) t.bus].data() : mR.data();
        for (int i = 0; i < n; ++i) { dl[i] += tL[(size_t) i]; dr[i] += tR[(size_t) i]; }
    }

    for (int b = 0; b < numBuses; ++b)
    {
        auto& bus = g.buses[(size_t) b];
        float* L = bL[(size_t) b].data();
        float* R = bR[(size_t) b].data();
        if (bus.chain && ! bus.chain->empty())
            bus.chain->process (L, R, n, info);
        applyFader (*bus.controls, ! bus.controls->mute.load (std::memory_order_relaxed), true, L, R, n);
        bus.controls->meter.process (L, R, n);
        for (int i = 0; i < n; ++i) { mL[(size_t) i] += L[i]; mR[(size_t) i] += R[i]; }
    }

    if (g.masterFx && ! g.masterFx->empty())
        g.masterFx->process (mL.data(), mR.data(), n, info);
    if (g.master)
        applyFader (*g.master, ! g.master->mute.load (std::memory_order_relaxed), true, mL.data(), mR.data(), n);
    if (g.mastering && ! g.mastering->empty())
        g.mastering->process (mL.data(), mR.data(), n, info);

    if (g.master) g.master->meter.process (mL.data(), mR.data(), n);
    if (loudness) loudness->process (mL.data(), mR.data(), n);

    for (int i = 0; i < n; ++i)
    {
        outL[i] = std::isfinite (mL[(size_t) i]) ? mL[(size_t) i] : 0.0f;
        outR[i] = std::isfinite (mR[(size_t) i]) ? mR[(size_t) i] : 0.0f;
    }
}

} // namespace vox
