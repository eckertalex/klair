global String8 usage = str8_lit_comp(
    "Usage: klair [command] [flags]\n"
    "\n"
    "Commands:\n"
    "  status   [-l NAME]\n"
    "  on       [-b 3-100] [-t 2900-7000] [-l NAME]\n"
    "  off      [-l NAME]\n"
    "  help\n"
    "  version\n"
    "\n"
    "No command: same as status.\n");

internal String8 config_path_from_env(Arena *arena) {
#if OS_WINDOWS
    String8 app_data = str8_from_env(arena, "APPDATA");
    if(app_data.size == 0) {
        return str8_zero();
    }
    return push_str8f(arena, "%.*s\\klair", str8_varg(app_data));
#else
    String8 xdg = str8_from_env(arena, "XDG_CONFIG_HOME");
    if(xdg.size > 0 && xdg.str[0] == '/') {
        return push_str8f(arena, "%.*s/klair", str8_varg(xdg));
    }
    String8 home = str8_from_env(arena, "HOME");
    if(home.size == 0) {
        return str8_zero();
    }
    return push_str8f(arena, "%.*s/.config/klair", str8_varg(home));
#endif
}

internal int klair_run(Arena *arena, u64 argc, char **argv) {
    Args args = args_from_argv(arena, argc, argv);
    if(args.error.size > 0) {
        fprintf(stderr, "%.*s\n\n", str8_varg(args.error));
        write_str8(stderr, usage);
        return 2;
    }
    if(args.command == Command_Help) {
        write_str8(stdout, usage);
        return 0;
    }
    if(args.command == Command_Version) {
        fputs(BUILD_VERSION_FULL "\n", stdout);
        return 0;
    }


    String8 path = config_path_from_env(arena);
    if(path.size == 0) {
        fputs("cannot determine config directory\n", stderr);
        return 1;
    }

    String8 text = {0};
    if(!data_from_file_path(arena, path, &text)) {
        fprintf(stderr, "cannot read config: %.*s\n", str8_varg(path));
        return 1;
    }

    Config config = config_from_text(arena, text);
    if(config.error.size > 0) {
        fprintf(stderr, "invalid config %.*s: line %" PRIu64 ": %.*s\n",
                str8_varg(path), config.error_line, str8_varg(config.error));
        return 1;
    }
    if(config.lights.count == 0) {
        fprintf(stderr, "no lights in config: %.*s\n", str8_varg(path));
        return 1;
    }


    LightArray targets = config.lights;
    if(args.has_light_name) {
        targets = (LightArray){0};
        String8List names = {0};
        for(u64 i = 0; i < config.lights.count; i += 1) {
            Light *light = &config.lights.v[i];
            str8_list_push(arena, &names, light->name);
            if(str8_match(light->name, args.light_name)) {
                targets = (LightArray){light, 1};
            }
        }
        if(targets.count == 0) {
            String8 available = str8_list_join(arena, &names, str8_lit(", "));
            fprintf(stderr, "light \"%.*s\" not found; available: %.*s\n",
                    str8_varg(args.light_name), str8_varg(available));
            return 1;
        }
    }


    Method method = Method_Put;
    LightSettings settings = {0};
    String8 verb = str8_lit("Update");
    switch(args.command) {
    case Command_Status: {
        method = Method_Get;
        verb = str8_lit("Status");
    } break;
    case Command_On: {
        settings.on = 1;
        settings.has_brightness = args.has_brightness;
        settings.brightness = args.brightness;
        settings.has_temperature = args.has_temperature;
        settings.temperature = args.has_temperature ? mired_from_kelvin(args.temperature) : 0;
    } break;
    case Command_Off: break;
    case Command_Help:
    case Command_Version:
    case Command_COUNT: AssertAlways(false); break;
    }

    Job *jobs = push_array(arena, Job, targets.count);
    for(u64 i = 0; i < targets.count; i += 1) {
        jobs[i].light = &targets.v[i];
        jobs[i].request = request_from_settings(arena, method, targets.v[i].host, settings);
        jobs[i].response_arena = arena_alloc(MB(64));
    }
    jobs_run(arena, jobs, targets.count);


    String8List out = {0};
    bool all_ok = true;
    for(u64 i = 0; i < targets.count; i += 1) {
        Job *job = &jobs[i];
        String8 name = job->light->name;
        if(!job->ok) {
            String8 message = message_from_job(arena, job);
            str8_list_pushf(arena, &out, "%.*s of light \"%.*s\": error: %.*s\n",
                            str8_varg(verb), str8_varg(name), str8_varg(message));
            all_ok = false;
            continue;
        }
        str8_list_pushf(arena, &out,
                        "Status of light \"%.*s\":\n"
                        "  Power:       %s\n"
                        "  Brightness:  %" PRId64 "%%\n"
                        "  Temperature: %" PRId64 "K\n",
                        str8_varg(name),
                        job->light_state.on == 1 ? "ON" : "OFF",
                        job->light_state.brightness,
                        kelvin_from_mired(job->light_state.temperature));
    }
    write_str8(stdout, str8_list_join(arena, &out, str8_zero()));

    return all_ok ? 0 : 1;
}
