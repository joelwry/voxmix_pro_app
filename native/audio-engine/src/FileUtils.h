#pragma once
// Minimal POSIX file helpers (Android + Linux + macOS).

#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <vector>
#include <cstdio>

namespace vox::fs
{
inline bool exists (const std::string& p)
{
    struct stat st {};
    return ::stat (p.c_str(), &st) == 0;
}

inline bool isDir (const std::string& p)
{
    struct stat st {};
    return ::stat (p.c_str(), &st) == 0 && S_ISDIR (st.st_mode);
}

inline int64_t fileSize (const std::string& p)
{
    struct stat st {};
    return ::stat (p.c_str(), &st) == 0 ? (int64_t) st.st_size : -1;
}

inline bool mkdirs (const std::string& path)
{
    if (path.empty() || isDir (path)) return true;
    const auto slash = path.find_last_of ('/');
    if (slash != std::string::npos && slash > 0 && ! mkdirs (path.substr (0, slash))) return false;
    return ::mkdir (path.c_str(), 0775) == 0 || isDir (path);
}

inline std::vector<std::string> list (const std::string& dir)
{
    std::vector<std::string> out;
    if (DIR* d = ::opendir (dir.c_str()))
    {
        while (dirent* e = ::readdir (d))
        {
            const std::string n = e->d_name;
            if (n != "." && n != "..") out.push_back (n);
        }
        ::closedir (d);
    }
    return out;
}

inline bool removeAll (const std::string& path)
{
    if (isDir (path))
    {
        for (const auto& n : list (path)) removeAll (path + "/" + n);
        return ::rmdir (path.c_str()) == 0;
    }
    return std::remove (path.c_str()) == 0;
}

/** Free bytes on the volume holding `path`, or -1 if unknown. */
inline int64_t freeSpace (const std::string& path)
{
    struct statvfs v {};
    if (::statvfs (path.c_str(), &v) != 0) return -1;
    return (int64_t) v.f_bavail * (int64_t) v.f_frsize;
}

} // namespace vox::fs
