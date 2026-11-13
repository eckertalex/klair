internal bool data_from_file_path(Arena *arena, String8 path, String8 *out) {
    Temp scratch = scratch_begin(&arena, 1);
    String8 path_cstring = push_str8_copy(scratch.arena, path);
    FILE *file = fopen((char *)path_cstring.str, "rb");
    scratch_end(scratch);
    if(!file) {
        return false;
    }

    u64 start = arena_pos(arena);
    u8 *data = arena_push(arena, 0, 1, false);
    u64 size = 0;
    for(;;) {
        u8 *chunk = arena_push(arena, KB(64), 1, false);
        u64 read = (u64)fread(chunk, 1, KB(64), file);
        size += read;
        arena_pop_to(arena, start + size);
        if(read < KB(64)) {
            break;
        }
    }

    // A directory opens fine on macOS, reading it is what fails
    bool ok = !ferror(file);
    fclose(file);
    if(!ok) {
        arena_pop_to(arena, start);
        return false;
    }

    *out = str8(data, size);
    return true;
}

internal String8 str8_from_env(Arena *arena, char const *name) {
    char const *value = getenv(name);
    if(!value) {
        return str8_zero();
    }
    return push_str8_copy(arena, str8_cstring(value));
}

internal void write_str8(FILE *file, String8 string) {
    fwrite(string.str, 1, string.size, file);
}
