#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

typedef uint8_t u8;
typedef uint32_t u32;
typedef int32_t s32;
typedef s32 b32;

#ifndef KLAIR_VERSION
#define KLAIR_VERSION "v0.0.0"
#endif

#define MAX_LIGHTS 16
#define UNSET (-1)
#define ArrayCount(a) (sizeof(a) / sizeof((a)[0]))

typedef struct {
    s32 on;
    s32 brightness;
    s32 temperature;
} LightDetail;

typedef struct {
    char name[64];
    char ip[64];
} LightConfig;

// --- unit conversion ---
// The Elgato wire protocol speaks mired; the CLI speaks Kelvin.

static s32 round_to_50(s32 n) {
    return (n + 25) / 50 * 50;
}

// Same trick as round_to_50 - add half the divisor before the division
// rounds instead of truncates - just for an arbitrary divisor.
static s32 round_div(s32 numerator, s32 denominator) {
    return (numerator + denominator / 2) / denominator;
}

// mired -> Kelvin for display. Round before bucketing to the nearest 50K,
// don't truncate - mired 315 truncates to 3150K, but the real value is
// 3174.6K, which rounds to 3200K. Truncating alone is off by a bucket.
static s32 mired_to_kelvin(s32 mired) {
    return round_to_50(round_div(1000000, mired));
}

// Kelvin -> mired, sent straight to the light. Round to the nearest mired
// so the light lands as close as possible to what the user asked for.
static s32 kelvin_to_mired(s32 kelvin) {
    return round_div(1000000, kelvin);
}

// --- config parsing ---
// name = ip, one per line. It's our own format, not JSON, so a few lines
// of scanning beats pulling in a parser for it.

static char *trim(char *s) {
    while(isspace((unsigned char)*s)) {
        s++;
    }

    if(*s == '\0') {
        return s;
    }

    char *end = s + strlen(s) - 1;
    while(end > s && isspace((unsigned char)*end)) {
        end--;
    }
    end[1] = '\0';

    return s;
}

static b32 parse_config(char *text, LightConfig *out, s32 max, s32 *count) {
    *count = 0;

    for(char *line = text; line; ) {
        char *newline = strchr(line, '\n');
        if(newline) {
            *newline = '\0';
        }
        char *next = newline ? newline + 1 : NULL;

        char *trimmed = trim(line);
        line = next;
        if(*trimmed == '\0') {
            continue;
        }

        char *eq = strchr(trimmed, '=');
        if(!eq) {
            return false;
        }
        *eq = '\0';

        char *name = trim(trimmed);
        char *ip = trim(eq + 1);

        if(*count >= max) {
            return false;
        }
        if(strlen(name) >= sizeof(out[0].name) || strlen(ip) >= sizeof(out[0].ip)) {
            return false;
        }

        strcpy(out[*count].name, name);
        strcpy(out[*count].ip, ip);
        (*count)++;
    }

    return true;
}

static b32 resolve_config_path(char *buf, u32 cap) {
    const char *home = getenv("HOME");
    if(!home || *home == '\0') {
        return false;
    }

    s32 len = snprintf(buf, cap, "%s/.config/klair", home);
    return len > 0 && (u32)len < cap;
}

static b32 load_config(const char *path, LightConfig *out, s32 max, s32 *count) {
    FILE *f = fopen(path, "rb");
    if(!f) {
        return false;
    }

    char buf[8192];
    s32 n = (s32)fread(buf, 1, sizeof(buf) - 1, f);

    // n hit capacity because the file ran out of buffer, not because it
    // ended - peek one more byte to tell those apart before we go parsing
    // half a line.
    b32 truncated = (n == (s32)sizeof(buf) - 1) && (fgetc(f) != EOF);
    fclose(f);
    if(truncated) {
        return false;
    }

    buf[n] = '\0';

    return parse_config(buf, out, max, count);
}

// --- device response parsing ---
// It's JSON, but a fixed shape that's Elgato's to change, not ours, and
// tiny - so we just pull the three integer fields we need out of the text
// rather than write a parser for it.

