#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#if !defined(BUILD_DEBUG)
# define BUILD_DEBUG 1
#endif

#define internal static
#define global static
#define local_persist static

typedef uint8_t u8;
typedef uint32_t u32;
typedef int32_t i32;

#define BUILD_NAME "klair"
#define BUILD_VERSION "0.4.0"
#define BUILD_LANG "c-legacy"

#if defined(BUILD_GIT_HASH)
# define BUILD_GIT_HASH_APPEND "+" BUILD_GIT_HASH
#else
# define BUILD_GIT_HASH_APPEND ""
#endif

#if BUILD_DEBUG
# define BUILD_DEBUG_APPEND ", debug"
#else
# define BUILD_DEBUG_APPEND ""
#endif

// klair version 0.4.0+8b30b7a-dirty (c-legacy, debug)
#define BUILD_VERSION_FULL \
    BUILD_NAME " version " BUILD_VERSION BUILD_GIT_HASH_APPEND " (" BUILD_LANG BUILD_DEBUG_APPEND ")"

constexpr i32 MAX_LIGHTS = 16;
constexpr i32 UNSET = -1;
#define ArrayCount(a) (sizeof(a) / sizeof((a)[0]))

typedef struct {
    i32 on;
    i32 brightness;
    i32 temperature;
} LightDetail;

typedef struct {
    char name[64];
    char ip[64];
} LightConfig;

// --- unit conversion ---
// The Elgato wire protocol speaks mired; the CLI speaks Kelvin.

internal i32 round_to_50(i32 n) {
    return (n + 25) / 50 * 50;
}

// Same trick as round_to_50 - add half the divisor before the division
// rounds instead of truncates - just for an arbitrary divisor.
internal i32 round_div(i32 numerator, i32 denominator) {
    return (numerator + denominator / 2) / denominator;
}

// mired -> Kelvin for display. Round before bucketing to the nearest 50K,
// don't truncate - mired 315 truncates to 3150K, but the real value is
// 3174.6K, which rounds to 3200K. Truncating alone is off by a bucket.
internal i32 mired_to_kelvin(i32 mired) {
    assert(mired > 0);
    return round_to_50(round_div(1'000'000, mired));
}

// Kelvin -> mired, sent straight to the light. Round to the nearest mired
// so the light lands as close as possible to what the user asked for.
internal i32 kelvin_to_mired(i32 kelvin) {
    return round_div(1'000'000, kelvin);
}

// --- config parsing ---
// name = ip, one per line. It's our own format, not JSON, so a few lines
// of scanning beats pulling in a parser for it.

