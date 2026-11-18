#ifndef __COMPONENT_BASE_H__
#define __COMPONENT_BASE_H__

#include "types.h"

class IInitDoneBase
{
public:
    virtual ~IInitDoneBase() {}
    virtual bool ADDIN_API Init(void* disp) = 0;
    virtual bool ADDIN_API setMemManager(void* mem) = 0;
    virtual long ADDIN_API GetInfo() = 0;
    virtual void ADDIN_API Done() = 0;
};

class ILanguageExtenderBase
{
public:
    virtual ~ILanguageExtenderBase() {}
    virtual bool ADDIN_API RegisterExtensionAs(WCHAR_T** wsExtensionName) = 0;
    virtual long ADDIN_API GetNProps() = 0;
    virtual long ADDIN_API FindProp(const WCHAR_T* wsPropName) = 0;
    virtual const WCHAR_T* ADDIN_API GetPropName(long lPropNum, long lPropAlias) = 0;
    virtual bool ADDIN_API GetPropVal(const long lPropNum, tVariant* pvarPropVal) = 0;
    virtual bool ADDIN_API SetPropVal(const long lPropNum, tVariant* varPropVal) = 0;
    virtual bool ADDIN_API IsPropReadable(const long lPropNum) = 0;
    virtual bool ADDIN_API IsPropWritable(const long lPropNum) = 0;
    virtual long ADDIN_API GetNMethods() = 0;
    virtual long ADDIN_API FindMethod(const WCHAR_T* wsMethodName) = 0;
    virtual const WCHAR_T* ADDIN_API GetMethodName(const long lMethodNum, const long lMethodAlias) = 0;
    virtual long ADDIN_API GetNParams(const long lMethodNum) = 0;
    virtual bool ADDIN_API GetParamDefValue(const long lMethodNum, const long lParamNum, tVariant* pvarParamDefValue) = 0;
    virtual bool ADDIN_API HasRetVal(const long lMethodNum) = 0;
    virtual bool ADDIN_API CallAsProc(const long lMethodNum, tVariant* paParams, const long lSizeArray) = 0;
    virtual bool ADDIN_API CallAsFunc(const long lMethodNum, tVariant* pvarRetValue, tVariant* paParams, const long lSizeArray) = 0;
};

class LocaleBase
{
public:
    virtual ~LocaleBase() {}
    virtual void ADDIN_API SetLocale(const WCHAR_T* loc) = 0;
};

class UserLanguageBase
{
public:
    virtual ~UserLanguageBase() {}
    virtual void ADDIN_API SetUserInterfaceLanguageCode(const WCHAR_T* lang) = 0;
};

class IComponentBase :
    public IInitDoneBase,
    public ILanguageExtenderBase,
    public LocaleBase,
    public UserLanguageBase
{
public:
    virtual ~IComponentBase() {}
};

enum AppCapabilities
{
    eAppCapabilitiesInvalid = -1,
    eAppCapabilities1 = 1,
    eAppCapabilities2 = 2,
    eAppCapabilities3 = 3,
    eAppCapabilitiesLast = eAppCapabilities3
};

/// How the add-in may be attached (required on recent 1C platforms).
enum AttachType
{
    eCanAttachNotIsolated = 1,
    eCanAttachIsolated = 2,
    eCanAttachAny = 3
};

extern "C" long GetClassObject(const WCHAR_T*, IComponentBase** pIntf);
extern "C" long DestroyObject(IComponentBase** pIntf);
extern "C" const WCHAR_T* GetClassNames();
extern "C" AppCapabilities SetPlatformCapabilities(const AppCapabilities capabilities);
extern "C" AttachType GetAttachType();

#endif
