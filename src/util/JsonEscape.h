#pragma once

#include <string>

namespace fw {

std::string JsonEscape(const std::string& s);

std::string BuildEventJson(
    const std::string& type,
    const std::string& itemType,
    const std::string& path,
    const std::string& oldPath,
    const std::string& newPath,
    const std::string& date,
    const std::string& content,
    bool contentTruncated,
    bool contentSkipped);

} // namespace fw
