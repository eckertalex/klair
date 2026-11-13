#ifndef BASE_OS_H
#define BASE_OS_H


internal void *os_reserve(u64 size);
internal bool os_commit(void *ptr, u64 size);
internal void os_release(void *ptr, u64 size);

internal u64 os_now_us(void);

#endif // BASE_OS_H
