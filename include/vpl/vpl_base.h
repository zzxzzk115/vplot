/*
 * vpl_base.h - linkage, calling convention and C/C++ guards.
 */

#ifndef VPL_BASE_H
#define VPL_BASE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
#    define VPL_EXTERN_C_BEGIN extern "C" {
#    define VPL_EXTERN_C_END }
#else
#    define VPL_EXTERN_C_BEGIN
#    define VPL_EXTERN_C_END
#endif

/* vplot builds as a static library by default; define VPL_SHARED when building
   or consuming a DLL, and VPL_EXPORTS while building it. */
#if defined(_WIN32)
#    if defined(VPL_SHARED)
#        if defined(VPL_EXPORTS)
#            define VPL_API __declspec(dllexport)
#        else
#            define VPL_API __declspec(dllimport)
#        endif
#    else
#        define VPL_API
#    endif
#    define VPL_CALL __cdecl
#else
#    if defined(VPL_SHARED) && defined(VPL_EXPORTS)
#        define VPL_API __attribute__((visibility("default")))
#    else
#        define VPL_API
#    endif
#    define VPL_CALL
#endif

#endif /* VPL_BASE_H */
