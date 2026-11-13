#include "base_arena.c"
#include "base_strings.c"
#include "base_files.c"

#if OS_MAC
# include "mac/base/mac_base.c"
#elif OS_LINUX
# include "linux/base/linux_base.c"
#elif OS_WINDOWS
# include "win32/base/win32_base.c"
#endif
