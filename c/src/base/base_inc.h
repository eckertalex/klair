#ifndef BASE_INC_H
#define BASE_INC_H

#include "base_context_cracking.h"

#include "base_core.h"
#include "base_os.h"
#include "base_arena.h"
#include "base_strings.h"
#include "base_files.h"

#if OS_MAC
# include "mac/base/mac_base.h"
#elif OS_LINUX
# include "linux/base/linux_base.h"
#elif OS_WINDOWS
# include "win32/base/win32_base.h"
#else
# error base: no backend for this OS yet
#endif

#endif // BASE_INC_H
