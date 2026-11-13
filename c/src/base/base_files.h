#ifndef BASE_FILES_H
#define BASE_FILES_H


internal bool data_from_file_path(Arena *arena, String8 path, String8 *out);

internal String8 str8_from_env(Arena *arena, char const *name);

internal void write_str8(FILE *file, String8 string);

#endif // BASE_FILES_H
