#pragma once

#include <cstdint>
#include <string>

namespace fw {

std::string Utf16ToUtf8(const char16_t* src, size_t len);
std::string Utf16ToUtf8(const std::u16string& src);
std::u16string Utf8ToUtf16(const std::string& src);

#ifdef _WIN32
std::string WideToUtf8(const wchar_t* src);
std::wstring Utf8ToWide(const std::string& src);
#endif

} // namespace fw
