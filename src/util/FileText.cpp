#include "FileText.h"
#include "StringConv.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace fw {

namespace {

bool LooksBinary(const std::vector<char>& data)
{
    const size_t n = data.size() < 512 ? data.size() : 512;
    for (size_t i = 0; i < n; ++i) {
        if (data[i] == '\0')
            return true;
    }
    return false;
}

#ifdef _WIN32
std::string Cp1251ToUtf8(const std::string& src)
{
    if (src.empty())
        return {};
    int wlen = MultiByteToWideChar(1251, 0, src.data(), static_cast<int>(src.size()), nullptr, 0);
    if (wlen <= 0)
        return src;
    std::wstring wide(static_cast<size_t>(wlen), L'\0');
    MultiByteToWideChar(1251, 0, src.data(), static_cast<int>(src.size()), &wide[0], wlen);
    return WideToUtf8(wide.c_str());
}
#endif

bool IsValidUtf8Bytes(const std::string& s)
{
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(s.data());
    size_t i = 0;
    const size_t n = s.size();
    while (i < n) {
        if (bytes[i] <= 0x7F) {
            ++i;
            continue;
        }
        size_t len = 0;
        if ((bytes[i] & 0xE0) == 0xC0)
            len = 2;
        else if ((bytes[i] & 0xF0) == 0xE0)
            len = 3;
        else if ((bytes[i] & 0xF8) == 0xF0)
            len = 4;
        else
            return false;
        if (i + len > n)
            return false;
        for (size_t j = 1; j < len; ++j) {
            if ((bytes[i + j] & 0xC0) != 0x80)
                return false;
        }
        i += len;
    }
    return true;
}

std::string FormatTimeT(std::time_t t)
{
    std::tm tmLocal{};
#ifdef _WIN32
    localtime_s(&tmLocal, &t);
#else
    localtime_r(&t, &tmLocal);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d",
                  tmLocal.tm_year + 1900, tmLocal.tm_mon + 1, tmLocal.tm_mday,
                  tmLocal.tm_hour, tmLocal.tm_min, tmLocal.tm_sec);
    return buf;
}

} // namespace

FileTextResult ReadFileText(const std::string& path, size_t maxBytes)
{
    FileTextResult result;
    if (path.empty() || maxBytes == 0) {
        result.skipped = true;
        return result;
    }

#ifdef _WIN32
    const std::wstring wpath = Utf8ToWide(path);
    HANDLE h = CreateFileW(wpath.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        result.skipped = true;
        return result;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(h, &size) || size.QuadPart < 0) {
        CloseHandle(h);
        result.skipped = true;
        return result;
    }
    const size_t fileSize = static_cast<size_t>(size.QuadPart);
    const size_t toRead = fileSize > maxBytes ? maxBytes : fileSize;
    result.truncated = fileSize > maxBytes;
    std::vector<char> buf(toRead);
    DWORD read = 0;
    if (toRead > 0) {
        if (!ReadFile(h, buf.data(), static_cast<DWORD>(toRead), &read, nullptr)) {
            CloseHandle(h);
            result.skipped = true;
            return result;
        }
        buf.resize(read);
    }
    CloseHandle(h);
#else
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        result.skipped = true;
        return result;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff fileSizeOff = in.tellg();
    if (fileSizeOff < 0) {
        result.skipped = true;
        return result;
    }
    const size_t fileSize = static_cast<size_t>(fileSizeOff);
    in.seekg(0, std::ios::beg);
    const size_t toRead = fileSize > maxBytes ? maxBytes : fileSize;
    result.truncated = fileSize > maxBytes;
    std::vector<char> buf(toRead);
    if (toRead > 0)
        in.read(buf.data(), static_cast<std::streamsize>(toRead));
    buf.resize(static_cast<size_t>(in.gcount()));
#endif

    if (LooksBinary(buf)) {
        result.skipped = true;
        result.text.clear();
        return result;
    }

    std::string raw(buf.begin(), buf.end());
    if (IsValidUtf8Bytes(raw)) {
        result.text = std::move(raw);
        return result;
    }

#ifdef _WIN32
    result.text = Cp1251ToUtf8(raw);
#else
    result.text = std::move(raw);
#endif
    return result;
}

ItemKind DetectItemKind(const std::string& path)
{
#ifdef _WIN32
    const std::wstring wpath = Utf8ToWide(path);
    const DWORD attrs = GetFileAttributesW(wpath.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES)
        return ItemKind::Unknown;
    return (attrs & FILE_ATTRIBUTE_DIRECTORY) ? ItemKind::Directory : ItemKind::File;
#else
    struct stat st{};
    if (stat(path.c_str(), &st) != 0)
        return ItemKind::Unknown;
    if (S_ISDIR(st.st_mode))
        return ItemKind::Directory;
    if (S_ISREG(st.st_mode))
        return ItemKind::File;
    return ItemKind::Unknown;
#endif
}

std::string FormatFileMtime(const std::string& path)
{
#ifdef _WIN32
    const std::wstring wpath = Utf8ToWide(path);
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(wpath.c_str(), GetFileExInfoStandard, &data))
        return FormatNowLocal();
    FILETIME ft = data.ftLastWriteTime;
    ULARGE_INTEGER ull{};
    ull.LowPart = ft.dwLowDateTime;
    ull.HighPart = ft.dwHighDateTime;
    // FILETIME is 100-ns intervals since 1601; convert to Unix epoch.
    const uint64_t unixTime = (ull.QuadPart - 116444736000000000ULL) / 10000000ULL;
    return FormatTimeT(static_cast<std::time_t>(unixTime));
#else
    struct stat st{};
    if (stat(path.c_str(), &st) != 0)
        return FormatNowLocal();
    return FormatTimeT(st.st_mtime);
#endif
}

std::string FormatNowLocal()
{
    const auto now = std::chrono::system_clock::now();
    return FormatTimeT(std::chrono::system_clock::to_time_t(now));
}

std::string JoinPath(const std::string& dir, const std::string& name)
{
    if (dir.empty())
        return name;
    if (name.empty())
        return dir;
#ifdef _WIN32
    const char sep = '\\';
    const bool hasSep = dir.back() == '\\' || dir.back() == '/';
#else
    const char sep = '/';
    const bool hasSep = dir.back() == '/';
#endif
    if (hasSep)
        return dir + name;
    return dir + sep + name;
}

} // namespace fw
