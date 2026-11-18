#include "JsonEscape.h"

#include <cstdio>

namespace fw {

std::string JsonEscape(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 16);
    for (unsigned char c : s) {
        switch (c) {
        case '\"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (c < 0x20) {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out.push_back(static_cast<char>(c));
            }
            break;
        }
    }
    return out;
}

std::string BuildEventJson(
    const std::string& type,
    const std::string& itemType,
    const std::string& path,
    const std::string& oldPath,
    const std::string& newPath,
    const std::string& date,
    const std::string& content,
    bool contentTruncated,
    bool contentSkipped)
{
    std::string json;
    json.reserve(256 + content.size());
    json += "{\"type\":\"";
    json += JsonEscape(type);
    json += "\",\"itemType\":\"";
    json += JsonEscape(itemType);
    json += "\",\"path\":\"";
    json += JsonEscape(path);
    json += "\",\"oldPath\":\"";
    json += JsonEscape(oldPath);
    json += "\",\"newPath\":\"";
    json += JsonEscape(newPath);
    json += "\",\"date\":\"";
    json += JsonEscape(date);
    json += "\",\"content\":\"";
    json += JsonEscape(content);
    json += "\",\"contentTruncated\":";
    json += contentTruncated ? "true" : "false";
    json += ",\"contentSkipped\":";
    json += contentSkipped ? "true" : "false";
    json += "}";
    return json;
}

} // namespace fw
