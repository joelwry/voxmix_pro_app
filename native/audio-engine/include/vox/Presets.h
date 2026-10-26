#pragma once
// Creative preset system. A preset stores a complete effect chain: effect types, their order,
// every parameter value and bypass state. Factory presets are compiled in and read-only; user
// presets live in <dataDir>/presets/user-presets.json (versioned, written atomically).

#include "Project.h"
#include <string>
#include <vector>

namespace vox
{
struct ChainPreset
{
    std::string id;
    std::string name;
    std::string category;      // "vocal", "creative", "space", "master", "user"
    std::string description;
    bool factory = true;
    std::vector<EffectSlotSpec> slots;   // slot ids are regenerated when applied
};

struct MasteringPreset
{
    std::string id, name, description;
    float loudnessTarget = -14.0f;
    std::map<std::string, float> params;
};

const std::vector<ChainPreset>& factoryChainPresets();
const std::vector<MasteringPreset>& masteringPresets();
const MasteringPreset* findMasteringPreset (const std::string& id);

class UserPresetStore
{
public:
    void setFile (const std::string& path);
    bool load (std::string& error);
    bool save (std::string& error) const;

    const std::vector<ChainPreset>& presets() const noexcept { return list; }
    const ChainPreset* find (const std::string& id) const;
    ChainPreset& add (ChainPreset p);
    bool rename (const std::string& id, const std::string& name);
    bool remove (const std::string& id);

private:
    std::string file;
    std::vector<ChainPreset> list;
};

std::string presetToJson (const ChainPreset& p);

} // namespace vox
