#pragma once
#include <string>
#include <vector>
#include <sys/types.h>
namespace lsplt {
struct MapInfo { dev_t dev; ino_t inode; std::string path; static std::vector<MapInfo> Scan() { return {}; } };
inline bool RegisterHook(dev_t, ino_t, std::string_view, void *, void **) { return true; }
inline bool CommitHook() { return true; }
}
