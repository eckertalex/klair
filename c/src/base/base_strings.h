#ifndef BASE_STRINGS_H
#define BASE_STRINGS_H

typedef struct String8 String8;
struct String8 {
    u8 *str;
    u64 size;
};

typedef struct String8Node String8Node;
struct String8Node {
    String8Node *next;
    String8 string;
};

typedef struct String8List String8List;
struct String8List {
    String8Node *first;
    String8Node *last;
    u64 node_count;
    u64 total_size;
};

#define str8_lit(S) str8((u8 *)(S), sizeof(S) - 1)
#define str8_lit_comp(S) {(u8 *)(S), sizeof(S) - 1}
#define str8_varg(S) (int)((S).size), ((S).str)

internal String8 str8(u8 *str, u64 size);
internal String8 str8_zero(void);
internal String8 str8_cstring(char const *cstring);

internal bool char_is_space(u8 c);
internal bool char_is_digit(u8 c);
internal u8 char_to_lower(u8 c);

internal bool str8_match(String8 a, String8 b);
internal bool str8_match_case_insensitive(String8 a, String8 b);

internal String8 str8_prefix(String8 string, u64 size);
internal String8 str8_skip(String8 string, u64 amount);
internal String8 str8_substr(String8 string, u64 first, u64 opl);
internal String8 str8_skip_chop_whitespace(String8 string);

internal u64 str8_find_needle(String8 string, u64 start, String8 needle);
internal u64 str8_find_last_char(String8 string, u8 c);
internal bool str8_contains_char(String8 string, u8 c);

internal bool str8_is_integer(String8 string);
internal bool try_i64_from_str8(String8 string, i64 *out);

internal String8 push_str8_copy(Arena *arena, String8 string);
internal String8 push_str8fv(Arena *arena, char const *fmt, va_list args);
[[gnu::format(printf, 2, 3)]]
internal String8 push_str8f(Arena *arena, char const *fmt, ...);

internal void str8_list_push(Arena *arena, String8List *list, String8 string);
[[gnu::format(printf, 3, 4)]]
internal void str8_list_pushf(Arena *arena, String8List *list, char const *fmt, ...);
internal String8 str8_list_join(Arena *arena, String8List *list, String8 separator);

#endif // BASE_STRINGS_H
