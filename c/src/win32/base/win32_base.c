internal void *os_reserve(u64 size) {
    return VirtualAlloc(nullptr, (SIZE_T)size, MEM_RESERVE, PAGE_NOACCESS);
}

internal bool os_commit(void *ptr, u64 size) {
    return VirtualAlloc(ptr, (SIZE_T)size, MEM_COMMIT, PAGE_READWRITE) == ptr;
}

internal void os_release(void *ptr, u64 size) {
    (void)size;
    VirtualFree(ptr, 0, MEM_RELEASE);
}

internal u64 os_now_us(void) {
    LARGE_INTEGER count = {0};
    LARGE_INTEGER frequency = {0};
    AssertAlways(QueryPerformanceCounter(&count));
    AssertAlways(QueryPerformanceFrequency(&frequency));
    u64 ticks = (u64)count.QuadPart;
    u64 hz = (u64)frequency.QuadPart;
    return (ticks / hz) * Million(1) + ((ticks % hz) * Million(1)) / hz;
}
