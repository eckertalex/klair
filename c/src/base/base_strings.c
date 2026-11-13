internal String8 str8(u8 *str, u64 size) {
    String8 result = {str, size};
    return result;
}

internal String8 str8_zero(void) {
    String8 result = {0};
    return result;
}

internal String8 str8_cstring(char const *cstring) {
    return str8((u8 *)cstring, (u64)strlen(cstring));
}

internal bool char_is_space(u8 c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

internal bool char_is_digit(u8 c) {
    return c >= '0' && c <= '9';
}

internal u8 char_to_lower(u8 c) {
    return (c >= 'A' && c <= 'Z') ? (u8)(c + ('a' - 'A')) : c;
}

internal bool str8_match(String8 a, String8 b) {
    return a.size == b.size && (a.size == 0 || MemoryMatch(a.str, b.str, a.size));
}

internal bool str8_match_case_insensitive(String8 a, String8 b) {
    if(a.size != b.size) {
        return false;
    }
    for(u64 i = 0; i < a.size; i += 1) {
        if(char_to_lower(a.str[i]) != char_to_lower(b.str[i])) {
            return false;
        }
    }
    return true;
}

internal String8 str8_prefix(String8 string, u64 size) {
    return str8(string.str, Min(size, string.size));
}

internal String8 str8_skip(String8 string, u64 amount) {
    amount = Min(amount, string.size);
    return str8(string.str + amount, string.size - amount);
}

internal String8 str8_substr(String8 string, u64 first, u64 opl) {
    opl = Min(opl, string.size);
    first = Min(first, opl);
    return str8(string.str + first, opl - first);
}

internal String8 str8_skip_chop_whitespace(String8 string) {
    u64 first = 0;
    u64 opl = string.size;
    while(first < opl && char_is_space(string.str[first])) {
        first += 1;
    }
    while(opl > first && char_is_space(string.str[opl - 1])) {
        opl -= 1;
    }
    return str8_substr(string, first, opl);
}

internal u64 str8_find_needle(String8 string, u64 start, String8 needle) {
    for(u64 i = start; i + needle.size <= string.size; i += 1) {
        if(MemoryMatch(string.str + i, needle.str, needle.size)) {
            return i;
        }
    }
    return string.size;
}

internal u64 str8_find_last_char(String8 string, u8 c) {
    for(u64 i = string.size; i > 0; i -= 1) {
        if(string.str[i - 1] == c) {
            return i - 1;
        }
    }
    return string.size;
}

internal bool str8_contains_char(String8 string, u8 c) {
    for(u64 i = 0; i < string.size; i += 1) {
        if(string.str[i] == c) {
            return true;
        }
    }
    return false;
}

internal bool str8_is_integer(String8 string) {
    String8 digits = string;
    if(digits.size > 0 && digits.str[0] == '-') {
        digits = str8_skip(digits, 1);
    }
    if(digits.size == 0) {
        return false;
    }
    for(u64 i = 0; i < digits.size; i += 1) {
        if(!char_is_digit(digits.str[i])) {
            return false;
        }
    }
    return true;
}

internal bool try_i64_from_str8(String8 string, i64 *out) {
    if(!str8_is_integer(string)) {
        return false;
    }

    bool negative = string.str[0] == '-';
    String8 digits = negative ? str8_skip(string, 1) : string;

    // Accumulate towards the sign, so INT64_MIN parses too
    i64 value = 0;
    for(u64 i = 0; i < digits.size; i += 1) {
        i64 digit = digits.str[i] - '0';
        if(ckd_mul(&value, value, 10) || ckd_add(&value, value, negative ? -digit : digit)) {
            return false;
        }
    }

    *out = value;
    return true;
}

internal String8 push_str8_copy(Arena *arena, String8 string) {
    u8 *str = push_array_no_zero(arena, u8, string.size + 1);
    if(string.size > 0) {
        MemoryCopy(str, string.str, string.size);
    }
    str[string.size] = 0;
    return str8(str, string.size);
}

internal String8 push_str8fv(Arena *arena, char const *fmt, va_list args) {
    va_list args_copy;
    va_copy(args_copy, args);
    int needed = vsnprintf(nullptr, 0, fmt, args);
    AssertAlways(needed >= 0);

    u64 size = (u64)needed;
    u8 *str = push_array_no_zero(arena, u8, size + 1);
    vsnprintf((char *)str, size + 1, fmt, args_copy);
    va_end(args_copy);
    return str8(str, size);
}

internal String8 push_str8f(Arena *arena, char const *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    String8 result = push_str8fv(arena, fmt, args);
    va_end(args);
    return result;
}

internal void str8_list_push(Arena *arena, String8List *list, String8 string) {
    String8Node *node = push_array(arena, String8Node, 1);
    node->string = string;
    if(list->last) {
        list->last->next = node;
    } else {
        list->first = node;
    }
    list->last = node;
    list->node_count += 1;
    list->total_size += string.size;
}

internal void str8_list_pushf(Arena *arena, String8List *list, char const *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    String8 string = push_str8fv(arena, fmt, args);
    va_end(args);
    str8_list_push(arena, list, string);
}

internal String8 str8_list_join(Arena *arena, String8List *list, String8 separator) {
    u64 separators = list->node_count > 0 ? list->node_count - 1 : 0;
    u64 size = list->total_size + separators * separator.size;
    u8 *str = push_array_no_zero(arena, u8, size + 1);

    u8 *at = str;
    for(String8Node *node = list->first; node; node = node->next) {
        if(node != list->first && separator.size > 0) {
            MemoryCopy(at, separator.str, separator.size);
            at += separator.size;
        }
        if(node->string.size > 0) {
            MemoryCopy(at, node->string.str, node->string.size);
            at += node->string.size;
        }
    }
    *at = 0;
    return str8(str, size);
}
