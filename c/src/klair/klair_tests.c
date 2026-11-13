#define BUILD_NAME "klair_tests"


#include "base/base_inc.h"
#include "net/net_inc.h"
#include "klair/klair.h"

#include "base/base_inc.c"
#include "net/net_inc.c"
#include "klair/klair_temperature.c"
#include "klair/klair_config.c"
#include "klair/klair_args.c"
#include "klair/klair_http.c"
#include "klair/klair_client.c"
#include "klair/klair_cli.c"


global u64 checks_run;
global u64 checks_failed;

internal void check_(bool ok, char const *expression, char const *file, int line) {
    checks_run += 1;
    if(!ok) {
        checks_failed += 1;
        fprintf(stderr, "%s:%d: check failed: %s\n", file, line, expression);
    }
}

#define Check(x) check_((x), #x, __FILE__, __LINE__)
#define CheckStr8(a, b) check_(str8_match((a), (b)), #a " == " #b, __FILE__, __LINE__)


internal void test_units(void) {
    Check(mired_from_kelvin(4700) == 213);
    Check(kelvin_from_mired(213) == 4700);

    // The five values rounding twice gets wrong, and a tie that rounds up
    Check(kelvin_from_mired(181) == 5500);
    Check(kelvin_from_mired(221) == 4500);
    Check(kelvin_from_mired(226) == 4400);
    Check(kelvin_from_mired(292) == 3400);
    Check(kelvin_from_mired(315) == 3150);
    Check(kelvin_from_mired(320) == 3150);

    // 2900K would be 345, the light takes up to 344
    Check(mired_from_kelvin(2900) == 344);
    Check(mired_from_kelvin(7000) == 143);
    Check(mired_from_kelvin(3150) == 317);

    for(i64 kelvin = KELVIN_MIN; kelvin <= KELVIN_MAX; kelvin += 50) {
        Check(kelvin_from_mired(mired_from_kelvin(kelvin)) == kelvin);
    }
}


internal String8 config_error(Arena *arena, char const *text, u64 *line) {
    Config config = config_from_text(arena, str8_cstring(text));
    *line = config.error_line;
    return config.error;
}

internal void test_config(Arena *arena) {
    Config config = config_from_text(arena, str8_lit(
        "# desk lights\r\n"
        "left  = 192.168.2.164:9123\r\n"
        "\n"
        "  right=[fe80::1]:9123  \n"));
    Check(config.error.size == 0);
    Check(config.lights.count == 2);
    CheckStr8(config.lights.v[0].name, str8_lit("left"));
    CheckStr8(config.lights.v[0].host, str8_lit("192.168.2.164"));
    Check(config.lights.v[0].net_address.port == 9123);
    CheckStr8(config.lights.v[1].name, str8_lit("right"));
    CheckStr8(config.lights.v[1].host, str8_lit("[fe80::1]"));
    Check(config.lights.v[1].net_address.is_ip6);

    Check(config_from_text(arena, str8_lit("# only a comment\n")).lights.count == 0);

    u64 line = 0;
    CheckStr8(config_error(arena, "left 127.0.0.1:19123", &line), str8_lit("expected name = address"));
    Check(line == 1);
    CheckStr8(config_error(arena, " = 127.0.0.1:19123", &line), str8_lit("empty name"));
    CheckStr8(config_error(arena, "my light = 127.0.0.1:19123", &line), str8_lit("name \"my light\" contains whitespace"));
    CheckStr8(config_error(arena, "left =", &line), str8_lit("empty address"));
    CheckStr8(config_error(arena, "left = lights.local:9123", &line), str8_lit("invalid address \"lights.local:9123\""));
    CheckStr8(config_error(arena, "left = 127.0.0.1:0", &line), str8_lit("invalid address \"127.0.0.1:0\""));
    CheckStr8(config_error(arena, "left = 127.0.0.1", &line), str8_lit("invalid address \"127.0.0.1\""));
    CheckStr8(config_error(arena, "left = 127.0.0.1:65536", &line), str8_lit("invalid address \"127.0.0.1:65536\""));
    CheckStr8(config_error(arena, "left = 127.0.0.1:", &line), str8_lit("invalid address \"127.0.0.1:\""));
    CheckStr8(config_error(arena, "left = fe80::1:9123", &line), str8_lit("invalid address \"fe80::1:9123\""));

    CheckStr8(config_error(arena, "left = 127.0.0.1:1\n# comment\n\nleft = 127.0.0.1:2\n", &line),
              str8_lit("duplicate name \"left\""));
    Check(line == 4);
}


typedef struct ArgsCase ArgsCase;
struct ArgsCase {
    char *argv[6];
    char *error;
};

