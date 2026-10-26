#include "vox/GraphBuilder.h"
#include "voxfx/EffectRegistry.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace vox
{
float dbToLinear (float db) { return db <= -60.0f ? 0.0f : std::pow (10.0f, db * 0.05f); }

namespace
{
ControlsPtr controlsFor (InstanceCache& cache, const std::string& id)
{
    auto& c = cache.controls[id];
    if (! c) c = std::make_shared<ChannelControls>();
    return c;
}

voxfx::EffectSlotPtr slotFor (InstanceCache& cache, const EffectSlotSpec& spec, std::vector<std::string>* warnings)
{
    auto it = cache.slots.find (spec.id);
    if (it != cache.slots.end() && it->second->effect->descriptor().typeId == spec.type)
        return it->second;
    auto fx = voxfx::EffectRegistry::instance().create (spec.type);
    if (! fx)
    {
        if (warnings) warnings->push_back ("Unknown effect type '" + spec.type + "' was skipped");
        return nullptr;
    }
    fx->prepare (cache.sampleRate, cache.maxBlock);
    auto slot = std::make_shared<voxfx::EffectSlot> (spec.id, fx);
    applySlot (spec, *slot);
    fx->reset();   // start from the applied parameters, not the defaults
    cache.slots[spec.id] = slot;
    return slot;
}

voxfx::EffectChainPtr chainFor (InstanceCache& cache, const std::vector<EffectSlotSpec>& specs, std::vector<std::string>* warnings)
{
    std::vector<voxfx::EffectSlotPtr> slots;
    for (const auto& s : specs)
        if (auto slot = slotFor (cache, s, warnings))
            slots.push_back (slot);
    return std::make_shared<voxfx::EffectChain> (std::move (slots), cache.sampleRate, cache.maxBlock);
}

void applyChannel (ChannelControls& c, float volumeDb, float pan, bool mute, bool solo)
{
    c.gain.store (dbToLinear (volumeDb), std::memory_order_relaxed);
    c.pan.store (std::clamp (pan, -1.0f, 1.0f), std::memory_order_relaxed);
    c.mute.store (mute, std::memory_order_relaxed);
    c.solo.store (solo, std::memory_order_relaxed);
}
} // namespace

void applySlot (const EffectSlotSpec& spec, voxfx::EffectSlot& slot)
{
    auto& fx = *slot.effect;
    const auto& params = fx.descriptor().params;
    for (int i = 0; i < (int) params.size(); ++i)
    {
        auto it = spec.params.find (params[(size_t) i].id);
        fx.setParam (i, it != spec.params.end() ? it->second : params[(size_t) i].def);
    }
    slot.setBypassed (spec.bypass);
}

MixGraphPtr buildGraph (const Project& p, const SourcePool& sources, InstanceCache& cache, std::vector<std::string>* warnings)
{
    auto g = std::make_shared<MixGraph>();
    g->sampleRate = cache.sampleRate;
    g->maxBlock = cache.maxBlock;
    g->bpm = p.bpm;

    for (const auto& b : p.buses)
    {
        BusRT bus;
        bus.id = b.id;
        bus.controls = controlsFor (cache, b.id);
        applyChannel (*bus.controls, b.volumeDb, b.pan, b.mute, false);
        bus.chain = chainFor (cache, b.fx, warnings);
        bus.latency = bus.chain->latencySamples();
        g->buses.push_back (std::move (bus));
    }

    for (const auto& t : p.tracks)
    {
        TrackRT tr;
        tr.id = t.id;
        tr.controls = controlsFor (cache, t.id);
        applyChannel (*tr.controls, t.volumeDb, t.pan, t.mute, t.solo);
        tr.chain = chainFor (cache, t.fx, warnings);
        tr.bus = -1;
        for (size_t b = 0; b < p.buses.size(); ++b)
            if (p.buses[b].id == t.output) tr.bus = (int) b;
        tr.readAhead = tr.chain->latencySamples() + (tr.bus >= 0 ? g->buses[(size_t) tr.bus].latency : 0);
        tr.tailFrames = (int64_t) (tr.chain->tailSeconds() * cache.sampleRate);
        bool stereo = false;
        for (const auto& c : t.clips)
        {
            if (c.muted || c.length <= 0) continue;
            auto it = sources.find (c.sourceId);
            if (it == sources.end() || ! it->second)
            {
                if (warnings) warnings->push_back ("Audio for clip '" + (c.name.empty() ? c.id : c.name) + "' is missing");
                continue;
            }
            ClipRT rt;
            rt.source = it->second;
            rt.start = c.start;
            rt.offset = std::max<int64_t> (0, c.offset);
            rt.length = std::min (c.length, std::max<int64_t> (0, it->second->frames() - rt.offset));
            rt.fadeIn = std::min (c.fadeIn, rt.length);
            rt.fadeOut = std::min (c.fadeOut, rt.length);
            rt.gain = dbToLinear (c.gainDb);
            if (rt.length <= 0) continue;
            stereo |= it->second->channels() > 1;
            g->endFrame = std::max (g->endFrame, rt.start + rt.length);
            tr.clips.push_back (std::move (rt));
        }
        std::sort (tr.clips.begin(), tr.clips.end(), [] (const ClipRT& a, const ClipRT& b) { return a.start < b.start; });
        tr.stereoBalance = stereo || t.kind == TrackKind::Beat;
        g->tracks.push_back (std::move (tr));
    }

    g->master = controlsFor (cache, "master");
    applyChannel (*g->master, p.master.volumeDb, 0.0f, false, false);
    g->masterFx = chainFor (cache, p.master.fx, warnings);
    EffectSlotSpec m = p.master.mastering;
    if (m.id.empty()) m.id = "mastering";
    m.type = "mastering";
    m.bypass = ! p.master.masteringOn;
    g->mastering = chainFor (cache, { m }, warnings);
    g->masterLatency = g->masterFx->latencySamples() + g->mastering->latencySamples();
    return g;
}

void applyControls (const Project& p, InstanceCache& cache)
{
    auto applyChain = [&cache] (const std::vector<EffectSlotSpec>& fx)
    {
        for (const auto& s : fx)
            if (auto it = cache.slots.find (s.id); it != cache.slots.end())
                applySlot (s, *it->second);
    };
    for (const auto& t : p.tracks)
    {
        applyChannel (*controlsFor (cache, t.id), t.volumeDb, t.pan, t.mute, t.solo);
        applyChain (t.fx);
    }
    for (const auto& b : p.buses)
    {
        applyChannel (*controlsFor (cache, b.id), b.volumeDb, b.pan, b.mute, false);
        applyChain (b.fx);
    }
    applyChannel (*controlsFor (cache, "master"), p.master.volumeDb, 0.0f, false, false);
    applyChain (p.master.fx);
    EffectSlotSpec m = p.master.mastering;
    if (m.id.empty()) m.id = "mastering";
    m.bypass = ! p.master.masteringOn;
    applyChain ({ m });
}

void pruneCache (const Project& p, InstanceCache& cache)
{
    std::set<std::string> slotIds, ctlIds { "master" };
    auto add = [&slotIds] (const std::vector<EffectSlotSpec>& fx) { for (const auto& s : fx) slotIds.insert (s.id); };
    for (const auto& t : p.tracks) { add (t.fx); ctlIds.insert (t.id); }
    for (const auto& b : p.buses) { add (b.fx); ctlIds.insert (b.id); }
    add (p.master.fx);
    slotIds.insert (p.master.mastering.id.empty() ? "mastering" : p.master.mastering.id);
    for (auto it = cache.slots.begin(); it != cache.slots.end();)
        it = slotIds.count (it->first) ? std::next (it) : cache.slots.erase (it);
    for (auto it = cache.controls.begin(); it != cache.controls.end();)
        it = ctlIds.count (it->first) ? std::next (it) : cache.controls.erase (it);
}

} // namespace vox
