#include "StringConv.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fw {

std::string Utf16ToUtf8(const char16_t* src, size_t len)
{
    std::string out;
    out.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        uint32_t cp = src[i];
        if (cp >= 0xD800 && cp <= 0xDBFF) {
            if (i + 1 < len) {
                uint32_t low = src[i + 1];
                if (low >= 0xDC00 && low <= 0xDFFF) {
                    cp = 0x10000 + (((cp - 0xD800) << 10) | (low - 0xDC00));
                    ++i;
                }
            }
        }
        if (cp <= 0x7F) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

std::string Utf16ToUtf8(const std::u16string& src)
{
    return Utf16ToUtf8(src.data(), src.size());
}

std::u16string Utf8ToUtf16(const std::string& src)
{
    std::u16string out;
    out.reserve(src.size());
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(src.data());
    size_t i = 0;
    const size_t n = src.size();
    while (i < n) {
        uint32_t cp = 0;
        if (bytes[i] <= 0x7F) {
            cp = bytes[i++];
        } else if ((bytes[i] & 0xE0) == 0xC0 && i + 1 < n) {
            cp = ((bytes[i] & 0x1F) << 6) | (bytes[i + 1] & 0x3F);
            i += 2;
        } else if ((bytes[i] & 0xF0) == 0xE0 && i + 2 < n) {
            cp = ((bytes[i] & 0x0F) << 12) | ((bytes[i + 1] & 0x3F) << 6) | (bytes[i + 2] & 0x3F);
            i += 3;
        } else if ((bytes[i] & 0xF8) == 0xF0 && i + 3 < n) {
            cp = ((bytes[i] & 0x07) << 18) | ((bytes[i + 1] & 0x3F) << 12) |
                 ((bytes[i + 2] & 0x3F) << 6) | (bytes[i + 3] & 0x3F);
            i += 4;
        } else {
            cp = '?';
            ++i;
        }
        if (cp <= 0xFFFF) {
            out.push_back(static_cast<char16_t>(cp));
        } else {
            cp -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
        }
    }
    return out;
}

#ifdef _WIN32
std::string WideToUtf8(const wchar_t* src)
{
    if (!src)
        return {};
    int needed = WideCharToMultiByte(CP_UTF8, 0, src, -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 1)
        return {};
    std::string out(static_cast<size_t>(needed - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, src, -1, &out[0], needed, nullptr, nullptr);
    return out;
}

std::wstring Utf8ToWide(const std::string& src)
{
    if (src.empty())
        return {};
    int needed = MultiByteToWideChar(CP_UTF8, 0, src.data(), static_cast<int>(src.size()), nullptr, 0);
    if (needed <= 0)
        return {};
    std::wstring out(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, src.data(), static_cast<int>(src.size()), &out[0], needed);
    return out;
}
#endif

} // namespace fw