internal char *trim(char *s) {
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

internal bool parse_config(char *text, LightConfig *out, i32 max, i32 *count) {
    *count = 0;

    for(char *line = text; line; ) {
        char *newline = strchr(line, '\n');
        if(newline) {
            *newline = '\0';
        }
        char *next = newline ? newline + 1 : nullptr;

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

internal bool resolve_config_path(char *buf, u32 cap) {
    const char *home = getenv("HOME");
    if(!home || *home == '\0') {
        return false;
    }

    i32 len = snprintf(buf, cap, "%s/.config/klair", home);
    return len > 0 && (u32)len < cap;
}

internal bool load_config(const char *path, LightConfig *out, i32 max, i32 *count) {
    FILE *f = fopen(path, "rb");
    if(!f) {
        return false;
    }

    char buf[8192];
    i32 n = (i32)fread(buf, 1, sizeof(buf) - 1, f);

    // n hit capacity because the file ran out of buffer, not because it
    // ended - peek one more byte to tell those apart before we go parsing
    // half a line.
    bool truncated = (n == (i32)sizeof(buf) - 1) && (fgetc(f) != EOF);
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

internal bool find_int_after_key(const char *json, const char *key, i32 *out) {
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

    *out = (i32)value;
    return true;
}

internal bool parse_light_status(const char *json, LightDetail *out) {
    *out = (LightDetail){};

    if(!find_int_after_key(json, "\"on\"", &out->on)) {
        return false;
    }

    if(!find_int_after_key(json, "\"brightness\"", &out->brightness)) {
        return false;
    }

    if(!find_int_after_key(json, "\"temperature\"", &out->temperature)) {
        return false;
    }

    return true;
}

// --- request body ---

// Appends to buf[*len] and advances *len; false on truncation, *len left
// alone, so a caller bails instead of driving `cap - len` negative later.
internal bool append_fmt(char *buf, u32 cap, i32 *len, const char *fmt, ...) {
    i32 remaining = (i32)cap - *len;
    if(remaining <= 0) {
        return false;
    }

    va_list ap;
    va_start(ap, fmt);
    i32 n = vsnprintf(buf + *len, (u32)remaining, fmt, ap);
    va_end(ap);

    if(n < 0 || n >= remaining) {
        return false;
    }

    *len += n;
    return true;
}

internal i32 build_light_body(char *buf, u32 cap, LightDetail d) {
    i32 len = 0;
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
    i32 attempts_left;
    double backoff_ms;
} RetryState;

internal void retry_init(RetryState *r, i32 max_attempts, double initial_backoff_ms) {
    r->attempts_left = max_attempts;
    r->backoff_ms = initial_backoff_ms;
}

internal bool retry_advance(RetryState *r, double *wait_ms) {
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
    i32 request_len;
    i32 request_sent;

    char response[8192];
    i32 response_len;

    int fd;
    JobState state;

    RetryState retry;
    double wake_at_ms;
    double deadline_ms;

    bool ok;
    LightDetail result;
    ErrorKind err_kind;
    char err[128];
} Job;

internal double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

internal bool split_host_port(const char *ip, char *host, u32 host_cap, char *port, u32 port_cap) {
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

internal bool build_request(Job *job, LightDetail *settings) {
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
        i32 body_len = build_light_body(body, sizeof(body), *settings);
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

internal void fail_or_retry(Job *job) {
    double wait_ms;
    if(retry_advance(&job->retry, &wait_ms)) {
        job->state = JOB_RETRY_WAIT;
        job->wake_at_ms = now_ms() + wait_ms;
    } else {
        job->state = JOB_FAILED;
    }
}

internal void job_init(Job *job, const LightConfig *cfg, Method method, LightDetail *settings) {
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

internal bool start_connect(Job *job) {
    // Config entries are always a literal ip:port (see README), never a
    // hostname, so AI_NUMERICHOST/AI_NUMERICSERV keeps this a plain parse.
    // Without it, a hostname would trigger a real DNS lookup here, which
    // blocks - and stalls every other light waiting behind it in the loop.
    struct addrinfo hints = {
        .ai_family = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM,
        .ai_flags = AI_NUMERICHOST | AI_NUMERICSERV,
    };

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

internal const char *find_header(const char *headers, const char *header_end, const char *name) {
    u32 name_len = (u32)strlen(name);
    for(const char *p = headers; p + name_len <= header_end; p++) {
        if(strncasecmp(p, name, name_len) == 0) {
            return p + name_len;
        }
    }
    return nullptr;
}

// Have we received a complete HTTP response? Uses Content-Length when present;
// otherwise the caller falls back to "connection closed = response complete".
internal bool response_complete(Job *job, i32 *out_status, char **out_body) {
    char *header_end = strstr(job->response, "\r\n\r\n");
    if(!header_end) {
        return false;
    }

    i32 status = 0;
    if(sscanf(job->response, "HTTP/%*d.%*d %d", &status) != 1) {
        return false;
    }

    char *body = header_end + 4;
    i32 header_len = (i32)(body - job->response);
    i32 body_len = job->response_len - header_len;

    const char *cl = find_header(job->response, header_end, "Content-Length:");
    if(cl) {
        i32 content_length = (i32)strtol(cl, nullptr, 10);
        if(body_len < content_length) {
            return false;
        }
    }

    *out_status = status;
    *out_body = body;
    return true;
}

// Device-reported mired. The only thing we need from it is that it's safe
// to divide by.
internal bool validate_mired(i32 n) {
    return n > 0;
}

internal void finish_with_status(Job *job, i32 status, char *body) {
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

    if(!validate_mired(job->result.temperature)) {
        job->err_kind = ERR_OTHER;
        snprintf(job->err, sizeof(job->err), "invalid temperature in response");
        fail_or_retry(job);
        return;
    }

    job->ok = true;
    job->state = JOB_DONE;
}

internal void service_job(Job *job) {
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

        job->request_sent += (i32)n;
        if(job->request_sent >= job->request_len) {
            job->state = JOB_READING;
        }
        return;
    }

    if(job->state == JOB_READING) {
        if(job->response_len >= (i32)sizeof(job->response) - 1) {
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

            i32 status;
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

        job->response_len += (i32)n;
        job->response[job->response_len] = '\0';

        i32 status;
        char *body;
        if(response_complete(job, &status, &body)) {
            close(job->fd);
            job->fd = -1;
            finish_with_status(job, status, body);
        }
        return;
    }
}

internal void run_jobs(Job *jobs, i32 n) {
    for(;;) {
        struct pollfd fds[MAX_LIGHTS];
        i32 fd_job_index[MAX_LIGHTS];
        i32 fd_count = 0;

        bool all_done = true;
        double next_wake = -1;
        double t = now_ms();

        for(i32 i = 0; i < n; i++) {
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

        // +1 so a fractional wait rounds up instead of becoming a 0ms spin.
        int nready = poll(fds, (nfds_t)fd_count, (int)(timeout_ms + 1.0));
        if(nready < 0 && errno == EINTR) {
            continue;
        }

        for(i32 k = 0; k < fd_count; k++) {
            if(fds[k].revents != 0) {
                service_job(&jobs[fd_job_index[k]]);
            }
        }

        t = now_ms();
        for(i32 i = 0; i < n; i++) {
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
    bool ok;
    LightDetail status;
    ErrorKind err_kind;
    char err[128];
} LightResult;

internal void run_lights(const LightConfig *targets, i32 count, Method method, LightDetail *settings, LightResult *results) {
    // count always comes from resolve_lights, which already asserts this
    // same bound on its own out[] before returning it here.
    Job jobs[MAX_LIGHTS];
    for(i32 i = 0; i < count; i++) {
        job_init(&jobs[i], &targets[i], method, settings);
    }

    run_jobs(jobs, count);

    for(i32 i = 0; i < count; i++) {
        results[i].ok = jobs[i].ok;
        results[i].status = jobs[i].result;
        results[i].err_kind = jobs[i].err_kind;
        strcpy(results[i].err, jobs[i].err);
    }
}

internal const char *classify_error(ErrorKind kind, const char *raw) {
    switch(kind) {
        case ERR_TIMEOUT: return "timeout while connecting";
        case ERR_CLOSED: return "connection closed unexpectedly";
        case ERR_CONNECT_FAILED: return "failed to connect";
        default: return raw;
    }
}

// --- validation ---

internal bool validate_brightness(i32 n) {
    return n >= 3 && n <= 100;
}

internal bool validate_temperature(i32 n) {
    return n >= 2900 && n <= 7000;
}

// --- light resolution ---

internal i32 resolve_lights(const LightConfig *lights, i32 count, const char *name, LightConfig *out) {
    // Every call site passes a MAX_LIGHTS-sized out[]; count must never
    // exceed it or the "no filter" branch below writes past the end.
    assert(count <= MAX_LIGHTS);

    if(!name || *name == '\0') {
        for(i32 i = 0; i < count; i++) {
            out[i] = lights[i];
        }
        return count;
    }

    for(i32 i = 0; i < count; i++) {
        if(strcmp(lights[i].name, name) == 0) {
            out[0] = lights[i];
            return 1;
        }
    }

    fprintf(stderr, "light \"%s\" not found; available: ", name);
    for(i32 i = 0; i < count; i++) {
        fprintf(stderr, "%s%s", i ? ", " : "", lights[i].name);
    }
    fprintf(stderr, "\n");
    return 0;
}

// --- output ---

internal const char *format_on_off(i32 on) {
    return on == 1 ? "ON" : "OFF";
}

internal void print_light_result(const char *name, const char *verb, LightResult *result) {
    if(!result->ok) {
        printf("%s of light \"%s\": error: %s\n", verb, name, classify_error(result->err_kind, result->err));
        return;
    }

    printf("Status of light \"%s\":\n", name);
    printf("  Power:       %s\n", format_on_off(result->status.on));
    printf("  Brightness:  %d%%\n", result->status.brightness);
    printf("  Temperature: %dK\n", mired_to_kelvin(result->status.temperature));
}

internal void print_usage(FILE *out) {
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
internal bool parse_int_arg(const char *s, i32 *out) {
    if(*s == '\0') {
        return false;
    }

    errno = 0;
    char *end;
    long value = strtol(s, &end, 10);
    if(*end != '\0' || errno == ERANGE || value < INT32_MIN || value > INT32_MAX) {
        return false;
    }

    *out = (i32)value;
    return true;
}

typedef struct {
    bool accept_bt;

    bool has_brightness;
    i32 brightness;

    bool has_temperature;
    i32 temperature;

    char light_name[64];
} CliArgs;

internal bool parse_common_flags(char **args, i32 count, CliArgs *out) {
    for(i32 i = 0; i < count; i++) {
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
internal bool run_command(const LightConfig *lights, i32 light_count, const char *light_name,
        Method method, LightDetail *settings, const char *verb) {
    LightConfig targets[MAX_LIGHTS];
    i32 target_count = resolve_lights(lights, light_count, light_name, targets);
    if(target_count == 0) {
        return false;
    }

    LightResult results[MAX_LIGHTS];
    run_lights(targets, target_count, method, settings, results);

    bool all_ok = true;
    for(i32 i = 0; i < target_count; i++) {
        print_light_result(targets[i].name, verb, &results[i]);
        all_ok = all_ok && results[i].ok;
    }

    return all_ok;
}

internal bool cmd_status(const LightConfig *lights, i32 light_count, const CliArgs *a) {
    return run_command(lights, light_count, a->light_name, METHOD_GET, nullptr, "Status");
}

internal bool cmd_on(const LightConfig *lights, i32 light_count, const CliArgs *a) {
    LightDetail settings = {.on = 1, .brightness = UNSET, .temperature = UNSET};

    if(a->has_brightness) {
        if(!validate_brightness(a->brightness)) {
            fprintf(stderr, "invalid brightness: must be between 3 and 100\n");
            return false;
        }
        settings.brightness = a->brightness;
    }

    if(a->has_temperature) {
        if(!validate_temperature(a->temperature)) {
            fprintf(stderr, "invalid temperature: must be between 2900K and 7000K\n");
            return false;
        }
        settings.temperature = kelvin_to_mired(a->temperature);
    }

    return run_command(lights, light_count, a->light_name, METHOD_PUT, &settings, "Update");
}

internal bool cmd_off(const LightConfig *lights, i32 light_count, const CliArgs *a) {
    LightDetail settings = {.on = 0, .brightness = UNSET, .temperature = UNSET};

    return run_command(lights, light_count, a->light_name, METHOD_PUT, &settings, "Update");
}

typedef enum {
    CMD_STATUS,
    CMD_ON,
    CMD_OFF,
} Command;

internal bool parse_command(const char *s, Command *out) {
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
    // A light dropping the connection mid-write would otherwise kill us
    // with SIGPIPE before write()'s EPIPE ever reaches our error path.
    signal(SIGPIPE, SIG_IGN);

    if(argc >= 2 && strcmp(argv[1], "-h") == 0) {
        print_usage(stdout);
        return 0;
    }

    if(argc >= 2 && strcmp(argv[1], "-v") == 0) {
        printf("%s\n", BUILD_VERSION_FULL);
        return 0;
    }

    char config_path[512];
    if(!resolve_config_path(config_path, sizeof(config_path))) {
        fprintf(stderr, "cannot determine home directory\n");
        return 1;
    }

    LightConfig lights[MAX_LIGHTS];
    i32 light_count = 0;
    if(!load_config(config_path, lights, MAX_LIGHTS, &light_count)) {
        fprintf(stderr, "error loading config: %s\n", config_path);
        return 1;
    }

    const char *command_name = "status";
    char **cmd_args = nullptr;
    i32 cmd_arg_count = 0;
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

    bool ok = false;
    switch(command) {
    case CMD_STATUS:
        ok = cmd_status(lights, light_count, &a);
        break;
    case CMD_ON:
        ok = cmd_on(lights, light_count, &a);
        break;
    case CMD_OFF:
        ok = cmd_off(lights, light_count, &a);
        break;
    }

    return ok ? 0 : 1;
}