static b32 find_int_after_key(const char *json, const char *key, s32 *out) {
    const char *p = strstr(json, key);
    if(!p) {
        return false;
    }

    p = strchr(p + strlen(key), ':');
    if(!p) {
        return false;
    }
    p++;

    while(isspace((unsigned char)*p)) {
        p++;
    }

    char *end;
    long value = strtol(p, &end, 10);
    if(end == p) {
        return false;
    }

    *out = (s32)value;
    return true;
}

static b32 parse_light_status(const char *json, LightDetail *out) {
    memset(out, 0, sizeof(*out));

    if(!find_int_after_key(json, "\"on\"", &out->on)) {
        return false;
    }

    find_int_after_key(json, "\"brightness\"", &out->brightness);
    find_int_after_key(json, "\"temperature\"", &out->temperature);

    return true;
}

// --- request body ---

// Appends to buf[*len] and advances *len; false on truncation, *len left
// alone, so a caller bails instead of driving `cap - len` negative later.
static b32 append_fmt(char *buf, u32 cap, s32 *len, const char *fmt, ...) {
    s32 remaining = (s32)cap - *len;
    if(remaining <= 0) {
        return false;
    }

    va_list ap;
    va_start(ap, fmt);
    s32 n = vsnprintf(buf + *len, (u32)remaining, fmt, ap);
    va_end(ap);

    if(n < 0 || n >= remaining) {
        return false;
    }

    *len += n;
    return true;
}

static s32 build_light_body(char *buf, u32 cap, LightDetail d) {
    s32 len = 0;
    if(!append_fmt(buf, cap, &len, "{\"lights\":[{\"on\":%d", d.on)) {
        return -1;
    }

    if(d.brightness != UNSET && !append_fmt(buf, cap, &len, ",\"brightness\":%d", d.brightness)) {
        return -1;
    }
    if(d.temperature != UNSET && !append_fmt(buf, cap, &len, ",\"temperature\":%d", d.temperature)) {
        return -1;
    }

    if(!append_fmt(buf, cap, &len, "}]}")) {
        return -1;
    }
    return len;
}

// --- retry / backoff ---
// Factored out of the networking code so it's testable without a socket.
// 3 attempts total; on failure wait `backoff_ms`, then double it.

typedef struct {
    s32 attempts_left;
    double backoff_ms;
} RetryState;

static void retry_init(RetryState *r, s32 max_attempts, double initial_backoff_ms) {
    r->attempts_left = max_attempts;
    r->backoff_ms = initial_backoff_ms;
}

static b32 retry_advance(RetryState *r, double *wait_ms) {
    // Never call this once exhausted - a caller checking retry_advance's
    // own return value should never let that happen.
    assert(r->attempts_left > 0);

    r->attempts_left--;
    if(r->attempts_left <= 0) {
        return false;
    }

    *wait_ms = r->backoff_ms;
    r->backoff_ms *= 2;
    return true;
}

// --- HTTP over sockets + poll() event loop ---
//
// A dead light eating its full connect timeout while other lights wait behind
// it is a real failure mode on flaky wifi gear. Fixing it is an I/O-waiting
// problem, not a compute problem: one thread, an array of non-blocking
// sockets, one poll() call. Each light is a small state machine because
// that's what non-blocking I/O actually is.

typedef enum {
    METHOD_GET,
    METHOD_PUT,
} Method;

typedef enum {
    JOB_RETRY_WAIT,
    JOB_CONNECTING,
    JOB_WRITING,
    JOB_READING,
    JOB_DONE,
    JOB_FAILED,
} JobState;

typedef enum {
    ERR_NONE,
    ERR_TIMEOUT,
    ERR_CLOSED,
    ERR_CONNECT_FAILED,
    ERR_OTHER,
} ErrorKind;

