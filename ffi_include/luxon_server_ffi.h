// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <stdint.h>
#include <stddef.h>

/* ============================================================================
 * EXPORT VISIBILITY & LINKAGE MACROS
 * ============================================================================ */

#if defined(FFI_WASM) || defined(__wasm__)
#if defined(__GNUC__) || defined(__clang__)
#define FFI_EXPORT(name) __attribute__((export_name(name))) __attribute__((visibility("default"))) __attribute__((used))
#else
#define FFI_EXPORT(name)
#endif
#elif defined(_WIN32)
#if defined(FFI_BUILD_IMPL)
#define FFI_EXPORT(name) __declspec(dllexport)
#else
#define FFI_EXPORT(name) __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define FFI_EXPORT(name) __attribute__((visibility("default")))
#else
#define FFI_EXPORT(name)
#endif

/* ============================================================================
 * TYPE MAPPINGS TABLE
 * ============================================================================ */

#if defined(FFI_WASM) || defined(__wasm__)
typedef uint32_t ffi_size_t;
typedef int32_t ffi_ssize_t;
typedef uint32_t ffi_uintptr_t;
typedef int32_t ffi_intptr_t;
typedef int32_t ffi_ptrdiff_t;
#else
typedef size_t ffi_size_t;
typedef ptrdiff_t ffi_ssize_t;
typedef uintptr_t ffi_uintptr_t;
typedef intptr_t ffi_intptr_t;
typedef ptrdiff_t ffi_ptrdiff_t;
#endif

#ifndef __cplusplus
#ifdef bool
#undef bool
#endif
#define bool uint8_t
#endif

/* ============================================================================
 * ENUMERATIONS
 * ============================================================================ */

typedef enum {
    LUXON_SERVER_TYPE_NONE = 0,
    LUXON_SERVER_TYPE_NAMESERVER = 1,
    LUXON_SERVER_TYPE_MASTERSERVER = 2,
    LUXON_SERVER_TYPE_GAMESERVER = 3
} LuxonServerType;

typedef enum { LUXON_PROTOCOL_UDP = 0, LUXON_PROTOCOL_TCP = 1, LUXON_PROTOCOL_WEBSOCKET = 2 } LuxonServerProtocol;

typedef enum { LUXON_DELIVERY_UNRELIABLE = 0, LUXON_DELIVERY_RELIABLE = 1, LUXON_DELIVERY_UNSEQUENCED = 2 } LuxonDeliveryMode;

typedef enum { LUXON_RECEIVERS_ALL = 0, LUXON_RECEIVERS_GROUP = 1, LUXON_RECEIVERS_ACTORS = 2 } LuxonEventReceiversType;

typedef enum {
    LUXON_PLUGIN_RESULT_CONTINUE = 0,
    LUXON_PLUGIN_RESULT_FAIL = 1,
    LUXON_PLUGIN_RESULT_CANCEL = 2
} LuxonGamePluginResult;

typedef enum {
    LUXON_LOG_LEVEL_TRACE = 0,
    LUXON_LOG_LEVEL_DEBUG = 1,
    LUXON_LOG_LEVEL_INFO = 2,
    LUXON_LOG_LEVEL_WARN = 3,
    LUXON_LOG_LEVEL_ERROR = 4,
    LUXON_LOG_LEVEL_CRITICAL = 5,
    LUXON_LOG_LEVEL_OFF = 6
} LuxonLogLevel;

#ifdef __cplusplus
extern "C" {
#endif

#include "luxon_server_ffi_exports.inc"

#if defined(FFI_WASM) || defined(__wasm__)
#include "luxon_server_ffi_imports.inc"
#else
typedef struct {
#include "luxon_server_ffi_imports.inc"
} LuxonServerImports;

/**
 * @brief Bootstraps the function pointer table for dynamic library builds
 * * Required for non-WASM executions so the core server can route function calls
 * into the loaded plugin's implementations dynamically
 * * @param imports A pointer to the populated LuxonServerImports structure mapping logic bindings
 */
FFI_EXPORT(luxonSetServerImports) void luxonSetServerImports(const LuxonServerImports *imports);
#endif

#ifdef __cplusplus
}
#endif
