#pragma once

#if (defined __MINGW32__) || (defined __MINGW64__) // detect mingw before windows, because mingw defines _WIN32
#define PLATFORM_MINGW
//#pragma message("PLATFORM_MINGW")
#elif (defined _WIN32_WCE)
#define PLATFORM_POCKETPC
//#pragma message("PLATFORM_POCKETPC")
#elif defined(_XBOX)
#define PLATFORM_XBOX
//#pragma message("PLATFORM_XBOX")
#elif (defined _WIN32)
#define PLATFORM_WIN32
//#pragma message("PLATFORM_WIN32")
#if !defined(NOMINMAX)
#define NOMINMAX
#endif // NOMINMAX
#elif (defined __linux__)
#define PLATFORM_LINUX
//#pragma message("PLATFORM_LINUX")
#elif (defined __APPLE__) && (defined __MACH__)
#define PLATFORM_OSX
//#pragma message("PLATFORM_OSX")
#elif (defined __NetBSD__) || (defined __FreeBSD__) || (defined BSD)
#define PLATFORM_BSD
#elif (defined __QNX__)
#define PLATFORM_QNX
#elif (defined __CYGWIN__)
#define PLATFORM_CYGWIN
#else
#error "Unknown platform!"
#endif
