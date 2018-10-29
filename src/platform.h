#ifndef __LANES_PLATFORM_H__
#define __LANES_PLATFORM_H__ 1

#ifdef _WIN32_WCE
  #define PLATFORM_POCKETPC
#elif defined(_XBOX)
  #define PLATFORM_XBOX
#elif (defined _WIN32)
  #define PLATFORM_WIN32
#elif (defined __linux__)
  #define PLATFORM_LINUX
#elif (defined __APPLE__) && (defined __MACH__)
  #define PLATFORM_OSX
#elif (defined __NetBSD__) || (defined __FreeBSD__) || (defined BSD)
  #define PLATFORM_BSD
#elif (defined __QNX__)
  #define PLATFORM_QNX
#elif (defined __CYGWIN__)
  #define PLATFORM_CYGWIN
#else
  #error "Unknown platform!"
#endif

#endif // __LANES_PLATFORM_H__
