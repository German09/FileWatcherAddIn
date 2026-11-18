#include "ComponentBase.h"
#include "FileWatcherComponent.h"

#include <atomic>
#include <string>

#ifdef _WIN32
#include <windows.h>

BOOL APIENTRY DllMain(HMODULE /*hModule*/, DWORD /*ul_reason_for_call*/, LPVOID /*lpReserved*/)
{
    return TRUE;
}
#endif

#ifndef ADDIN_EXPORT
#ifdef _WIN32
#define ADDIN_EXPORT
#else
#define ADDIN_EXPORT __attribute__((visibility("default")))
#endif
#endif

namespace {

std::atomic<long> g_objects{0};
AppCapabilities g_capabilities = eAppCapabilitiesLast;

} // namespace

const WCHAR_T* g_classNames =
#ifdef _WIN32
    L"FileWatcher";
#else
    reinterpret_cast<const WCHAR_T*>(u"FileWatcher");
#endif

extern "C" ADDIN_EXPORT long GetClassObject(const WCHAR_T* clsName, IComponentBase** pIntf)
{
    if (!clsName || !pIntf)
        return 0;

    std::u16string name;
    for (const WCHAR_T* p = clsName; *p; ++p)
        name.push_back(static_cast<char16_t>(*p));

    if (name != u"FileWatcher")
        return 0;

    *pIntf = new CFileWatcherAddIn();
    return ++g_objects;
}

extern "C" ADDIN_EXPORT long DestroyObject(IComponentBase** pIntf)
{
    if (!pIntf || !*pIntf)
        return -1;
    delete *pIntf;
    *pIntf = nullptr;
    return --g_objects;
}

extern "C" ADDIN_EXPORT const WCHAR_T* GetClassNames()
{
    return g_classNames;
}

extern "C" ADDIN_EXPORT AppCapabilities SetPlatformCapabilities(const AppCapabilities capabilities)
{
    g_capabilities = capabilities;
    return eAppCapabilitiesLast;
}

extern "C" ADDIN_EXPORT AttachType GetAttachType()
{
    // Allow both normal and isolated attachment (1C 8.3.21+).
    return eCanAttachAny;
}

#if !defined(_WIN32)
extern "C" ADDIN_EXPORT AppCapabilities GetPlatformCapabilities()
{
    return eAppCapabilitiesLast;
}
#endif