internal Args args_from_case(Arena *arena, ArgsCase *c) {
    u64 count = 0;
    while(count < ArrayCount(c->argv) && c->argv[count]) {
        count += 1;
    }
    return args_from_argv(arena, count, c->argv);
}

internal void test_args(Arena *arena) {
    ArgsCase usage_errors[] = {
        {{"klair", "bogus"}, "unknown command: bogus"},
        {{"klair", "status", "-b", "50"}, "unknown flag for status: -b"},
        {{"klair", "-b", "50"}, "unknown flag for status: -b"},
        {{"klair", "off", "-x"}, "unknown flag for off: -x"},
        {{"klair", "on", "-b=50"}, "unknown flag for on: -b=50"},
        {{"klair", "status", "extra"}, "unexpected argument: extra"},
        {{"klair", "-h"}, "unknown flag for status: -h"},
        {{"klair", "--help"}, "unknown flag for status: --help"},
        {{"klair", "help", "me"}, "unexpected argument: me"},
        {{"klair", "version", "-l", "normal"}, "unknown flag for version: -l"},
        {{"klair", "on", "-b"}, "missing value for -b"},
        {{"klair", "on", "-b", "12x"}, "invalid value for -b: 12x"},
        {{"klair", "on", "-b", "+50"}, "invalid value for -b: +50"},
        {{"klair", "on", "-b", "2"}, "invalid brightness: must be between 3 and 100"},
        {{"klair", "on", "-b", "-5"}, "invalid brightness: must be between 3 and 100"},
        {{"klair", "on", "-b", "4294967346"}, "invalid brightness: must be between 3 and 100"},
        {{"klair", "on", "-b", "18446744073709551666"}, "invalid value for -b: 18446744073709551666"},
        {{"klair", "on", "-t", "18446744073709551666"}, "invalid value for -t: 18446744073709551666"},
        {{"klair", "on", "-t", "9000"}, "invalid temperature: must be between 2900K and 7000K"},
        {{"klair", "on", "-b", "2", "-x"}, "unknown flag for on: -x"},
        {{"klair", "on", "-b", "2", "-t", "9000"}, "invalid brightness: must be between 3 and 100"},
    };
    for(u64 i = 0; i < ArrayCount(usage_errors); i += 1) {
        Args args = args_from_case(arena, &usage_errors[i]);
        if(!str8_match(args.error, str8_cstring(usage_errors[i].error))) {
            fprintf(stderr, "args case %" PRIu64 ": got \"%.*s\"\n", i, str8_varg(args.error));
        }
        CheckStr8(args.error, str8_cstring(usage_errors[i].error));
    }

    ArgsCase no_command = {.argv = {"klair"}};
    Args args = args_from_case(arena, &no_command);
    Check(args.error.size == 0 && args.command == Command_Status);

    ArgsCase flags_only = {.argv = {"klair", "-l", "left"}};
    args = args_from_case(arena, &flags_only);
    Check(args.error.size == 0 && args.command == Command_Status && args.has_light_name);
    CheckStr8(args.light_name, str8_lit("left"));

    ArgsCase light_named_like_a_flag = {.argv = {"klair", "status", "-l", "-b"}};
    args = args_from_case(arena, &light_named_like_a_flag);
    Check(args.error.size == 0);
    CheckStr8(args.light_name, str8_lit("-b"));

    ArgsCase last_one_wins = {.argv = {"klair", "on", "-b", "10", "-b", "60"}};
    args = args_from_case(arena, &last_one_wins);
    Check(args.error.size == 0 && args.has_brightness && args.brightness == 60 && !args.has_temperature);

    ArgsCase help = {.argv = {"klair", "help"}};
    Check(args_from_case(arena, &help).command == Command_Help);
    ArgsCase version = {.argv = {"klair", "version"}};
    Check(args_from_case(arena, &version).command == Command_Version);
}


