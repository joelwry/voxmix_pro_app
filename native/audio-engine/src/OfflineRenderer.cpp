#include "vox/OfflineRenderer.h"
#include "vox/WavWriter.h"
#include <algorithm>
#include <cmath>
#include <sys/stat.h>

namespace vox
{
namespace
{
constexpr int kBlock = 256;
inline float toDb (float lin) { return lin > 0.0f ? 20.0f * std::log10 (lin) : -144.0f; }
} // namespace

RenderReport renderProject (const Project& projectIn, const SourcePool& sources, const RenderOptions& opts, RenderProgress* progress)
{
    RenderReport rep;
    Project p = projectIn;
    if (opts.masteringOverride) p.master.masteringOn = opts.masteringOn;

    InstanceCache cache;
    cache.sampleRate = p.sampleRate;
    cache.maxBlock = kBlock;
    std::vector<std::string> warnings;
    auto graph = buildGraph (p, sources, cache, &warnings);
    applyControls (p, cache);

    const int64_t contentEnd = graph->endFrame;
    int64_t start = std::max<int64_t> (0, opts.startFrame);
    int64_t end = opts.endFrame > start ? opts.endFrame : contentEnd;
    if (end <= start) { rep.error = "There is nothing to export yet: record or import audio first."; return rep; }

    int64_t tail = 0;
    if (opts.includeTail)
    {
        double t = 0.0;
        for (const auto& tr : graph->tracks) t = std::max (t, tr.chain->tailSeconds());
        for (const auto& b : graph->buses) t = std::max (t, b.chain->tailSeconds());
        t += graph->masterFx->tailSeconds();
        tail = (int64_t) (std::min (6.0, std::max (0.5, t)) * p.sampleRate);
    }
    // the master chain latency is removed from the start so the file is sample-aligned
    const int64_t latency = graph->masterLatency;
    const int64_t total = end - start + tail;

    const int outRate = opts.sampleRate > 0 ? opts.sampleRate : p.sampleRate;
    const int outCh = std::clamp (opts.channels, 1, 2);
    const bool resample = outRate != p.sampleRate;

    MixRenderer renderer;
    renderer.prepare (kBlock, (int) std::max<size_t> (1, graph->buses.size()));
    LoudnessMeter loud;
    loud.prepare (p.sampleRate);

    WavWriter writer;
    std::string err;
    if (opts.writeFile && ! resample && ! writer.open (opts.path, outRate, outCh, opts.bitDepth, err))
    {
        rep.error = err;
        return rep;
    }

    std::vector<float> L (kBlock), R (kBlock), inter (kBlock * 2);
    std::vector<float> all;   // only used when resampling
    if (resample && opts.writeFile) all.reserve ((size_t) (total * outCh));
    float peak = 0.0f;
    double sumSq = 0.0;
    int64_t rendered = 0, skip = latency;
    MonitorInput none;
    int64_t pos = start;
    while (rendered < total)
    {
        if (progress && progress->cancel.load())
        {
            rep.cancelled = true;
            rep.error = "Export cancelled";
            writer.close();
            if (opts.writeFile) std::remove (opts.path.c_str());
            return rep;
        }
        renderer.render (*graph, pos, kBlock, true, none, L.data(), R.data(), &loud);
        pos += kBlock;
        int first = 0;
        if (skip > 0) { first = (int) std::min<int64_t> (skip, kBlock); skip -= first; }
        const int n = (int) std::min<int64_t> (kBlock - first, total - rendered);
        if (n <= 0) continue;
        for (int i = 0; i < n; ++i)
        {
            const float l = L[(size_t) (first + i)], r = R[(size_t) (first + i)];
            peak = std::max (peak, std::max (std::abs (l), std::abs (r)));
            sumSq += 0.5 * (double (l) * l + double (r) * r);
            if (outCh == 2) { inter[(size_t) (2 * i)] = l; inter[(size_t) (2 * i + 1)] = r; }
            else inter[(size_t) i] = 0.5f * (l + r);
        }
        if (opts.writeFile)
        {
            if (resample) all.insert (all.end(), inter.begin(), inter.begin() + n * outCh);
            else if (! writer.write (inter.data(), n))
            {
                writer.close();
                std::remove (opts.path.c_str());
                rep.error = "Storage is full: the export could not be written.";
                return rep;
            }
        }
        rendered += n;
        if (progress) progress->progress.store (float (rendered) / float (total) * (resample ? 0.6f : 0.95f));
    }

    if (opts.writeFile && resample)
    {
        float rp = 0.0f;
        auto rs = resampleInterleaved (all, outCh, p.sampleRate, outRate, &rp);
        if (! writer.open (opts.path, outRate, outCh, opts.bitDepth, err)) { rep.error = err; return rep; }
        if (! writer.write (rs.data(), (int64_t) rs.size() / outCh))
        {
            writer.close();
            std::remove (opts.path.c_str());
            rep.error = "Storage is full: the export could not be written.";
            return rep;
        }
        if (progress) progress->progress.store (0.95f);
    }
    if (opts.writeFile && ! writer.close())
    {
        rep.error = "The exported file could not be finalised.";
        return rep;
    }

    rep.path = opts.path;
    rep.sampleRate = outRate;
    rep.channels = outCh;
    rep.bitDepth = opts.bitDepth;
    rep.frames = opts.writeFile ? writer.framesWritten() : rendered;
    rep.seconds = double (rep.frames) / outRate;
    rep.peakDb = toDb (peak);
    rep.truePeakDb = loud.truePeakDb();
    rep.integratedLufs = loud.integrated();
    rep.rmsDb = rendered > 0 ? 10.0f * std::log10 (std::max (1e-20, sumSq / double (rendered))) : -144.0f;
    rep.clipped = peak > 1.0f;

    if (opts.writeFile)
    {
        struct stat st {};
        if (::stat (opts.path.c_str(), &st) == 0) rep.fileBytes = (int64_t) st.st_size;
        // validation: decode the file again and compare
        DecodedAudio check;
        std::string verr;
        if (! decodeAudioFile (opts.path, check, verr))
            rep.validationNote = "Validation failed: " + verr;
        else
        {
            float vpeak = 0.0f;
            for (float v : check.interleaved) vpeak = std::max (vpeak, std::abs (v));
            const bool framesOk = check.frames() == rep.frames;
            const bool chOk = check.channels == outCh;
            const bool rateOk = check.sampleRate == outRate;
            const bool peakOk = std::abs (std::min (vpeak, 1.0f) - std::min (peak, 1.0f)) < (resample ? 0.1f : 0.01f);
            rep.validated = framesOk && chOk && rateOk && peakOk;
            if (! rep.validated)
                rep.validationNote = std::string ("Validation mismatch:") + (framesOk ? "" : " duration") + (chOk ? "" : " channels")
                                     + (rateOk ? "" : " sample rate") + (peakOk ? "" : " level");
            else if (peak < 1e-5f)
                rep.validationNote = "The exported song is silent: check mutes and levels.";
        }
    }
    rep.ok = true;
    if (progress) progress->progress.store (1.0f);
    return rep;
}

} // namespace vox
