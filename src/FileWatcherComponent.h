#pragma once

#include "ComponentBase.h"
#include "AddInDefBase.h"
#include "IMemoryManager.h"

#include "watch/EventQueue.h"
#include "watch/Watcher.h"

#include <atomic>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class CFileWatcherAddIn : public IComponentBase
{
public:
    CFileWatcherAddIn();
    virtual ~CFileWatcherAddIn();

    bool ADDIN_API Init(void* disp) override;
    bool ADDIN_API setMemManager(void* mem) override;
    long ADDIN_API GetInfo() override;
    void ADDIN_API Done() override;

    bool ADDIN_API RegisterExtensionAs(WCHAR_T** wsExtensionName) override;
    long ADDIN_API GetNProps() override;
    long ADDIN_API FindProp(const WCHAR_T* wsPropName) override;
    const WCHAR_T* ADDIN_API GetPropName(long lPropNum, long lPropAlias) override;
    bool ADDIN_API GetPropVal(const long lPropNum, tVariant* pvarPropVal) override;
    bool ADDIN_API SetPropVal(const long lPropNum, tVariant* varPropVal) override;
    bool ADDIN_API IsPropReadable(const long lPropNum) override;
    bool ADDIN_API IsPropWritable(const long lPropNum) override;

    long ADDIN_API GetNMethods() override;
    long ADDIN_API FindMethod(const WCHAR_T* wsMethodName) override;
    const WCHAR_T* ADDIN_API GetMethodName(const long lMethodNum, const long lMethodAlias) override;
    long ADDIN_API GetNParams(const long lMethodNum) override;
    bool ADDIN_API GetParamDefValue(const long lMethodNum, const long lParamNum, tVariant* pvarParamDefValue) override;
    bool ADDIN_API HasRetVal(const long lMethodNum) override;
    bool ADDIN_API CallAsProc(const long lMethodNum, tVariant* paParams, const long lSizeArray) override;
    bool ADDIN_API CallAsFunc(const long lMethodNum, tVariant* pvarRetValue, tVariant* paParams, const long lSizeArray) override;

    void ADDIN_API SetLocale(const WCHAR_T* loc) override;
    void ADDIN_API SetUserInterfaceLanguageCode(const WCHAR_T* lang) override;

private:
    enum Methods {
        eMethStartWatch = 0,
        eMethStopWatch,
        eMethStopAll,
        eMethVersion,
        eMethIsWatching,
        eMethGetLastError,
        eMethGetMaxContentBytes,
        eMethSetMaxContentBytes,
        eMethGetIncludeSubdirectories,
        eMethGetWatchList,
        eMethGetWatchRevision,
        eMethGetWatchChanges,
        eMethSha256,
        eMethLast
    };

    struct WatchChange {
        uint64_t revision;
        std::string path;
        std::string oldPath;
        bool rescan;
    };

    struct WatchState {
        std::atomic<int32_t> maxContentBytes{1024 * 1024};
        std::atomic<uint64_t> revision{0};
        std::atomic<uint64_t> rescanRevision{0};
        std::mutex changesMutex;
        uint64_t earliestRevision = 0;
        std::deque<WatchChange> changes;
    };

    struct WatchEntry {
        std::string path;
        bool recursive = true;
        std::shared_ptr<WatchState> state;
        std::string lastError;
        std::unique_ptr<fw::IWatcher> watcher;
    };

    bool AllocWstr(WCHAR_T** dest, const char16_t* src);
    bool AllocWstrFromUtf8(WCHAR_T** dest, const std::string& utf8);
    bool SetVariantBool(tVariant* var, bool value);
    bool SetVariantLong(tVariant* var, int32_t value);
    bool SetVariantString(tVariant* var, const std::string& utf8);
    bool SetVariantStringArray(tVariant* var, const std::vector<std::string>& values);
    std::string VariantToUtf8(tVariant* var) const;
    bool VariantToBool(tVariant* var, bool& out) const;
    bool VariantToLong(tVariant* var, int32_t& out) const;
    long FindName(const WCHAR_T* name, const char16_t* const* names, long count) const;
    void EmitExternalEvent(const std::string& watchRoot, const std::string& json);

    static std::string NormalizePathKey(const std::string& path);
    WatchEntry* FindEntry(const std::string& path);
    const WatchEntry* FindEntry(const std::string& path) const;
    bool StartWatchInternal(const std::string& path, bool recursive, int32_t maxContentBytes);
    bool StopWatchInternal(const std::string& path);
    void StopAllInternal();
    std::vector<std::string> CollectWatchPaths() const;

    IAddInDefBase* connection_ = nullptr;
    IMemoryManager* memory_ = nullptr;

    mutable std::mutex watchesMutex_;
    std::map<std::string, WatchEntry> watches_; // key = NormalizePathKey

    std::atomic<uint64_t> watchRevision_{0};
    fw::EventDispatcher dispatcher_;
};