internal void test_bodies(Arena *arena) {
    CheckStr8(body_from_settings(arena, (LightSettings){.on = 0}), str8_lit("{\"lights\":[{\"on\":0}]}"));
    CheckStr8(body_from_settings(arena, (LightSettings){.on = 1}), str8_lit("{\"lights\":[{\"on\":1}]}"));
    CheckStr8(body_from_settings(arena, (LightSettings){.on = 1, .has_brightness = true, .brightness = 50}),
              str8_lit("{\"lights\":[{\"on\":1,\"brightness\":50}]}"));
    CheckStr8(body_from_settings(arena, (LightSettings){.on = 1, .has_temperature = true, .temperature = 213}),
              str8_lit("{\"lights\":[{\"on\":1,\"temperature\":213}]}"));
    CheckStr8(body_from_settings(arena, (LightSettings){.on = 1, .has_brightness = true, .brightness = 50,
                                                        .has_temperature = true, .temperature = 213}),
              str8_lit("{\"lights\":[{\"on\":1,\"brightness\":50,\"temperature\":213}]}"));

    CheckStr8(request_from_settings(arena, Method_Put, str8_lit("127.0.0.1"), (LightSettings){.on = 0}),
              str8_lit("PUT /elgato/lights HTTP/1.1\r\n"
                       "Host: 127.0.0.1\r\n"
                       "Connection: close\r\n"
                       "Accept: application/json\r\n"
                       "Content-Type: application/json\r\n"
                       "Content-Length: 21\r\n"
                       "\r\n"
                       "{\"lights\":[{\"on\":0}]}"));
}

internal void test_responses(void) {
    Response response = {0};
    Check(response_parse(str8_lit("HTTP/1.1 200 OK\r\nContent-"), false, &response) == ResponseParse_Incomplete);
    Check(response_parse(str8_lit("HTTP/1.1 200 OK\r\nContent-"), true, &response) == ResponseParse_ClosedEarly);

    Check(response_parse(str8_lit("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nab"), false, &response) == ResponseParse_Incomplete);
    Check(response_parse(str8_lit("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nab"), true, &response) == ResponseParse_ClosedEarly);
    Check(response_parse(str8_lit("HTTP/1.1 200 OK\r\ncontent-length: 5\r\n\r\nabcde"), false, &response) == ResponseParse_Complete);
    Check(response.status == 200);
    CheckStr8(response.body, str8_lit("abcde"));

    Check(response_parse(str8_lit("HTTP/1.1 200 OK\r\n\r\n{}"), false, &response) == ResponseParse_Incomplete);
    Check(response_parse(str8_lit("HTTP/1.1 200 OK\r\n\r\n{}"), true, &response) == ResponseParse_Complete);
    CheckStr8(response.body, str8_lit("{}"));

    Check(response_parse(str8_lit("HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n"), false, &response) == ResponseParse_Complete);
    Check(response.status == 500);

    Check(response_parse(str8_lit("garbage\r\n\r\n"), false, &response) == ResponseParse_Invalid);
    Check(response_parse(str8_lit("HTTP/1.1 200 OK\r\nContent-Length: x\r\n\r\n"), false, &response) == ResponseParse_Invalid);

    LightState state = {0};
    Check(light_state_from_json(str8_lit("{\"numberOfLights\":1,\"lights\":[{\"on\":1,\"brightness\":20,\"temperature\":213}]}"), &state));
    Check(state.on == 1 && state.brightness == 20 && state.temperature == 213);
    Check(light_state_from_json(str8_lit(" { \"lights\" : [ { \"temperature\" : 300 , \"on\" : 0 , \"brightness\" : 3 } ] } "), &state));
    Check(state.on == 0 && state.brightness == 3 && state.temperature == 300);
    Check(light_state_from_json(str8_lit("{\"x\":{\"on\":2},\"lights\":[{\"on\":1,\"brightness\":20,\"temperature\":213}]}"), &state));
    Check(state.on == 1);

    Check(!light_state_from_json(str8_lit("not json"), &state));
    Check(!light_state_from_json(str8_lit("{\"lights\":[]}"), &state));
    Check(!light_state_from_json(str8_lit("{\"lights\":[{\"on\":1,\"brightness\":20}]}"), &state));
    Check(!light_state_from_json(str8_lit("{\"lights\":[{\"on\":1,\"brightness\":20,\"temperature\":0}]}"), &state));
    Check(!light_state_from_json(str8_lit("{\"lights\":[{\"on\":1,\"brightness\":2.5,\"temperature\":213}]}"), &state));
    Check(!light_state_from_json(str8_lit("{\"lights\":[{\"on\":1,\"brightness\":20,\"temperature\":213"), &state));
}


internal void test_retry(void) {
    Retry retry = retry_make();
    u64 wait_us = 0;
    Check(retry_advance(&retry, &wait_us) && wait_us == 100'000);
    Check(retry_advance(&retry, &wait_us) && wait_us == 200'000);
    Check(!retry_advance(&retry, &wait_us));
}

int main(void) {
    Arena *arena = arena_alloc(GB(1));

    test_units();
    test_config(arena);
    test_args(arena);
    test_bodies(arena);
    test_responses();
    test_retry();

    printf("%" PRIu64 " checks, %" PRIu64 " failed\n", checks_run, checks_failed);
    return checks_failed == 0 ? 0 : 1;
}
