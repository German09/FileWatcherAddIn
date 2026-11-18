#ifndef __ADAPTER_DEF_H__
#define __ADAPTER_DEF_H__

#include "types.h"

struct IInterface
{
};

enum Interfaces
{
    eIMsgBox = 0,
    eIPlatformInfo
};

class IAddInDefBase
{
public:
    virtual ~IAddInDefBase() {}
    virtual bool ADDIN_API AddError(unsigned short wcode, const WCHAR_T* source,
                                    const WCHAR_T* descr, long scode) = 0;
    virtual bool ADDIN_API Read(WCHAR_T* wszPropName, tVariant* pVal,
                                long* pErrCode, WCHAR_T** errDescriptor) = 0;
    virtual bool ADDIN_API Write(WCHAR_T* wszPropName, tVariant* pVar) = 0;
    virtual bool ADDIN_API RegisterProfileAs(WCHAR_T* wszProfileName) = 0;
    virtual bool ADDIN_API SetEventBufferDepth(long lDepth) = 0;
    virtual long ADDIN_API GetEventBufferDepth() = 0;
    virtual bool ADDIN_API ExternalEvent(WCHAR_T* wszSource,
                                         WCHAR_T* wszMessage,
                                         WCHAR_T* wszData) = 0;
    virtual void ADDIN_API CleanEventBuffer() = 0;
    virtual bool ADDIN_API SetStatusLine(WCHAR_T* wszStatusLine) = 0;
    virtual void ADDIN_API ResetStatusLine() = 0;
};

class IAddInDefBaseEx : public IAddInDefBase
{
public:
    virtual ~IAddInDefBaseEx() {}
    virtual IInterface* ADDIN_API GetInterface(Interfaces iface) = 0;
};

struct IMsgBox : public IInterface
{
    virtual bool ADDIN_API Confirm(const WCHAR_T* queryText, tVariant* retVal) = 0;
    virtual bool ADDIN_API Alert(const WCHAR_T* text) = 0;
};

struct IPlatformInfo : public IInterface
{
    enum AppType
    {
        eAppUnknown = -1,
        eAppThinClient = 0,
        eAppThickClient,
        eAppWebClient,
        eAppServer,
        eAppExtConn,
        eAppMobileClient,
        eAppMobileServer
    };

    struct AppInfo
    {
        const WCHAR_T* AppVersion;
        const WCHAR_T* UserAgentInformation;
        AppType Application;
    };

    virtual const AppInfo* ADDIN_API GetPlatformInfo() = 0;
};

#endif
