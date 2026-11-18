#include "FileWatcherComponent.h"

#include "util/StringConv.h"
#include "util/JsonEscape.h"

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <filesystem>
#include <fstream>
#include <limits>
#endif
#include <charconv>
#include <unordered_set>

#include <cstring>
#include <vector>

namespace {
#ifdef _WIN32
std::string Sha256(const std::string& input, bool file)
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    unsigned char output[32]{};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        return {};

    auto status = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0);
    if (status >= 0) {
        if (file) {
            std::ifstream stream(std::filesystem::u8path(input), std::ios::binary);
            if (!stream) {
                status = -1;
            } else {
                std::array<char, 64 * 1024> buffer;
                while (status >= 0 && stream) {
                    stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                    if (stream.gcount() != 0)
                        status = BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()),
                            static_cast<ULONG>(stream.gcount()), 0);
                }
                if (stream.bad() || !stream.eof())
                    status = -1;
            }
        } else if (input.size() <= (std::numeric_limits<ULONG>::max)()) {
            status = BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(input.data())),
                static_cast<ULONG>(input.size()), 0);
        } else {
            status = -1;
        }
        if (status >= 0)
            status = BCryptFinishHash(hash, output, static_cast<ULONG>(sizeof(output)), 0);
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0)
        return {};

    const char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(sizeof(output) * 2);
    for (const auto byte : output) {
        result += digits[byte >> 4];
        result += digits[byte & 15];
    }
    return result;
}
#endif

constexpr char16_t kExtensionName[] = u"FileWatcher";
constexpr int32_t kDefaultMaxContentBytes = 1024 * 1024;

const char16_t* const kMethodNames[] = {
    u"StartWatch",
    u"StopWatch",
    u"StopAll",
    u"Version",
    u"IsWatching",
    u"GetLastError",
    u"GetMaxContentBytes",
    u"SetMaxContentBytes",
    u"GetIncludeSubdirectories",
    u"GetWatchList",
    u"GetWatchRevision",
    u"GetWatchChanges",
    u"Sha256"
};

// Unicode escapes: independent of source file encoding / MSVC code page
const char16_t* const kMethodNamesRu[] = {
    u"\u041d\u0430\u0447\u0430\u0442\u044c\u0421\u043b\u0435\u0436\u0435\u043d\u0438\u0435",                 // НачатьСлежение
    u"\u041e\u0441\u0442\u0430\u043d\u043e\u0432\u0438\u0442\u044c\u0421\u043b\u0435\u0436\u0435\u043d\u0438\u0435", // ОстановитьСлежение
    u"\u041e\u0441\u0442\u0430\u043d\u043e\u0432\u0438\u0442\u044c\u0412\u0441\u0435",                         // ОстановитьВсе
    u"\u0412\u0435\u0440\u0441\u0438\u044f",                                                                   // Версия
    u"\u0421\u043b\u0435\u0436\u0435\u043d\u0438\u0435\u0410\u043a\u0442\u0438\u0432\u043d\u043e",             // СлежениеАктивно
    u"\u041f\u043e\u043b\u0443\u0447\u0438\u0442\u044c\u041f\u043e\u0441\u043b\u0435\u0434\u043d\u044e\u044e\u041e\u0448\u0438\u0431\u043a\u0443", // ПолучитьПоследнююОшибку
    u"\u041f\u043e\u043b\u0443\u0447\u0438\u0442\u044c\u041c\u0430\u043a\u0441\u0411\u0430\u0439\u0442\u0421\u043e\u0434\u0435\u0440\u0436\u0438\u043c\u043e\u0433\u043e", // ПолучитьМаксБайтСодержимого
    u"\u0423\u0441\u0442\u0430\u043d\u043e\u0432\u0438\u0442\u044c\u041c\u0430\u043a\u0441\u0411\u0430\u0439\u0442\u0421\u043e\u0434\u0435\u0440\u0436\u0438\u043c\u043e\u0433\u043e", // УстановитьМаксБайтСодержимого
    u"\u041f\u043e\u043b\u0443\u0447\u0438\u0442\u044c\u0412\u043a\u043b\u044e\u0447\u0430\u044f\u041f\u043e\u0434\u043a\u0430\u0442\u0430\u043b\u043e\u0433\u0438", // ПолучитьВключаяПодкаталоги
    u"\u041f\u043e\u043b\u0443\u0447\u0438\u0442\u044c\u0421\u043f\u0438\u0441\u043e\u043a\u041f\u0430\u043f\u043e\u043a" , // ПолучитьСписокПапок
    u"\u0420\u0435\u0432\u0438\u0437\u0438\u044f\u0421\u043b\u0435\u0436\u0435\u043d\u0438\u044f", // РевизияСлежения
    u"ИзмененияСлежения",
    u"ХешSHA256"
};

