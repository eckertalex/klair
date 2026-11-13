#ifndef BASE_ARENA_H
#define BASE_ARENA_H

typedef struct Arena Arena;
struct Arena {
    u64 reserved;
    u64 committed;
    u64 pos;
};

#define ARENA_HEADER_SIZE AlignPow2((u64)sizeof(Arena), (u64)16)
#define ARENA_COMMIT_SIZE KB(64)

typedef struct Temp Temp;
struct Temp {
    Arena *arena;
    u64 pos;
};

internal Arena *arena_alloc(u64 reserve_size);
internal void arena_release(Arena *arena);
internal void *arena_push(Arena *arena, u64 size, u64 align, bool zero);
internal u64 arena_pos(Arena *arena);
internal void arena_pop_to(Arena *arena, u64 pos);
internal void arena_clear(Arena *arena);

#define push_array_no_zero(arena, T, count) \
    (T *)arena_push((arena), sizeof(T) * (u64)(count), alignof(T), false)
#define push_array(arena, T, count) \
    (T *)arena_push((arena), sizeof(T) * (u64)(count), alignof(T), true)

internal Temp temp_begin(Arena *arena);
internal void temp_end(Temp temp);

// Pass result arenas as conflicts to keep scratch memory separate.
internal Temp scratch_begin(Arena **conflicts, u64 conflict_count);
#define scratch_end(temp) temp_end(temp)

#endif // BASE_ARENA_H