typedef struct {
    const LightConfig *cfg;
    Method method;

    char host[128];
    char port[16];

    char request[512];
    s32 request_len;
    s32 request_sent;

    char response[8192];
    s32 response_len;

    int fd;
    JobState state;

    RetryState retry;
    double wake_at_ms;
    double deadline_ms;

    b32 ok;
    LightDetail result;
    ErrorKind err_kind;
    char err[128];
} Job;

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static b32 split_host_port(const char *ip, char *host, u32 host_cap, char *port, u32 port_cap) {
    const char *colon = strrchr(ip, ':');
    if(!colon) {
        return false;
    }

    u32 host_len = (u32)(colon - ip);
    if(host_len >= host_cap) {
        return false;
    }
    memcpy(host, ip, host_len);
    host[host_len] = '\0';

    u32 port_len = (u32)strlen(colon + 1);
    if(port_len >= port_cap) {
        return false;
    }
    strcpy(port, colon + 1);

    return true;
}

static b32 build_request(Job *job, LightDetail *settings) {
    if(job->method == METHOD_GET) {
        job->request_len = snprintf(job->request, sizeof(job->request),
            "GET /elgato/lights HTTP/1.1\r\n"
            "Host: %s\r\n"
            "Accept: application/json\r\n"
            "Connection: close\r\n"
            "\r\n",
            job->host);
    } else {
        char body[256];
        s32 body_len = build_light_body(body, sizeof(body), *settings);
        if(body_len < 0) {
            return false;
        }

        job->request_len = snprintf(job->request, sizeof(job->request),
            "PUT /elgato/lights HTTP/1.1\r\n"
            "Host: %s\r\n"
            "Content-Type: application/json\r\n"
            "Accept: application/json\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "\r\n"
            "%s",
            job->host, body_len, body);
    }

    return job->request_len > 0 && (u32)job->request_len < sizeof(job->request);
}

static void fail_or_retry(Job *job) {
    double wait_ms;
    if(retry_advance(&job->retry, &wait_ms)) {
        job->state = JOB_RETRY_WAIT;
        job->wake_at_ms = now_ms() + wait_ms;
    } else {
        job->state = JOB_FAILED;
    }
}

static void job_init(Job *job, const LightConfig *cfg, Method method, LightDetail *settings) {
    memset(job, 0, sizeof(*job));
    job->cfg = cfg;
    job->method = method;
    job->fd = -1;
    job->err_kind = ERR_NONE;
    retry_init(&job->retry, 3, 100.0);

    if(!split_host_port(cfg->ip, job->host, sizeof(job->host), job->port, sizeof(job->port))) {
        job->err_kind = ERR_OTHER;
        snprintf(job->err, sizeof(job->err), "invalid address: %s", cfg->ip);
        job->state = JOB_FAILED;
        return;
    }

    if(!build_request(job, settings)) {
        job->err_kind = ERR_OTHER;
        snprintf(job->err, sizeof(job->err), "request too large");
        job->state = JOB_FAILED;
        return;
    }

    // Seed the job as an already-elapsed retry wait: the very first attempt
    // and every subsequent retry go through the exact same code path.
    job->state = JOB_RETRY_WAIT;
    job->wake_at_ms = now_ms();
}

