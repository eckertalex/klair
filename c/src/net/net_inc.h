#ifndef NET_INC_H
#define NET_INC_H

#include "net.h"

#if OS_MAC
# include "mac/net/mac_net.h"
#elif OS_LINUX
# include "linux/net/linux_net.h"
#elif OS_WINDOWS
# include "win32/net/win32_net.h"
#else
# error net: no backend for this OS yet. Linux: io_uring. Windows: IOCP.
#endif

#endif // NET_INC_H
