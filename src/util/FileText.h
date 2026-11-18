#pragma once

#include <cstdint>
#include <string>

namespace fw {

struct FileTextResult
{
    std::string text;
    bool truncated = false;
    bool skipped = false;
};

FileTextResult ReadFileText(const std::string& path, size_t maxBytes);

enum class ItemKind {
    Unknown,
    File,
    Directory
};

ItemKind DetectItemKind(const std::string& path);
std::string FormatFileMtime(const std::string& path);
std::string FormatNowLocal();
std::string JoinPath(const std::string& dir, const std::string& name);

} // namespace fw
