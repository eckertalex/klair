internal bool light_address_from_str8(String8 address, String8 *host, NET_Address *out) {
    u64 colon = str8_find_last_char(address, ':');
    if(colon == address.size) {
        return false;
    }

    String8 port_text = str8_skip(address, colon + 1);
    i64 port = 0;
    if(!try_i64_from_str8(port_text, &port) || port < 1 || port > 65535) {
        return false;
    }

    *host = str8_prefix(address, colon);
    String8 ip = *host;
    bool is_ip6 = ip.size >= 2 && ip.str[0] == '[' && ip.str[ip.size - 1] == ']';
    if(is_ip6) {
        ip = str8_substr(ip, 1, ip.size - 1);
    }
    return net_address_from_str8(ip, is_ip6, (u16)port, out);
}

internal String8 config_error_from_line(Arena *arena, String8 line, LightArray lights, Light *out) {
    u64 equals = str8_find_needle(line, 0, str8_lit("="));
    if(equals == line.size) {
        return str8_lit("expected name = address");
    }

    String8 name = str8_skip_chop_whitespace(str8_prefix(line, equals));
    String8 address = str8_skip_chop_whitespace(str8_skip(line, equals + 1));
    if(name.size == 0) {
        return str8_lit("empty name");
    }
    if(str8_contains_char(name, ' ') || str8_contains_char(name, '\t')) {
        return push_str8f(arena, "name \"%.*s\" contains whitespace", str8_varg(name));
    }
    if(address.size == 0) {
        return str8_lit("empty address");
    }

    Light light = {.name = name, .address = address};
    if(!light_address_from_str8(address, &light.host, &light.net_address)) {
        return push_str8f(arena, "invalid address \"%.*s\"", str8_varg(address));
    }

    for(u64 i = 0; i < lights.count; i += 1) {
        if(str8_match(lights.v[i].name, name)) {
            return push_str8f(arena, "duplicate name \"%.*s\"", str8_varg(name));
        }
    }

    *out = light;
    return str8_zero();
}

internal Config config_from_text(Arena *arena, String8 text) {
    Config config = {0};

    u64 line_number = 0;
    for(u64 line_start = 0; line_start < text.size;) {
        u64 line_end = str8_find_needle(text, line_start, str8_lit("\n"));
        String8 line = str8_skip_chop_whitespace(str8_substr(text, line_start, line_end));
        line_start = line_end + 1;
        line_number += 1;

        if(line.size == 0 || line.str[0] == '#') {
            continue;
        }

        Light light = {0};
        String8 error = config_error_from_line(arena, line, config.lights, &light);
        if(error.size > 0) {
            config.error_line = line_number;
            config.error = error;
            return config;
        }

        Light *slot = push_array_no_zero(arena, Light, 1);
        if(config.lights.count == 0) {
            config.lights.v = slot;
        }
        Assert(slot == config.lights.v + config.lights.count);
        *slot = light;
        config.lights.count += 1;
    }

    return config;
}
