#ifndef __CON_TYPES_H__
#define __CON_TYPES_H__

#if defined(_WINDOWS) || defined(WINAPI_FAMILY) || defined(_WIN32)
#include <windows.h>
#endif

#if defined(WINAPI_FAMILY)
#include <wtypes.h>
#endif

#if __GNUC__ >= 3
#pragma GCC system_header
#endif

#include "com.h"
#include <time.h>
#include <string.h>
#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#define EXTERN_C extern "C"

#ifdef __GNUC__
#define _ANONYMOUS_UNION __extension__
#define _ANONYMOUS_STRUCT __extension__
#else
#define _ANONYMOUS_UNION
#define _ANONYMOUS_STRUCT
#endif

#ifdef NONAMELESSUNION
#define __VARIANT_NAME_1 u
#define __VARIANT_NAME_2 iface
#define __VARIANT_NAME_3 str
#define __VARIANT_NAME_4 wstr
#else
#define __VARIANT_NAME_1
#define __VARIANT_NAME_2
#define __VARIANT_NAME_3
#define __VARIANT_NAME_4
#endif

#ifndef ADDIN_API
#ifdef _WIN32
#define ADDIN_API __stdcall
#else
#define ADDIN_API
#endif
#endif

#ifdef _WIN32
#define WCHAR_T wchar_t
#else
#define WCHAR_T uint16_t
#endif

typedef unsigned short TYPEVAR;
enum ENUMVAR
{
    VTYPE_EMPTY = 0,
    VTYPE_NULL,
    VTYPE_I2,
    VTYPE_I4,
    VTYPE_R4,
    VTYPE_R8,
    VTYPE_DATE,
    VTYPE_TM,
    VTYPE_PSTR,
    VTYPE_INTERFACE,
    VTYPE_ERROR,
    VTYPE_BOOL,
    VTYPE_VARIANT,
    VTYPE_I1,
    VTYPE_UI1,
    VTYPE_UI2,
    VTYPE_UI4,
    VTYPE_I8,
    VTYPE_UI8,
    VTYPE_INT,
    VTYPE_UINT,
    VTYPE_HRESULT,
    VTYPE_PWSTR,
    VTYPE_BLOB,
    VTYPE_CLSID,
    VTYPE_STR_BLOB = 0xfff,
    VTYPE_VECTOR = 0x1000,
    VTYPE_ARRAY = 0x2000,
    VTYPE_BYREF = 0x4000,
    VTYPE_RESERVED = 0x8000,
    VTYPE_ILLEGAL = 0xffff,
    VTYPE_ILLEGALMASKED = 0xfff,
    VTYPE_TYPEMASK = 0xfff
};

#if defined(__GNUC__) && !defined(NONAMELESSUNION)
__extension__
#endif
struct _tVariant
{
    _ANONYMOUS_UNION union
    {
        int8_t i8Val;
        int16_t shortVal;
        int32_t lVal;
        int intVal;
        unsigned int uintVal;
        int64_t llVal;
        uint8_t ui8Val;
        uint16_t ushortVal;
        uint32_t ulVal;
        uint64_t ullVal;
        int32_t errCode;
        long hRes;
        float fltVal;
        double dblVal;
        bool bVal;
        char chVal;
        wchar_t wchVal;
        DATE date;
        IID IDVal;
        struct _tVariant* pvarVal;
        struct tm tmVal;
        _ANONYMOUS_STRUCT struct
        {
            void* pInterfaceVal;
            IID InterfaceID;
        } __VARIANT_NAME_2;
        _ANONYMOUS_STRUCT struct
        {
            char* pstrVal;
            uint32_t strLen;
        } __VARIANT_NAME_3;
        _ANONYMOUS_STRUCT struct
        {
            WCHAR_T* pwstrVal;
            uint32_t wstrLen;
        } __VARIANT_NAME_4;
    } __VARIANT_NAME_1;
    uint32_t cbElements;
    TYPEVAR vt;
};
typedef struct _tVariant tVariant;

#if defined(NONAMELESSUNION)
#define TV_JOIN(X, Y) ((X)->u.Y)
#else
#define TV_JOIN(X, Y) ((X)->Y)
#endif

#define TV_VT(X) ((X)->vt)

inline void tVarInit(tVariant* tvar)
{
    assert(tvar != NULL);
    memset(tvar, 0, sizeof(tVariant));
    TV_VT(tvar) = VTYPE_EMPTY;
}

#endif
