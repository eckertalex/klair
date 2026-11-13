internal Arena *arena_alloc(u64 reserve_size) {
    u64 reserved = AlignPow2(reserve_size, ARENA_COMMIT_SIZE);
    void *base = os_reserve(reserved);
    AssertAlways(base != nullptr);
    AssertAlways(os_commit(base, ARENA_COMMIT_SIZE));

    Arena *arena = (Arena *)base;
    arena->reserved = reserved;
    arena->committed = ARENA_COMMIT_SIZE;
    arena->pos = ARENA_HEADER_SIZE;
    return arena;
}

internal void arena_release(Arena *arena) {
    os_release(arena, arena->reserved);
}

internal void *arena_push(Arena *arena, u64 size, u64 align, bool zero) {
    u64 start = AlignPow2(arena->pos, align);
    u64 end = start + size;
    AssertAlways(end <= arena->reserved);

    if(end > arena->committed) {
        u64 commit_end = Min(AlignPow2(end, ARENA_COMMIT_SIZE), arena->reserved);
        AssertAlways(os_commit((u8 *)arena + arena->committed, commit_end - arena->committed));
        arena->committed = commit_end;
    }

    arena->pos = end;
    u8 *result = (u8 *)arena + start;
    if(zero) {
        MemoryZero(result, size);
    }
    return result;
}

internal u64 arena_pos(Arena *arena) {
    return arena->pos;
}

internal void arena_pop_to(Arena *arena, u64 pos) {
    Assert(pos <= arena->pos);
    arena->pos = Max(ARENA_HEADER_SIZE, pos);
}

internal void arena_clear(Arena *arena) {
    arena_pop_to(arena, ARENA_HEADER_SIZE);
}

internal Temp temp_begin(Arena *arena) {
    Temp temp = {arena, arena->pos};
    return temp;
}

internal void temp_end(Temp temp) {
    arena_pop_to(temp.arena, temp.pos);
}

thread_static Arena *scratch_arenas[2];

internal Temp scratch_begin(Arena **conflicts, u64 conflict_count) {
    for(u64 i = 0; i < ArrayCount(scratch_arenas); i += 1) {
        if(scratch_arenas[i] == nullptr) {
            scratch_arenas[i] = arena_alloc(GB(1));
        }

        bool conflicted = false;
        for(u64 j = 0; j < conflict_count; j += 1) {
            if(conflicts[j] == scratch_arenas[i]) {
                conflicted = true;
            }
        }
        if(!conflicted) {
            return temp_begin(scratch_arenas[i]);
        }
    }

    AssertAlways(false);
    return (Temp){0};
}
