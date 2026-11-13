
internal String8 body_from_settings(Arena *arena, LightSettings settings) {
    String8List parts = {0};
    str8_list_pushf(arena, &parts, "{\"lights\":[{\"on\":%" PRId64, settings.on);
    if(settings.has_brightness) {
        str8_list_pushf(arena, &parts, ",\"brightness\":%" PRId64, settings.brightness);
    }
    if(settings.has_temperature) {
        str8_list_pushf(arena, &parts, ",\"temperature\":%" PRId64, settings.temperature);
    }
    str8_list_push(arena, &parts, str8_lit("}]}"));
    return str8_list_join(arena, &parts, str8_zero());
}

internal String8 request_from_settings(Arena *arena, Method method, String8 host, LightSettings settings) {
    if(method == Method_Get) {
        return push_str8f(arena,
                          "GET /elgato/lights HTTP/1.1\r\n"
                          "Host: %.*s\r\n"
                          "Connection: close\r\n"
                          "Accept: application/json\r\n"
                          "\r\n",
                          str8_varg(host));
    }

    String8 body = body_from_settings(arena, settings);
    return push_str8f(arena,
                      "PUT /elgato/lights HTTP/1.1\r\n"
                      "Host: %.*s\r\n"
                      "Connection: close\r\n"
                      "Accept: application/json\r\n"
                      "Content-Type: application/json\r\n"
                      "Content-Length: %" PRIu64 "\r\n"
                      "\r\n"
                      "%.*s",
                      str8_varg(host), body.size, str8_varg(body));
}


// Complete at Content-Length, or on close when it is absent.
internal ResponseParse response_parse(String8 data, bool closed, Response *out) {
    u64 header_end = str8_find_needle(data, 0, str8_lit("\r\n\r\n"));
    if(header_end == data.size) {
        return closed ? ResponseParse_ClosedEarly : ResponseParse_Incomplete;
    }
    String8 headers = str8_prefix(data, header_end);
    String8 body = str8_skip(data, header_end + 4);

    u64 status_line_end = str8_find_needle(headers, 0, str8_lit("\r\n"));
    String8 status_line = str8_prefix(headers, status_line_end);
    u64 space = str8_find_needle(status_line, 0, str8_lit(" "));
    String8 version = str8_prefix(status_line, space);
    String8 after_version = str8_skip(status_line, space + 1);
    String8 code = str8_prefix(after_version, str8_find_needle(after_version, 0, str8_lit(" ")));
    i64 status = 0;
    if(!str8_match(str8_prefix(version, 5), str8_lit("HTTP/")) || code.size != 3 || !try_i64_from_str8(code, &status)) {
        return ResponseParse_Invalid;
    }

    bool has_length = false;
    i64 length = 0;
    for(u64 pos = status_line_end + 2; pos < headers.size;) {
        u64 line_end = str8_find_needle(headers, pos, str8_lit("\r\n"));
        String8 line = str8_substr(headers, pos, line_end);
        pos = line_end + 2;

        u64 colon = str8_find_needle(line, 0, str8_lit(":"));
        String8 name = str8_skip_chop_whitespace(str8_prefix(line, colon));
        String8 value = str8_skip_chop_whitespace(str8_skip(line, colon + 1));
        if(str8_match_case_insensitive(name, str8_lit("content-length"))) {
            if(!try_i64_from_str8(value, &length) || length < 0) {
                return ResponseParse_Invalid;
            }
            has_length = true;
        }
    }

    if(has_length) {
        if(body.size < (u64)length) {
            return closed ? ResponseParse_ClosedEarly : ResponseParse_Incomplete;
        }
        body = str8_prefix(body, (u64)length);
    } else if(!closed) {
        return ResponseParse_Incomplete;
    }

    out->status = status;
    out->body = body;
    return ResponseParse_Complete;
}


internal u64 json_skip_space(String8 json, u64 pos) {
    while(pos < json.size && char_is_space(json.str[pos])) {
        pos += 1;
    }
    return pos;
}

internal u64 json_skip_string(String8 json, u64 pos) {
    for(pos += 1; pos < json.size; pos += 1) {
        if(json.str[pos] == '\\') {
            pos += 1;
        } else if(json.str[pos] == '"') {
            return pos + 1;
        }
    }
    return json.size;
}

internal u64 json_skip_value(String8 json, u64 pos) {
    if(pos >= json.size) {
        return json.size;
    }

    u8 c = json.str[pos];
    if(c == '"') {
        return json_skip_string(json, pos);
    }

    if(c == '{' || c == '[') {
        u64 depth = 0;
        while(pos < json.size) {
            c = json.str[pos];
            if(c == '"') {
                pos = json_skip_string(json, pos);
                continue;
            }
            if(c == '{' || c == '[') {
                depth += 1;
            } else if(c == '}' || c == ']') {
                depth -= 1;
                if(depth == 0) {
                    return pos + 1;
                }
            }
            pos += 1;
        }
        return json.size;
    }

    u64 start = pos;
    while(pos < json.size && !char_is_space(json.str[pos]) &&
          json.str[pos] != ',' && json.str[pos] != '}' && json.str[pos] != ']') {
        pos += 1;
    }
    return pos == start ? json.size : pos;
}

internal bool json_find_key(String8 json, u64 object_pos, String8 key, u64 *value_pos) {
    if(object_pos >= json.size || json.str[object_pos] != '{') {
        return false;
    }

    for(u64 pos = json_skip_space(json, object_pos + 1); pos < json.size;) {
        if(json.str[pos] != '"') {
            return false;
        }
        u64 key_end = json_skip_string(json, pos);
        String8 name = str8_substr(json, pos + 1, key_end - 1);

        pos = json_skip_space(json, key_end);
        if(pos >= json.size || json.str[pos] != ':') {
            return false;
        }
        pos = json_skip_space(json, pos + 1);
        if(str8_match(name, key)) {
            *value_pos = pos;
            return true;
        }

        pos = json_skip_space(json, json_skip_value(json, pos));
        if(pos >= json.size || json.str[pos] != ',') {
            return false;
        }
        pos = json_skip_space(json, pos + 1);
    }
    return false;
}

internal bool json_integer_at(String8 json, u64 pos, i64 *out) {
    u64 end = json_skip_value(json, pos);
    return end < json.size && try_i64_from_str8(str8_substr(json, pos, end), out);
}

internal bool light_state_from_json(String8 json, LightState *out) {
    u64 object = json_skip_space(json, 0);
    u64 lights = 0;
    if(!json_find_key(json, object, str8_lit("lights"), &lights) || lights >= json.size || json.str[lights] != '[') {
        return false;
    }
    u64 first = json_skip_space(json, lights + 1);

    u64 on = 0, brightness = 0, temperature = 0;
    LightState state = {0};
    bool ok = json_find_key(json, first, str8_lit("on"), &on) &&
              json_integer_at(json, on, &state.on) &&
              json_find_key(json, first, str8_lit("brightness"), &brightness) &&
              json_integer_at(json, brightness, &state.brightness) &&
              json_find_key(json, first, str8_lit("temperature"), &temperature) &&
              json_integer_at(json, temperature, &state.temperature);

    if(!ok || state.temperature <= 0) {
        return false;
    }
    *out = state;
    return true;
}
