#pragma once
// Common interface for every VoxMix effect (layer, bus and master FX).
//
// Threading contract
//  - Constructor, prepare(): control thread only. All memory is allocated here.
//  - setParam(): any thread. Values are clamped to the declared range and stored in atomics,
//    so invalid values never reach the DSP.
//  - process(), reset(): audio thread (or the offline renderer's thread). No allocation,
//    locking or I/O. Effects read their parameter atomics at the start of each block.
//
// Audio format: 32-bit float, non-interleaved stereo, processed in place. Mono material is
// duplicated to both channels before the insert chain so stereo effects (reverb, doubler,
// width) behave the same on every track.

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace voxfx
{
enum class ParamType
{
    Continuous,
    Integer,
    Toggle,
    Choice
};

struct ParamInfo
{
    std::string id;     // stable, saved in projects and presets: never rename
    std::string name;
    ParamType type = ParamType::Continuous;
    float min = 0.0f, max = 1.0f, def = 0.0f;
    std::string unit;
    bool logScale = false;              // UI hint: slider uses a logarithmic mapping
    std::vector<std::string> choices;   // for Choice
};

enum class EffectCategory
{
    Pitch,      // pitch correction and tuning
    Dynamics,   // EQ, compression, de-essing, gate, saturation, limiting
    Space,      // reverb, delay, chorus, width
    Creative,   // MoodShift FX library
    Chain,      // complete multi-module chains (VocalSweet)
    Mastering
};

const char* categoryId (EffectCategory c) noexcept;

/** Static description of an effect type (shared by all instances). */
struct EffectDescriptor
{
    std::string typeId;     // stable, saved in projects
    std::string name;
    EffectCategory category = EffectCategory::Dynamics;
    std::string description;
    std::string origin;     // where the DSP comes from (e.g. "VocalSweet", "MoodShift FX", "VoxMix")
    std::vector<ParamInfo> params;
};

/** Transport information handed to tempo-aware effects. */
struct ProcessInfo
{
    double sampleRate = 48000.0;
    double bpm = 120.0;
    double beatPosition = 0.0;  // quarter notes at the first sample of the block
    bool playing = false;
};

class Effect
{
public:
    explicit Effect (const EffectDescriptor& d);
    virtual ~Effect() = default;

    const EffectDescriptor& descriptor() const noexcept { return desc; }
    int numParams() const noexcept { return (int) desc.params.size(); }
    int paramIndex (const std::string& id) const noexcept;

    /** Clamps (and rounds for discrete params) then stores. Thread safe, real-time safe. */
    void setParam (int index, float value) noexcept;
    float getParam (int index) const noexcept;
    static float sanitise (const ParamInfo& p, float value) noexcept;

    virtual void prepare (double sampleRate, int maxBlockSize) = 0;
    virtual void reset() = 0;
    virtual void process (float* left, float* right, int numFrames, const ProcessInfo& info) noexcept = 0;
    virtual int latencySamples() const noexcept { return 0; }
    virtual double tailSeconds() const noexcept { return 0.0; }

    /** Optional metering value (e.g. gain reduction in dB); 0 when not applicable. */
    virtual float gainReductionDb() const noexcept { return 0.0f; }

protected:
    float p (int index) const noexcept { return values[(size_t) index].load (std::memory_order_relaxed); }

private:
    const EffectDescriptor& desc;
    std::unique_ptr<std::atomic<float>[]> values;
};

using EffectPtr = std::shared_ptr<Effect>;

} // namespace voxfx
