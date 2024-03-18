#pragma once

#include "platform.h"

#if (defined PLATFORM_WIN32) || (defined PLATFORM_POCKETPC)
#ifdef __cplusplus
#define LANES_API extern "C" __declspec(dllexport)
#else
#define LANES_API extern __declspec(dllexport)
#endif // __cplusplus
#else
#ifdef __cplusplus
#define LANES_API extern "C"
#else
#define LANES_API extern
#endif // __cplusplus
#endif // (defined PLATFORM_WIN32) || (defined PLATFORM_POCKETPC)
