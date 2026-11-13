#ifndef KLAIR_H
#define KLAIR_H


global constexpr i64 BRIGHTNESS_MIN = 3;
global constexpr i64 BRIGHTNESS_MAX = 100;
global constexpr i64 KELVIN_MIN = 2900;
global constexpr i64 KELVIN_MAX = 7000;
global constexpr i64 MIRED_MIN = 143; // what the light accepts
global constexpr i64 MIRED_MAX = 344;

global constexpr u64 ATTEMPT_COUNT = 3;
global constexpr u64 ATTEMPT_TIMEOUT_US = 3'000'000;
global constexpr u64 FIRST_BACKOFF_US = 100'000;
global constexpr u64 MAX_BODY_SIZE = KB(64);
global constexpr u64 MAX_HEADER_SIZE = KB(64); // per response, separate from the body limit
global constexpr u64 READ_CHUNK_SIZE = KB(4);


typedef struct Light Light;
struct Light {
    String8 name;
    String8 address; // as written, for messages
    String8 host;    // the address without the port, for the Host header
    NET_Address net_address;
};

typedef struct LightArray LightArray;
struct LightArray {
    Light *v;
    u64 count;
};

typedef struct Config Config;
struct Config {
    LightArray lights;
    u64 error_line; // 1 based, 0 when the config is fine
    String8 error;
};

typedef enum Command {
    Command_Status,
    Command_On,
    Command_Off,
    Command_Help,
    Command_Version,
    Command_COUNT,
} Command;

typedef struct Args Args;
struct Args {
    Command command;
    bool has_light_name;
    String8 light_name;
    bool has_brightness;
    i64 brightness;
    bool has_temperature;
    i64 temperature; // Kelvin
    String8 error;   // a usage error when not empty
};

typedef enum Method {
    Method_Get,
    Method_Put,
} Method;

typedef struct LightSettings LightSettings;
struct LightSettings {
    i64 on;
    bool has_brightness;
    i64 brightness;
    bool has_temperature;
    i64 temperature; // mired
};

typedef struct LightState LightState;
struct LightState {
    i64 on;
    i64 brightness;
    i64 temperature; // mired
};

typedef enum ResponseParse {
    ResponseParse_Incomplete, // keep reading
    ResponseParse_Complete,
    ResponseParse_ClosedEarly,
    ResponseParse_Invalid,
} ResponseParse;

typedef struct Response Response;
struct Response {
    i64 status;
    String8 body;
};

typedef enum LightError {
    LightError_None,
    LightError_ConnectFailed,
    LightError_Timeout,
    LightError_Closed,
    LightError_Status,
    LightError_Invalid,
} LightError;

typedef struct Retry Retry;
struct Retry {
    u64 attempts_left;
    u64 backoff_us;
};

typedef enum JobState {
    JobState_Waiting, // until wake_at_us, then the next attempt
    JobState_Dialing,
    JobState_Sending,
    JobState_Receiving,
    JobState_Done,
} JobState;

typedef struct Job Job;
struct Job {
    Light *light;
    String8 request;

    Arena *response_arena;
    u64 response_start;
    String8 response;

    bool has_socket;
    NET_Socket socket;

    JobState state;
    Retry retry;
    u64 wake_at_us;
    u64 deadline_us; // of the current attempt

    bool ok;
    LightState light_state;
    LightError error; // of the last attempt
    i64 status;       // for LightError_Status
};


internal i64 kelvin_from_mired(i64 mired);
internal i64 mired_from_kelvin(i64 kelvin);

internal Config config_from_text(Arena *arena, String8 text);

internal Args args_from_argv(Arena *arena, u64 count, char **argv);

internal String8 body_from_settings(Arena *arena, LightSettings settings);
internal String8 request_from_settings(Arena *arena, Method method, String8 host, LightSettings settings);
internal ResponseParse response_parse(String8 data, bool closed, Response *out);
internal bool light_state_from_json(String8 json, LightState *out);

internal Retry retry_make(void);
internal bool retry_advance(Retry *retry, u64 *wait_us);
internal void jobs_run(Arena *arena, Job *jobs, u64 count);
internal String8 message_from_job(Arena *arena, Job *job);

internal int klair_run(Arena *arena, u64 argc, char **argv);

#endif // KLAIR_H
