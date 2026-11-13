internal void *os_reserve(u64 size) {
    void *result = mmap(nullptr, size, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    return result == MAP_FAILED ? nullptr : result;
}

internal bool os_commit(void *ptr, u64 size) {
    return mprotect(ptr, size, PROT_READ | PROT_WRITE) == 0;
}

internal void os_release(void *ptr, u64 size) {
    munmap(ptr, size);
}

internal u64 os_now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (u64)ts.tv_sec * Million(1) + (u64)ts.tv_nsec / Thousand(1);
}
