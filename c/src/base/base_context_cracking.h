#ifndef BASE_CONTEXT_CRACKING_H
#define BASE_CONTEXT_CRACKING_H

/////////////////////////////
// Compiler/OS/Arch Cracking

#if defined(__clang__)
# define COMPILER_CLANG 1
#elif defined(_MSC_VER)
# error MSVC is not supported, it lacks the C23 features this codebase uses. Use clang.
#elif defined(__GNUC__)
# define COMPILER_GCC 1
#else
# error Unsupported compiler
#endif

#if defined(_WIN32)
# define OS_WINDOWS 1
#elif defined(__APPLE__)
# define OS_MAC 1
#elif defined(__linux__)
# define OS_LINUX 1
#else
# error Unsupported OS
#endif

#if defined(__x86_64__) || defined(_M_X64)
# define ARCH_X64 1
#elif defined(__aarch64__) || defined(_M_ARM64)
# define ARCH_ARM64 1
#else
# error Unsupported architecture
#endif

/////////////////////////
// Build Option Cracking

#if !defined(BUILD_DEBUG)
# define BUILD_DEBUG 1
#endif

#if !defined(BUILD_NAME)
# define BUILD_NAME "Untitled"
#endif

#if !defined(BUILD_VERSION)
# define BUILD_VERSION "0.4.0"
#endif

#if defined(BUILD_GIT_HASH)
# define BUILD_GIT_HASH_APPEND "+" BUILD_GIT_HASH
#else
# define BUILD_GIT_HASH_APPEND ""
#endif

#if BUILD_DEBUG
# define BUILD_DEBUG_APPEND ", debug"
#else
# define BUILD_DEBUG_APPEND ""
#endif

#define BUILD_LANG "c"

#define BUILD_VERSION_FULL \
    BUILD_NAME " version " BUILD_VERSION BUILD_GIT_HASH_APPEND " (" BUILD_LANG BUILD_DEBUG_APPEND ")"

//////////////////////////////
// Zero All Undefined Options

#if !defined(COMPILER_CLANG)
# define COMPILER_CLANG 0
#endif
#if !defined(COMPILER_GCC)
# define COMPILER_GCC 0
#endif
#if !defined(OS_WINDOWS)
# define OS_WINDOWS 0
#endif
#if !defined(OS_MAC)
# define OS_MAC 0
#endif
#if !defined(OS_LINUX)
# define OS_LINUX 0
#endif
#if !defined(ARCH_X64)
# define ARCH_X64 0
#endif
#if !defined(ARCH_ARM64)
# define ARCH_ARM64 0
#endif

#endif // BASE_CONTEXT_CRACKING_H
