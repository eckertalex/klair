global String8 command_names[Command_COUNT] = {
    [Command_Status] = str8_lit_comp("status"),
    [Command_On] = str8_lit_comp("on"),
    [Command_Off] = str8_lit_comp("off"),
    [Command_Help] = str8_lit_comp("help"),
    [Command_Version] = str8_lit_comp("version"),
};

internal Args args_from_argv(Arena *arena, u64 count, char **argv) {
    Args args = {.command = Command_Status};

    u64 first_flag = 1;
    if(count > 1 && argv[1][0] != '-') {
        String8 name = str8_cstring(argv[1]);
        bool found = false;
        for(u64 i = 0; i < Command_COUNT; i += 1) {
            if(str8_match(name, command_names[i])) {
                args.command = (Command)i;
                found = true;
            }
        }
        if(!found) {
            args.error = push_str8f(arena, "unknown command: %.*s", str8_varg(name));
            return args;
        }
        first_flag = 2;
    }

    String8 command_name = command_names[args.command];
    bool takes_light = args.command == Command_Status || args.command == Command_On || args.command == Command_Off;
    bool takes_settings = args.command == Command_On;

    for(u64 i = first_flag; i < count; i += 1) {
        String8 arg = str8_cstring(argv[i]);
        bool is_light = takes_light && str8_match(arg, str8_lit("-l"));
        bool is_brightness = takes_settings && str8_match(arg, str8_lit("-b"));
        bool is_temperature = takes_settings && str8_match(arg, str8_lit("-t"));

        if(!is_light && !is_brightness && !is_temperature) {
            if(arg.size > 0 && arg.str[0] == '-') {
                args.error = push_str8f(arena, "unknown flag for %.*s: %.*s", str8_varg(command_name), str8_varg(arg));
            } else {
                args.error = push_str8f(arena, "unexpected argument: %.*s", str8_varg(arg));
            }
            return args;
        }

        if(i + 1 >= count) {
            args.error = push_str8f(arena, "missing value for %.*s", str8_varg(arg));
            return args;
        }
        i += 1;
        String8 value = str8_cstring(argv[i]);

        if(is_light) {
            args.has_light_name = true;
            args.light_name = value;
            continue;
        }

        i64 number = 0;
        if(!try_i64_from_str8(value, &number)) {
            args.error = push_str8f(arena, "invalid value for %.*s: %.*s", str8_varg(arg), str8_varg(value));
            return args;
        }

        if(is_brightness) {
            args.has_brightness = true;
            args.brightness = number;
        } else {
            args.has_temperature = true;
            args.temperature = number;
        }
    }

    if(args.has_brightness && (args.brightness < BRIGHTNESS_MIN || args.brightness > BRIGHTNESS_MAX)) {
        args.error = str8_lit("invalid brightness: must be between 3 and 100");
    } else if(args.has_temperature && (args.temperature < KELVIN_MIN || args.temperature > KELVIN_MAX)) {
        args.error = str8_lit("invalid temperature: must be between 2900K and 7000K");
    }
    return args;
}
