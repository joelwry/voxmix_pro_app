#include "vox/Project.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <random>

namespace vox
{
TrackSpec* Project::findTrack (const std::string& tid)
{
    for (auto& t : tracks) if (t.id == tid) return &t;
    return nullptr;
}

const TrackSpec* Project::findTrack (const std::string& tid) const
{
    for (auto& t : tracks) if (t.id == tid) return &t;
    return nullptr;
}

BusSpec* Project::findBus (const std::string& bid)
{
    for (auto& b : buses) if (b.id == bid) return &b;
    return nullptr;
}

const SourceSpec* Project::findSource (const std::string& sid) const
{
    for (auto& s : sources) if (s.id == sid) return &s;
    return nullptr;
}

ClipSpec* Project::findClip (const std::string& clipId, TrackSpec** owner)
{
    for (auto& t : tracks)
        for (auto& c : t.clips)
            if (c.id == clipId)
            {
                if (owner) *owner = &t;
                return &c;
            }
    return nullptr;
}

int Project::vocalLayerCount() const
{
    return (int) std::count_if (tracks.begin(), tracks.end(), [] (const TrackSpec& t) { return t.kind == TrackKind::Vocal; });
}

int64_t Project::contentEnd() const
{
    int64_t end = 0;
    for (const auto& t : tracks)
        for (const auto& c : t.clips)
            end = std::max (end, c.start + c.length);
    return end;
}

std::string makeId (const std::string& prefix)
{
    static std::atomic<uint32_t> counter { 0 };
    static const uint32_t seed = (uint32_t) std::random_device {}() ^ (uint32_t) nowMs();
    const uint32_t v = seed * 2654435761u + (counter.fetch_add (1) + 1) * 40503u;
    static const char* hex = "0123456789abcdef";
    std::string s = prefix + "-";
    for (int i = 0; i < 8; ++i) s += hex[(v >> (i * 4)) & 15];
    return s;
}

int64_t nowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds> (system_clock::now().time_since_epoch()).count();
}

} // namespace vox