static b32 start_connect(Job *job) {
    // Config entries are always a literal ip:port (see README), never a
    // hostname, so AI_NUMERICHOST/AI_NUMERICSERV keeps this a plain parse.
    // Without it, a hostname would trigger a real DNS lookup here, which
    // blocks - and stalls every other light waiting behind it in the loop.
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;

    struct addrinfo *result;
    int rc = getaddrinfo(job->host, job->port, &hints, &result);
    if(rc != 0) {
        job->err_kind = ERR_CONNECT_FAILED;
        snprintf(job->err, sizeof(job->err), "resolve failed: %s", gai_strerror(rc));
        return false;
    }

    int fd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if(fd < 0) {
        freeaddrinfo(result);
        job->err_kind = ERR_OTHER;
        snprintf(job->err, sizeof(job->err), "socket: %s", strerror(errno));
        return false;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    int rc2 = connect(fd, result->ai_addr, result->ai_addrlen);
    freeaddrinfo(result);

    if(rc2 < 0 && errno != EINPROGRESS) {
        close(fd);
        job->err_kind = ERR_CONNECT_FAILED;
        snprintf(job->err, sizeof(job->err), "connect: %s", strerror(errno));
        return false;
    }

    job->fd = fd;
    job->state = JOB_CONNECTING;
    job->request_sent = 0;
    job->response_len = 0;
    job->deadline_ms = now_ms() + 3000.0;
    return true;
}

static const char *find_header(const char *headers, const char *header_end, const char *name) {
    u32 name_len = (u32)strlen(name);
    for(const char *p = headers; p + name_len <= header_end; p++) {
        if(strncasecmp(p, name, name_len) == 0) {
            return p + name_len;
        }
    }
    return NULL;
}

// Have we received a complete HTTP response? Uses Content-Length when present;
// otherwise the caller falls back to "connection closed = response complete".
static b32 response_complete(Job *job, s32 *out_status, char **out_body) {
    char *header_end = strstr(job->response, "\r\n\r\n");
    if(!header_end) {
        return false;
    }

    s32 status = 0;
    if(sscanf(job->response, "HTTP/%*d.%*d %d", &status) != 1) {
        return false;
    }

    char *body = header_end + 4;
    s32 header_len = (s32)(body - job->response);
    s32 body_len = job->response_len - header_len;

    const char *cl = find_header(job->response, header_end, "Content-Length:");
    if(cl) {
        s32 content_length = (s32)strtol(cl, NULL, 10);
        if(body_len < content_length) {
            return false;
        }
    }

    *out_status = status;
    *out_body = body;
    return true;
}

static void finish_with_status(Job *job, s32 status, char *body) {
    // Only a PUT enforces a 200 status; a GET parses whatever body comes
    // back regardless of status code.
    if(job->method == METHOD_PUT && status != 200) {
        job->err_kind = ERR_OTHER;
        snprintf(job->err, sizeof(job->err), "unexpected status %d: %s", status, body);
        fail_or_retry(job);
        return;
    }

    if(!parse_light_status(body, &job->result)) {
        job->err_kind = ERR_OTHER;
        snprintf(job->err, sizeof(job->err), "parsing response failed");
        fail_or_retry(job);
        return;
    }

    job->ok = true;
    job->state = JOB_DONE;
}

static void service_job(Job *job) {
    // Only ever called from run_jobs for a job it just put an active fd
    // into the poll() set for - CONNECTING, WRITING, or READING.
    assert(job->fd >= 0);

    if(job->state == JOB_CONNECTING) {
        int err = 0;
        socklen_t len = sizeof(err);
        if(getsockopt(job->fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0) {
            err = errno;
        }

        if(err != 0) {
            close(job->fd);
            job->fd = -1;
            job->err_kind = ERR_CONNECT_FAILED;
            snprintf(job->err, sizeof(job->err), "connect: %s", strerror(err));
            fail_or_retry(job);
            return;
        }

        job->state = JOB_WRITING;
        return;
    }

    if(job->state == JOB_WRITING) {
        ssize_t n = write(job->fd, job->request + job->request_sent,
            (u32)(job->request_len - job->request_sent));

        if(n < 0) {
            if(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                return;
            }
            close(job->fd);
            job->fd = -1;
            job->err_kind = ERR_OTHER;
            snprintf(job->err, sizeof(job->err), "write: %s", strerror(errno));
            fail_or_retry(job);
            return;
        }

        job->request_sent += (s32)n;
        if(job->request_sent >= job->request_len) {
            job->state = JOB_READING;
        }
        return;
    }

    if(job->state == JOB_READING) {
        if(job->response_len >= (s32)sizeof(job->response) - 1) {
            close(job->fd);
            job->fd = -1;
            job->err_kind = ERR_OTHER;
            snprintf(job->err, sizeof(job->err), "response too large");
            fail_or_retry(job);
            return;
        }

        ssize_t n = read(job->fd, job->response + job->response_len,
            sizeof(job->response) - 1 - (u32)job->response_len);

        if(n < 0) {
            if(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                return;
            }
            close(job->fd);
            job->fd = -1;
            job->err_kind = ERR_OTHER;
            snprintf(job->err, sizeof(job->err), "read: %s", strerror(errno));
            fail_or_retry(job);
            return;
        }

        if(n == 0) {
            close(job->fd);
            job->fd = -1;
            job->response[job->response_len] = '\0';

            s32 status;
            char *body;
            if(response_complete(job, &status, &body)) {
                finish_with_status(job, status, body);
            } else {
                job->err_kind = ERR_CLOSED;
                snprintf(job->err, sizeof(job->err), "connection closed before response completed");
                fail_or_retry(job);
            }
            return;
        }

        job->response_len += (s32)n;
        job->response[job->response_len] = '\0';

        s32 status;
        char *body;
        if(response_complete(job, &status, &body)) {
            close(job->fd);
            job->fd = -1;
            finish_with_status(job, status, body);
        }
        return;
    }
}

static void run_jobs(Job *jobs, s32 n) {
    for(;;) {
        struct pollfd fds[MAX_LIGHTS];
        s32 fd_job_index[MAX_LIGHTS];
        s32 fd_count = 0;

        b32 all_done = true;
        double next_wake = -1;
        double t = now_ms();

        for(s32 i = 0; i < n; i++) {
            Job *job = &jobs[i];
            if(job->state == JOB_DONE || job->state == JOB_FAILED) {
                continue;
            }
            all_done = false;

            if(job->state == JOB_RETRY_WAIT) {
                if(next_wake < 0 || job->wake_at_ms < next_wake) {
                    next_wake = job->wake_at_ms;
                }
                continue;
            }

            fds[fd_count].fd = job->fd;
            fds[fd_count].events = (job->state == JOB_READING) ? POLLIN : POLLOUT;
            fds[fd_count].revents = 0;
            fd_job_index[fd_count] = i;
            fd_count++;

            if(next_wake < 0 || job->deadline_ms < next_wake) {
                next_wake = job->deadline_ms;
            }
        }

        if(all_done) {
            break;
        }

        double timeout_ms = next_wake < 0 ? 3000.0 : next_wake - t;
        if(timeout_ms < 0) {
            timeout_ms = 0;
        }

        int nready = poll(fds, (nfds_t)fd_count, (int)timeout_ms);
        if(nready < 0 && errno == EINTR) {
            continue;
        }

        for(s32 k = 0; k < fd_count; k++) {
            if(fds[k].revents != 0) {
                service_job(&jobs[fd_job_index[k]]);
            }
        }

        t = now_ms();
        for(s32 i = 0; i < n; i++) {
            Job *job = &jobs[i];
            if(job->state == JOB_DONE || job->state == JOB_FAILED) {
                continue;
            }

            if(job->state == JOB_RETRY_WAIT) {
                if(t >= job->wake_at_ms && !start_connect(job)) {
                    fail_or_retry(job);
                }
                continue;
            }

            if(t >= job->deadline_ms) {
                close(job->fd);
                job->fd = -1;
                job->err_kind = ERR_TIMEOUT;
                snprintf(job->err, sizeof(job->err), "timed out");
                fail_or_retry(job);
            }
        }
    }
}

typedef struct {
    b32 ok;
    LightDetail status;
    ErrorKind err_kind;
    char err[128];
} LightResult;

static void run_lights(const LightConfig *targets, s32 count, Method method, LightDetail *settings, LightResult *results) {
    // count always comes from resolve_lights, which already asserts this
    // same bound on its own out[] before returning it here.
    Job jobs[MAX_LIGHTS];
    for(s32 i = 0; i < count; i++) {
        job_init(&jobs[i], &targets[i], method, settings);
    }

    run_jobs(jobs, count);

    for(s32 i = 0; i < count; i++) {
        results[i].ok = jobs[i].ok;
        results[i].status = jobs[i].result;
        results[i].err_kind = jobs[i].err_kind;
        strcpy(results[i].err, jobs[i].err);
    }
}

static const char *classify_error(ErrorKind kind, const char *raw) {
    switch(kind) {
        case ERR_TIMEOUT: return "timeout while connecting";
        case ERR_CLOSED: return "connection closed unexpectedly";
        case ERR_CONNECT_FAILED: return "failed to connect";
        default: return raw;
    }
}

// --- validation ---

static b32 validate_brightness(s32 n) {
    return n >= 3 && n <= 100;
}

static b32 validate_temperature(s32 n) {
    return n >= 2900 && n <= 7000;
}

// --- light resolution ---

static s32 resolve_lights(const LightConfig *lights, s32 count, const char *name, LightConfig *out) {
    // Every call site passes a MAX_LIGHTS-sized out[]; count must never
    // exceed it or the "no filter" branch below writes past the end.
    assert(count <= MAX_LIGHTS);

    if(!name || *name == '\0') {
        for(s32 i = 0; i < count; i++) {
            out[i] = lights[i];
        }
        return count;
    }

    for(s32 i = 0; i < count; i++) {
        if(strcmp(lights[i].name, name) == 0) {
            out[0] = lights[i];
            return 1;
        }
    }

    fprintf(stderr, "light \"%s\" not found; available: ", name);
    for(s32 i = 0; i < count; i++) {
        fprintf(stderr, "%s%s", i ? ", " : "", lights[i].name);
    }
    fprintf(stderr, "\n");
    return 0;
}

// --- output ---

static const char *format_on_off(s32 on) {
    return on == 1 ? "ON" : "OFF";
}

static void print_light_result(const char *name, const char *verb, LightResult *result) {
    if(!result->ok) {
        printf("%s of light \"%s\": error: %s\n", verb, name, classify_error(result->err_kind, result->err));
        return;
    }

    printf("Status of light \"%s\":\n", name);
    printf("  Power:       %s\n", format_on_off(result->status.on));
    printf("  Brightness:  %d%%\n", result->status.brightness);
    printf("  Temperature: %dK\n", mired_to_kelvin(result->status.temperature));
}

static void print_usage(FILE *out) {
    fprintf(out,
        "Usage: klair [-h] [-v] <command> [flags]\n"
        "\n"
        "Commands:\n"
        "  status  [-l NAME]\n"
        "  on      [-b 3-100] [-t 2900-7000] [-l NAME]\n"
        "  off     [-l NAME]\n"
        "\n"
        "No command: same as status.\n");
}

// --- CLI parsing + commands ---

// Rejects empty strings, trailing junk ("12x"), and out-of-range values -
// a bare strtol call would let all of those through. -1 is a legit value
// here too, so whether a flag was given at all lives in CliArgs's
// has_brightness/has_temperature, not in the value itself.
static b32 parse_int_arg(const char *s, s32 *out) {
    if(*s == '\0') {
        return false;
    }

    errno = 0;
    char *end;
    long value = strtol(s, &end, 10);
    if(*end != '\0' || errno == ERANGE || value < INT32_MIN || value > INT32_MAX) {
        return false;
    }

    *out = (s32)value;
    return true;
}

typedef struct {
    b32 accept_bt;

    b32 has_brightness;
    s32 brightness;

    b32 has_temperature;
    s32 temperature;

    char light_name[64];
} CliArgs;

static b32 parse_common_flags(char **args, s32 count, CliArgs *out) {
    for(s32 i = 0; i < count; i++) {
        if(out->accept_bt && strcmp(args[i], "-b") == 0) {
            if(i + 1 >= count || !parse_int_arg(args[++i], &out->brightness)) {
                return false;
            }
            out->has_brightness = true;
        } else if(out->accept_bt && strcmp(args[i], "-t") == 0) {
            if(i + 1 >= count || !parse_int_arg(args[++i], &out->temperature)) {
                return false;
            }
            out->has_temperature = true;
        } else if(strcmp(args[i], "-l") == 0) {
            if(i + 1 >= count) {
                return false;
            }
            strncpy(out->light_name, args[++i], sizeof(out->light_name) - 1);
            out->light_name[sizeof(out->light_name) - 1] = '\0';
        } else {
            return false;
        }
    }
    return true;
}

// Shared by every command: resolve the targeted lights, run the request
// against each in parallel, and print each one's result.
static void run_command(const LightConfig *lights, s32 light_count, const char *light_name,
        Method method, LightDetail *settings, const char *verb) {
    LightConfig targets[MAX_LIGHTS];
    s32 target_count = resolve_lights(lights, light_count, light_name, targets);
    if(target_count == 0) {
        return;
    }

    LightResult results[MAX_LIGHTS];
    run_lights(targets, target_count, method, settings, results);

    for(s32 i = 0; i < target_count; i++) {
        print_light_result(targets[i].name, verb, &results[i]);
    }
}

static void cmd_status(const LightConfig *lights, s32 light_count, const CliArgs *a) {
    run_command(lights, light_count, a->light_name, METHOD_GET, NULL, "Status");
}

static void cmd_on(const LightConfig *lights, s32 light_count, const CliArgs *a) {
    LightDetail settings = {.on = 1, .brightness = UNSET, .temperature = UNSET};

    if(a->has_brightness) {
        if(!validate_brightness(a->brightness)) {
            fprintf(stderr, "invalid brightness: must be between 3 and 100\n");
            return;
        }
        settings.brightness = a->brightness;
    }

    if(a->has_temperature) {
        if(!validate_temperature(a->temperature)) {
            fprintf(stderr, "invalid temperature: must be between 2900K and 7000K\n");
            return;
        }
        settings.temperature = kelvin_to_mired(a->temperature);
    }

    run_command(lights, light_count, a->light_name, METHOD_PUT, &settings, "Update");
}

static void cmd_off(const LightConfig *lights, s32 light_count, const CliArgs *a) {
    LightDetail settings = {.on = 0, .brightness = UNSET, .temperature = UNSET};

    run_command(lights, light_count, a->light_name, METHOD_PUT, &settings, "Update");
}

typedef enum {
    CMD_STATUS,
    CMD_ON,
    CMD_OFF,
} Command;

static b32 parse_command(const char *s, Command *out) {
    if(strcmp(s, "status") == 0) {
        *out = CMD_STATUS;
        return true;
    }
    if(strcmp(s, "on") == 0) {
        *out = CMD_ON;
        return true;
    }
    if(strcmp(s, "off") == 0) {
        *out = CMD_OFF;
        return true;
    }
    return false;
}

int main(int argc, char **argv) {
    if(argc >= 2 && strcmp(argv[1], "-h") == 0) {
        print_usage(stdout);
        return 0;
    }

    if(argc >= 2 && strcmp(argv[1], "-v") == 0) {
        printf("%s\n", KLAIR_VERSION);
        return 0;
    }

    char config_path[512];
    if(!resolve_config_path(config_path, sizeof(config_path))) {
        fprintf(stderr, "cannot determine home directory\n");
        return 1;
    }

    LightConfig lights[MAX_LIGHTS];
    s32 light_count = 0;
    if(!load_config(config_path, lights, MAX_LIGHTS, &light_count)) {
        fprintf(stderr, "error loading config: %s\n", config_path);
        return 1;
    }

    const char *command_name = "status";
    char **cmd_args = NULL;
    s32 cmd_arg_count = 0;
    if(argc >= 2) {
        command_name = argv[1];
        cmd_args = argv + 2;
        cmd_arg_count = argc - 2;
    }

    Command command;
    if(!parse_command(command_name, &command)) {
        fprintf(stderr, "unknown command: %s\n\n", command_name);
        print_usage(stderr);
        return 1;
    }

    CliArgs a = {.accept_bt = command == CMD_ON};
    if(!parse_common_flags(cmd_args, cmd_arg_count, &a)) {
        print_usage(stderr);
        return 1;
    }

    switch(command) {
    case CMD_STATUS:
        cmd_status(lights, light_count, &a);
        break;
    case CMD_ON:
        cmd_on(lights, light_count, &a);
        break;
    case CMD_OFF:
        cmd_off(lights, light_count, &a);
        break;
    }

    return 0;
}
