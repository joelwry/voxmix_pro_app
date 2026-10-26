#pragma once
// JSON (de)serialisation of the project model plus crash-safe file writes.
//
// Safe write: project.json.tmp is written and flushed, the previous project.json is kept as
// project.json.bak, then the tmp file is renamed over project.json (atomic on POSIX). Loading
// falls back to the backup when the main file is missing or corrupt.

#include "Project.h"
#include <string>

namespace vox
{
struct LoadResult
{
    bool ok = false;
    bool usedBackup = false;
    bool migrated = false;
    std::string error;
    std::vector<std::string> warnings;   // e.g. missing media files
};

std::string projectToJson (const Project& p, bool pretty = true);
/** Parses and validates; unknown fields are ignored, missing ones get defaults, values are clamped. */
bool projectFromJson (const std::string& json, Project& out, std::string& error, bool* migrated = nullptr);

std::string effectSlotToJson (const EffectSlotSpec& s);

bool writeFileAtomic (const std::string& path, const std::string& contents, std::string& error);
bool readFile (const std::string& path, std::string& out);

/** Saves <folder>/project.json safely. */
bool saveProjectFile (const Project& p, const std::string& folder, std::string& error);
LoadResult loadProjectFile (const std::string& folder, Project& out);

} // namespace vox