bool EqualsIgnoreCase(const char16_t* a, const char16_t* b)
{
    if (!a || !b)
        return false;
    while (*a && *b) {
        char16_t ca = *a;
        char16_t cb = *b;
        if (ca >= u'A' && ca <= u'Z')
            ca = static_cast<char16_t>(ca - u'A' + u'a');
        if (cb >= u'A' && cb <= u'Z')
            cb = static_cast<char16_t>(cb - u'A' + u'a');
        if (ca != cb)
            return false;
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

size_t U16Len(const char16_t* s)
{
    size_t n = 0;
    while (s && s[n])
        ++n;
    return n;
}

} // namespace

CFileWatcherAddIn::CFileWatcherAddIn()
{
    dispatcher_.SetEmit([this](const std::string& watchRoot, const std::string& json) {
        EmitExternalEvent(watchRoot, json);
    });
}

CFileWatcherAddIn::~CFileWatcherAddIn()
{
    Done();
}

bool CFileWatcherAddIn::Init(void* disp)
{
    connection_ = static_cast<IAddInDefBase*>(disp);
    return connection_ != nullptr;
}

bool CFileWatcherAddIn::setMemManager(void* mem)
{
    memory_ = static_cast<IMemoryManager*>(mem);
    return memory_ != nullptr;
}

long CFileWatcherAddIn::GetInfo()
{
    return 2000;
}

void CFileWatcherAddIn::Done()
{
    StopAllInternal();
    dispatcher_.Stop();
    connection_ = nullptr;
}

bool CFileWatcherAddIn::AllocWstr(WCHAR_T** dest, const char16_t* src)
{
    if (!memory_ || !dest || !src)
        return false;
    const size_t len = U16Len(src);
    if (!memory_->AllocMemory(reinterpret_cast<void**>(dest), static_cast<unsigned long>((len + 1) * sizeof(WCHAR_T))))
        return false;
    for (size_t i = 0; i < len; ++i)
        (*dest)[i] = static_cast<WCHAR_T>(src[i]);
    (*dest)[len] = 0;
    return true;
}

bool CFileWatcherAddIn::AllocWstrFromUtf8(WCHAR_T** dest, const std::string& utf8)
{
    const std::u16string u16 = fw::Utf8ToUtf16(utf8);
    return AllocWstr(dest, u16.c_str());
}

bool CFileWatcherAddIn::SetVariantBool(tVariant* var, bool value)
{
    if (!var)
        return false;
    tVarInit(var);
    TV_VT(var) = VTYPE_BOOL;
    var->bVal = value;
    return true;
}

bool CFileWatcherAddIn::SetVariantLong(tVariant* var, int32_t value)
{
    if (!var)
        return false;
    tVarInit(var);
    TV_VT(var) = VTYPE_I4;
    var->lVal = value;
    return true;
}

bool CFileWatcherAddIn::SetVariantString(tVariant* var, const std::string& utf8)
{
    if (!var || !memory_)
        return false;
    const std::u16string u16 = fw::Utf8ToUtf16(utf8);
    tVarInit(var);
    WCHAR_T* buf = nullptr;
    if (!memory_->AllocMemory(reinterpret_cast<void**>(&buf),
                              static_cast<unsigned long>((u16.size() + 1) * sizeof(WCHAR_T))))
        return false;
    for (size_t i = 0; i < u16.size(); ++i)
        buf[i] = static_cast<WCHAR_T>(u16[i]);
    buf[u16.size()] = 0;
    TV_VT(var) = VTYPE_PWSTR;
    var->pwstrVal = buf;
    var->wstrLen = static_cast<uint32_t>(u16.size());
    return true;
}

bool CFileWatcherAddIn::SetVariantStringArray(tVariant* var, const std::vector<std::string>& values)
{
    if (!var || !memory_)
        return false;

    tVarInit(var);
    if (values.empty()) {
        TV_VT(var) = static_cast<TYPEVAR>(VTYPE_ARRAY | VTYPE_VARIANT);
        var->pvarVal = nullptr;
        var->cbElements = 0;
        return true;
    }

    tVariant* items = nullptr;
    if (!memory_->AllocMemory(reinterpret_cast<void**>(&items),
                              static_cast<unsigned long>(sizeof(tVariant) * values.size())))
        return false;

    for (size_t i = 0; i < values.size(); ++i) {
        tVarInit(&items[i]);
        if (!SetVariantString(&items[i], values[i])) {
            for (size_t j = 0; j < i; ++j) {
                if (TV_VT(&items[j]) == VTYPE_PWSTR && items[j].pwstrVal)
                    memory_->FreeMemory(reinterpret_cast<void**>(&items[j].pwstrVal));
            }
            memory_->FreeMemory(reinterpret_cast<void**>(&items));
            return false;
        }
    }

    TV_VT(var) = static_cast<TYPEVAR>(VTYPE_ARRAY | VTYPE_VARIANT);
    var->pvarVal = items;
    var->cbElements = static_cast<uint32_t>(values.size());
    return true;
}

std::string CFileWatcherAddIn::VariantToUtf8(tVariant* var) const
{
    if (!var)
        return {};
    if (TV_VT(var) == VTYPE_PWSTR && var->pwstrVal) {
        std::u16string u16;
        u16.resize(var->wstrLen);
        for (uint32_t i = 0; i < var->wstrLen; ++i)
            u16[i] = static_cast<char16_t>(var->pwstrVal[i]);
        return fw::Utf16ToUtf8(u16);
    }
    if (TV_VT(var) == VTYPE_PSTR && var->pstrVal)
        return std::string(var->pstrVal, var->strLen);
    return {};
}

bool CFileWatcherAddIn::VariantToBool(tVariant* var, bool& out) const
{
    if (!var)
        return false;
    if (TV_VT(var) == VTYPE_BOOL) {
        out = var->bVal;
        return true;
    }
    if (TV_VT(var) == VTYPE_I4 || TV_VT(var) == VTYPE_UI4 || TV_VT(var) == VTYPE_INT) {
        out = var->lVal != 0;
        return true;
    }
    return false;
}

bool CFileWatcherAddIn::VariantToLong(tVariant* var, int32_t& out) const
{
    if (!var)
        return false;
    switch (TV_VT(var)) {
    case VTYPE_I4:
    case VTYPE_INT:
        out = var->lVal;
        return true;
    case VTYPE_UI4:
        out = static_cast<int32_t>(var->ulVal);
        return true;
    case VTYPE_R8:
        out = static_cast<int32_t>(var->dblVal);
        return true;
    default:
        return false;
    }
}

long CFileWatcherAddIn::FindName(const WCHAR_T* name, const char16_t* const* names, long count) const
{
    if (!name)
        return -1;
    std::u16string incoming;
    for (const WCHAR_T* p = name; *p; ++p)
        incoming.push_back(static_cast<char16_t>(*p));
    for (long i = 0; i < count; ++i) {
        if (EqualsIgnoreCase(incoming.c_str(), names[i]))
            return i;
    }
    return -1;
}

void CFileWatcherAddIn::EmitExternalEvent(const std::string& watchRoot, const std::string& json)
{
    if (!connection_ || !memory_)
        return;

    WCHAR_T* source = nullptr;
    WCHAR_T* message = nullptr;
    WCHAR_T* data = nullptr;
    if (!AllocWstr(&source, u"FileWatcher"))
        return;
    // Событие = путь папки слежения (как передано в НачатьСлежение)
    if (!AllocWstrFromUtf8(&message, watchRoot.empty() ? std::string("?") : watchRoot)) {
        memory_->FreeMemory(reinterpret_cast<void**>(&source));
        return;
    }
    if (!AllocWstrFromUtf8(&data, json)) {
        memory_->FreeMemory(reinterpret_cast<void**>(&source));
        memory_->FreeMemory(reinterpret_cast<void**>(&message));
        return;
    }

    connection_->ExternalEvent(source, message, data);

    memory_->FreeMemory(reinterpret_cast<void**>(&source));
    memory_->FreeMemory(reinterpret_cast<void**>(&message));
    memory_->FreeMemory(reinterpret_cast<void**>(&data));
}

std::string CFileWatcherAddIn::NormalizePathKey(const std::string& path)
{
    std::string out = path;
    while (!out.empty() && (out.back() == '\\' || out.back() == '/'))
        out.pop_back();
#ifdef _WIN32
    for (char& c : out) {
        if (c == '/')
            c = '\\';
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
#endif
    return out;
}

CFileWatcherAddIn::WatchEntry* CFileWatcherAddIn::FindEntry(const std::string& path)
{
    const std::string key = NormalizePathKey(path);
    auto it = watches_.find(key);
    if (it == watches_.end())
        return nullptr;
    return &it->second;
}

const CFileWatcherAddIn::WatchEntry* CFileWatcherAddIn::FindEntry(const std::string& path) const
{
    const std::string key = NormalizePathKey(path);
    auto it = watches_.find(key);
    if (it == watches_.end())
        return nullptr;
    return &it->second;
}

bool CFileWatcherAddIn::StartWatchInternal(const std::string& path, bool recursive, int32_t maxContentBytes)
{
    if (path.empty())
        return false;

    const std::string key = NormalizePathKey(path);
    const int32_t maxBytes = maxContentBytes < 0 ? kDefaultMaxContentBytes : maxContentBytes;

    std::lock_guard<std::mutex> lock(watchesMutex_);

    // Restart if already present.
    auto existing = watches_.find(key);
    if (existing != watches_.end()) {
        if (existing->second.watcher) {
            existing->second.watcher->Stop();
            existing->second.watcher.reset();
        }
        watches_.erase(existing);
    }

    WatchEntry entry;
    entry.path = path;
    entry.recursive = recursive;
    entry.state = std::make_shared<WatchState>();
    entry.state->maxContentBytes = maxBytes;
    entry.state->earliestRevision = ++watchRevision_;
    entry.state->revision = entry.state->earliestRevision;
    entry.lastError.clear();

    const std::string watchRoot = path;
    entry.watcher = fw::CreateWatcher([this, watchRoot, state = entry.state](const fw::WatchEvent& ev) {
        {
            std::lock_guard<std::mutex> lock(state->changesMutex);
            const uint64_t revision = ++watchRevision_;
            if (ev.action == fw::WatchAction::Rescan)
                state->rescanRevision = revision;
            const bool rescan = ev.action == fw::WatchAction::Rescan;
            state->changes.push_back({revision, ev.path, ev.oldPath, rescan});
            if (state->changes.size() > 8192) {
                state->earliestRevision = state->changes.front().revision;
                state->changes.pop_front();
            }
            state->revision = revision;
        }

        fw::WatchEvent enriched = ev;
        enriched.watchRoot = watchRoot;
        enriched.maxContentBytes = static_cast<size_t>(state->maxContentBytes.load());
        dispatcher_.Enqueue(enriched);
    });

    std::string error;
    if (!entry.watcher->Start(path, recursive, error)) {
        entry.lastError = error.empty() ? "Start failed" : error;
        entry.watcher.reset();
        watches_[key] = std::move(entry);
        return false;
    }

    watches_[key] = std::move(entry);
    return true;
}

bool CFileWatcherAddIn::StopWatchInternal(const std::string& path)
{
    std::lock_guard<std::mutex> lock(watchesMutex_);
    const std::string key = NormalizePathKey(path);
    auto it = watches_.find(key);
    if (it == watches_.end())
        return false;
    if (it->second.watcher) {
        it->second.watcher->Stop();
        it->second.watcher.reset();
    }
    watches_.erase(it);
    return true;
}

void CFileWatcherAddIn::StopAllInternal()
{
    std::lock_guard<std::mutex> lock(watchesMutex_);
    for (auto& kv : watches_) {
        if (kv.second.watcher) {
            kv.second.watcher->Stop();
            kv.second.watcher.reset();
        }
    }
    watches_.clear();
}

std::vector<std::string> CFileWatcherAddIn::CollectWatchPaths() const
{
    std::lock_guard<std::mutex> lock(watchesMutex_);
    std::vector<std::string> paths;
    paths.reserve(watches_.size());
    for (const auto& kv : watches_)
        paths.push_back(kv.second.path);
    return paths;
}

bool CFileWatcherAddIn::RegisterExtensionAs(WCHAR_T** wsExtensionName)
{
    return AllocWstr(wsExtensionName, kExtensionName);
}

long CFileWatcherAddIn::GetNProps()
{
    return 0;
}

long CFileWatcherAddIn::FindProp(const WCHAR_T* /*wsPropName*/)
{
    return -1;
}

const WCHAR_T* CFileWatcherAddIn::GetPropName(long /*lPropNum*/, long /*lPropAlias*/)
{
    return nullptr;
}

bool CFileWatcherAddIn::GetPropVal(const long /*lPropNum*/, tVariant* /*pvarPropVal*/)
{
    return false;
}

bool CFileWatcherAddIn::SetPropVal(const long /*lPropNum*/, tVariant* /*varPropVal*/)
{
    return false;
}

bool CFileWatcherAddIn::IsPropReadable(const long /*lPropNum*/)
{
    return false;
}

bool CFileWatcherAddIn::IsPropWritable(const long /*lPropNum*/)
{
    return false;
}

long CFileWatcherAddIn::GetNMethods()
{
    return eMethLast;
}

long CFileWatcherAddIn::FindMethod(const WCHAR_T* wsMethodName)
{
    long idx = FindName(wsMethodName, kMethodNames, eMethLast);
    if (idx >= 0)
        return idx;
    return FindName(wsMethodName, kMethodNamesRu, eMethLast);
}

const WCHAR_T* CFileWatcherAddIn::GetMethodName(const long lMethodNum, const long lMethodAlias)
{
    if (lMethodNum < 0 || lMethodNum >= eMethLast || !memory_)
        return nullptr;
    // alias 0 = international (EN), 1 = Russian — both registered for FindMethod
    const char16_t* name = (lMethodAlias == 0) ? kMethodNames[lMethodNum] : kMethodNamesRu[lMethodNum];
    WCHAR_T* result = nullptr;
    if (!AllocWstr(&result, name))
        return nullptr;
    return result;
}

long CFileWatcherAddIn::GetNParams(const long lMethodNum)
{
    switch (lMethodNum) {
    case eMethStartWatch:
        return 3;
    case eMethStopWatch:
    case eMethIsWatching:
    case eMethGetLastError:
    case eMethGetMaxContentBytes:
    case eMethGetIncludeSubdirectories:
    case eMethGetWatchRevision:
        return 1;
    case eMethSetMaxContentBytes:
    case eMethSha256:
    case eMethGetWatchChanges:
        return 2;
    case eMethStopAll:
    case eMethVersion:
    case eMethGetWatchList:
        return 0;
    default:
        return 0;
    }
}

bool CFileWatcherAddIn::GetParamDefValue(const long lMethodNum, const long lParamNum, tVariant* pvarParamDefValue)
{
    if (lMethodNum == eMethStartWatch) {
        if (lParamNum == 1)
            return SetVariantBool(pvarParamDefValue, true);
        if (lParamNum == 2)
            return SetVariantLong(pvarParamDefValue, kDefaultMaxContentBytes);
    }
    tVarInit(pvarParamDefValue);
    return false;
}

bool CFileWatcherAddIn::HasRetVal(const long lMethodNum)
{
    switch (lMethodNum) {
    case eMethStopAll:
        return false;
    default:
        return true;
    }
}

bool CFileWatcherAddIn::CallAsProc(const long lMethodNum, tVariant* paParams, const long lSizeArray)
{
    // 1C uses CallAsProc when the return value is discarded:
    //   ВК.ОстановитьСлежение(Путь);  // not  Рез = ВК.ОстановитьСлежение(Путь);
    // Returning false here surfaces as «Метод объекта не обнаружен».
    switch (lMethodNum) {
    case eMethStopAll:
        StopAllInternal();
        return true;
    case eMethStopWatch: {
        if (lSizeArray < 1 || !paParams)
            return false;
        StopWatchInternal(VariantToUtf8(&paParams[0]));
        return true;
    }
    case eMethStartWatch: {
        if (lSizeArray < 1 || !paParams)
            return false;
        const std::string path = VariantToUtf8(&paParams[0]);
        bool recursive = true;
        int32_t maxBytes = kDefaultMaxContentBytes;
        if (lSizeArray >= 2)
            VariantToBool(&paParams[1], recursive);
        if (lSizeArray >= 3)
            VariantToLong(&paParams[2], maxBytes);
        if (path.empty())
            return false;
        StartWatchInternal(path, recursive, maxBytes);
        return true;
    }
    case eMethSetMaxContentBytes: {
        if (lSizeArray < 2 || !paParams)
            return false;
        const std::string path = VariantToUtf8(&paParams[0]);
        int32_t value = 0;
        if (!VariantToLong(&paParams[1], value) || value < 0)
            return false;
        std::lock_guard<std::mutex> lock(watchesMutex_);
        WatchEntry* e = FindEntry(path);
        if (!e)
            return false;
        e->state->maxContentBytes = value;
        e->lastError.clear();
        return true;
    }
    default:
        return false;
    }
}

bool CFileWatcherAddIn::CallAsFunc(const long lMethodNum, tVariant* pvarRetValue, tVariant* paParams, const long lSizeArray)
{
    switch (lMethodNum) {
    case eMethStartWatch: {
        if (lSizeArray < 1 || !paParams)
            return false;
        const std::string path = VariantToUtf8(&paParams[0]);
        bool recursive = true;
        int32_t maxBytes = kDefaultMaxContentBytes;
        if (lSizeArray >= 2)
            VariantToBool(&paParams[1], recursive);
        if (lSizeArray >= 3)
            VariantToLong(&paParams[2], maxBytes);
        if (path.empty()) {
            // Store error under empty key is useless; return false only.
            return SetVariantBool(pvarRetValue, false);
        }
        const bool ok = StartWatchInternal(path, recursive, maxBytes);
        if (!ok && path.empty() == false) {
            // Ensure lastError exists for GetLastError(path)
            std::lock_guard<std::mutex> lock(watchesMutex_);
            if (WatchEntry* e = FindEntry(path)) {
                if (e->lastError.empty())
                    e->lastError = "Failed to start watch";
            }
        }
        return SetVariantBool(pvarRetValue, ok);
    }
    case eMethStopWatch: {
        if (lSizeArray < 1 || !paParams)
            return false;
        const std::string path = VariantToUtf8(&paParams[0]);
        return SetVariantBool(pvarRetValue, StopWatchInternal(path));
    }
    case eMethVersion:
        return SetVariantString(pvarRetValue, "1.3.5");
    case eMethIsWatching: {
        if (lSizeArray < 1 || !paParams)
            return false;
        const std::string path = VariantToUtf8(&paParams[0]);
        std::lock_guard<std::mutex> lock(watchesMutex_);
        const WatchEntry* e = FindEntry(path);
        const bool active = e && e->watcher && e->watcher->IsRunning();
        return SetVariantBool(pvarRetValue, active);
    }
    case eMethGetLastError: {
        if (lSizeArray < 1 || !paParams)
            return false;
        const std::string path = VariantToUtf8(&paParams[0]);
        std::lock_guard<std::mutex> lock(watchesMutex_);
        const WatchEntry* e = FindEntry(path);
        return SetVariantString(pvarRetValue, e ? e->lastError : std::string("Folder is not registered"));
    }
    case eMethGetMaxContentBytes: {
        if (lSizeArray < 1 || !paParams)
            return false;
        const std::string path = VariantToUtf8(&paParams[0]);
        std::lock_guard<std::mutex> lock(watchesMutex_);
        const WatchEntry* e = FindEntry(path);
        return SetVariantLong(pvarRetValue, e ? e->state->maxContentBytes.load() : 0);
    }
    case eMethSetMaxContentBytes: {
        if (lSizeArray < 2 || !paParams)
            return false;
        const std::string path = VariantToUtf8(&paParams[0]);
        int32_t value = 0;
        if (!VariantToLong(&paParams[1], value) || value < 0)
            return SetVariantBool(pvarRetValue, false);
        std::lock_guard<std::mutex> lock(watchesMutex_);
        WatchEntry* e = FindEntry(path);
        if (!e)
            return SetVariantBool(pvarRetValue, false);
        e->state->maxContentBytes = value;
        e->lastError.clear();
        return SetVariantBool(pvarRetValue, true);
    }
    case eMethGetIncludeSubdirectories: {
        if (lSizeArray < 1 || !paParams)
            return false;
        const std::string path = VariantToUtf8(&paParams[0]);
        std::lock_guard<std::mutex> lock(watchesMutex_);
        const WatchEntry* e = FindEntry(path);
        return SetVariantBool(pvarRetValue, e ? e->recursive : false);
    }
    case eMethGetWatchRevision: {
        if (lSizeArray < 1 || !paParams)
            return false;
        const std::string path = VariantToUtf8(&paParams[0]);
        std::lock_guard<std::mutex> lock(watchesMutex_);
        const WatchEntry* entry = FindEntry(path);
        if (!entry || !entry->watcher || !entry->watcher->IsRunning())
            return SetVariantString(pvarRetValue, "");
        return SetVariantString(pvarRetValue, std::to_string(entry->state->revision.load())
            + ":" + std::to_string(entry->state->rescanRevision.load()));
    }
    case eMethSha256: {
        if (lSizeArray < 2 || !paParams)
            return false;
        bool isFile = false;
        if (!VariantToBool(&paParams[1], isFile))
            return false;
#ifdef _WIN32
        return SetVariantString(pvarRetValue, Sha256(VariantToUtf8(&paParams[0]), isFile));
#else
        return SetVariantString(pvarRetValue, "");
#endif
    }
    case eMethGetWatchChanges: {
        if (lSizeArray < 2 || !paParams)
            return false;
        const std::string path = VariantToUtf8(&paParams[0]);
        const std::string prior = VariantToUtf8(&paParams[1]);
        const auto separator = prior.find(':');
        const char* end = prior.data() + (separator == std::string::npos ? prior.size() : separator);
        uint64_t previousRevision = 0;
        const auto parsed = std::from_chars(prior.data(), end, previousRevision);

        std::lock_guard<std::mutex> watchLock(watchesMutex_);
        const WatchEntry* entry = FindEntry(path);
        if (!entry || !entry->watcher || !entry->watcher->IsRunning())
            return SetVariantString(pvarRetValue, "");

        const auto& state = entry->state;
        std::lock_guard<std::mutex> changesLock(state->changesMutex);
        const uint64_t revision = state->revision;
        bool rescan = parsed.ec != std::errc{} || parsed.ptr != end
            || previousRevision < state->earliestRevision || previousRevision > revision;
        std::unordered_set<std::string> paths;
        if (!rescan) {
            for (const auto& change : state->changes) {
                if (change.revision <= previousRevision)
                    continue;
                if (change.rescan) {
                    rescan = true;
                    break;
                }
                if (!change.path.empty())
                    paths.insert(change.path);
                if (!change.oldPath.empty())
                    paths.insert(change.oldPath);
            }
        }

        std::string json = "{\"revision\":\"" + std::to_string(revision) + ":"
            + std::to_string(state->rescanRevision.load()) + "\",\"rescan\":"
            + (rescan ? "true" : "false") + ",\"paths\":[";
        if (!rescan) {
            bool first = true;
            for (const auto& changedPath : paths) {
                if (!first)
                    json += ',';
                first = false;
                json += "\"" + fw::JsonEscape(changedPath) + "\"";
            }
        }
        json += "]}";
        return SetVariantString(pvarRetValue, json);
    }
    case eMethGetWatchList:
        return SetVariantStringArray(pvarRetValue, CollectWatchPaths());
    default:
        return false;
    }
}

void CFileWatcherAddIn::SetLocale(const WCHAR_T* /*loc*/)
{
}

void CFileWatcherAddIn::SetUserInterfaceLanguageCode(const WCHAR_T* /*lang*/)
{
}
