# klair CLI spec

What every klair implementation (`c/`, `odin/`, `go/`) does. If two implementations differ on anything this spec defines, that's a bug in at least one of them. The [conformance suite](../conformance) checks the implementations against this spec: `./check.sh`.

**Strict about gaps, loose about additions.** Everything the spec defines must work exactly as written. An implementation may accept more than the spec requires when that comes from its standard library, like a `+4` for `-t` or `127.1` as an address, as long as the extra input means what the user typed. Where the spec says an input _must_ be rejected, it's because accepting it would do the wrong thing.

`c-legacy/` predates the spec and doesn't follow it. [Changes from c-legacy](#changes-from-c-legacy) lists the differences.

Conventions:

- `<x>` is a placeholder. Everything else in a code block is literal.
- "Prints to stdout/stderr" means one line ending in `\n`, on every OS.
- "Usage error" means: print `<message>`, an empty line and the [usage text](#usage-text) to stderr, then exit 2.

## Order of operations

1. Debug builds print `[debug build]` to stderr, before anything else.
2. Parse the arguments. `help` and `version` are handled here, and any usage error exits here, before the config is touched.
3. Find, read and parse the config.
4. Pick the target lights (`-l`).
5. Send one request to every target light at the same time.
6. Wait for every light to succeed or run out of attempts.
7. Print one result per light, in config order, then exit.

Nothing is printed to stdout before step 7, apart from `help` and `version`.

## Command line

```
klair [command] [flags]
```

### Commands

| Command   | Flags            | Does                                                          |
| --------- | ---------------- | ------------------------------------------------------------- |
| `status`  | `-l`             | read each light's state                                       |
| `on`      | `-l`, `-b`, `-t` | turn on, optionally set brightness and temperature            |
| `off`     | `-l`             | turn off                                                      |
| `help`    | none             | print the [usage text](#usage-text) to stdout, exit 0         |
| `version` | none             | print the [version string](#version-string) to stdout, exit 0 |

`help` and `version` don't read the config, so they work without one.

There are no `-h`, `--help`, `-v` or `--version` flags. `klair -h` is `klair status -h`, an unknown flag, and the usage error prints the usage text anyway. `-v` would usually mean verbose.

- The command is the first argument, if that argument doesn't start with `-`.
- **No command means `status`.** That covers `klair` with no arguments and also a first argument that is a flag: `klair -l left` is `klair status -l left`.
- A first argument that doesn't start with `-` and isn't a command is a usage error: `unknown command: <arg>`.

### Flags

| Flag      | Value                               | Commands |
| --------- | ----------------------------------- | -------- |
| `-l NAME` | a light name from the config        | all      |
| `-b N`    | brightness in percent, 3 to 100     | `on`     |
| `-t N`    | temperature in Kelvin, 2900 to 7000 | `on`     |

Rules:

- **The value is always the next argument,** taken verbatim. `-l -b` selects a light named `-b`.
- **The separate form must work.** Other forms, like `-b=50` or `--b 50`, may work if the standard library's flag parser takes them.
- **Repeating a flag:** the last one wins.
- **Numbers:** plain digits must work. Parse with the standard library's integer parser into a 64 bit integer, then check the range on whatever it returns. So `-t -5` parses to -5 and gets the range error, and extra forms it takes (`+50`, `0x32`, `1_00`) may work.
- **What doesn't parse is an invalid value:** trailing junk like `12x`, and a number too large for 64 bits like `99999999999999999999`.
- **Never wrap around into a valid value.** `-b 4294967346` fits in 64 bits, so it's a range error, and `-b 18446744073709551666` doesn't fit, so it's an invalid value. Neither may become 50.
- `-b` and `-t` are independent. `on` without them sends only `on`.

Usage errors, checked from left to right, the first one wins:

| Situation                                                           | Message                                                |
| ------------------------------------------------------------------- | ------------------------------------------------------ |
| a flag the command doesn't take, or anything else starting with `-` | `unknown flag for <command>: <arg>`                    |
| an argument not starting with `-` where a flag is expected          | `unexpected argument: <arg>`                           |
| a flag is the last argument                                         | `missing value for <flag>`                             |
| `-b`/`-t` value isn't a number                                      | `invalid value for <flag>: <value>`                    |
| `-b` out of range                                                   | `invalid brightness: must be between 3 and 100`        |
| `-t` out of range                                                   | `invalid temperature: must be between 2900K and 7000K` |

`<command>` is the resolved command, so `klair -b 50` gives `unknown flag for status: -b`.

Range errors are checked after the whole argument list has parsed, `-b` before `-t`. `klair on -b 2 -x` reports the unknown flag `-x`, not the range.

### Usage text

Exactly this, including the final newline:

```
Usage: klair [command] [flags]

Commands:
  status   [-l NAME]
  on       [-b 3-100] [-t 2900-7000] [-l NAME]
  off      [-l NAME]
  help
  version

No command: same as status.
```

## Config

### Location

| OS              | Path                                                                                                       |
| --------------- | ---------------------------------------------------------------------------------------------------------- |
| Windows         | `%APPDATA%\klair`                                                                                          |
| everything else | `$XDG_CONFIG_HOME/klair` if `XDG_CONFIG_HOME` is set and an absolute path, otherwise `$HOME/.config/klair` |

- `klair` is a file, not a folder.
- Not `~/Library/Application Support` on macOS: everyone who uses a terminal looks in `~/.config`, and XDG still works for those who set it.
- If the variable the path is built from is unset or empty (`APPDATA`, or `HOME` when XDG doesn't apply), stderr gets `cannot determine config directory`, exit 1.
- Messages print `<path>` as built, with the OS's separator.

### Format

```
# desk lights
left  = 192.168.2.164:9123
right = 192.168.2.165:9123
```

- UTF-8 text, lines separated by `\n`. Every line is trimmed of leading and trailing spaces, tabs and `\r`, so CRLF files work.
- Empty lines are skipped. So is any line whose first character after trimming is `#`. There are no comments at the end of a line.
- Every other line is `name = address`, split at the first `=`, both sides trimmed.
- **name:** not empty, no spaces or tabs. Matched case sensitively by `-l`.
- **address:** an IPv4 literal with a port (`192.168.2.164:9123`) or a bracketed IPv6 literal with a port (`[fe80::1]:9123`). Port 1 to 65535. No hostnames: resolving one blocks, and a light that's slow to resolve would hold up all the others.
- Lights are kept in file order, which is also the output order. There's no limit on the number of lights or on the length of names.

### Errors

All to stderr, exit 1. Only the first problem is reported.

| Situation                                     | Message                                                              |
| --------------------------------------------- | -------------------------------------------------------------------- |
| file missing or unreadable                    | `cannot read config: <path>`                                         |
| line without `=`                              | `invalid config <path>: line <n>: expected name = address`           |
| empty name                                    | `invalid config <path>: line <n>: empty name`                        |
| name with whitespace                          | `invalid config <path>: line <n>: name "<name>" contains whitespace` |
| empty address                                 | `invalid config <path>: line <n>: empty address`                     |
| address isn't an IP literal with a valid port | `invalid config <path>: line <n>: invalid address "<address>"`       |
| name used on an earlier line                  | `invalid config <path>: line <n>: duplicate name "<name>"`           |
| no lights after parsing                       | `no lights in config: <path>`                                        |

Line numbers start at 1 and count every line, including empty lines and comments. Within a line, the checks run in the order of the table.

### Selecting lights

- Without `-l`: every light in the config.
- With `-l NAME`: the one light with that name. If there's none, stderr gets `light "<name>" not found; available: <names>`, with every name in config order separated by `, `, exit 1.

## Talking to a light

### Requests

| Command  | Request                        |
| -------- | ------------------------------ |
| `status` | `GET /elgato/lights`           |
| `on`     | `PUT /elgato/lights` with body |
| `off`    | `PUT /elgato/lights` with body |

Request rules:

- `HTTP/1.1` with `Connection: close`. One request per connection, and the close tells the light not to wait for another. It's what c-legacy sends, which works with real lights.
- `Host: <ip>` or `Host: <ip>:<port>`, an IPv6 address in brackets either way.
- A `PUT` sends `Content-Type: application/json` and a correct `Content-Length`.
- Other headers are allowed and ignored.

The `PUT` body is exactly this: no whitespace, keys in this order, unset keys left out entirely:

```
{"lights":[{"on":<0|1>,"brightness":<percent>,"temperature":<mired>}]}
```

| Command            | Body                                                      |
| ------------------ | --------------------------------------------------------- |
| `off`              | `{"lights":[{"on":0}]}`                                   |
| `on`               | `{"lights":[{"on":1}]}`                                   |
| `on -b 50`         | `{"lights":[{"on":1,"brightness":50}]}`                   |
| `on -t 4700`       | `{"lights":[{"on":1,"temperature":213}]}`                 |
| `on -b 50 -t 4700` | `{"lights":[{"on":1,"brightness":50,"temperature":213}]}` |

A key that isn't sent keeps its value on the light.

### Responses

A response is complete when either:

- it has a `Content-Length` and that many body bytes arrived, or
- it has no `Content-Length` and the light closed the connection.

Chunked transfer encoding isn't needed: lights don't send it.

A response succeeds when all of these hold:

- status 200, for `GET` and `PUT` alike
- a body of at most 64 KiB. Headers have no limit here, lights send a handful.
- the body is JSON with a `lights` array, whose first element has integer `on`, `brightness` and `temperature` fields
- `temperature` is greater than 0, since it's divided by for display

Other fields (`numberOfLights`) and extra elements of `lights` are ignored. A Key Light Air has exactly one light. A `PUT` response has the same shape as a `GET` response and contains the state after the update, which is what gets printed.

### Units

The light speaks mired, the CLI speaks Kelvin.

**Kelvin to mired**, sent to the light: `round(1,000,000 / kelvin)`, rounding .5 up, then clamped to 143 to 344, the range the light accepts. The clamp only affects 2900K, which would otherwise be 345.

**Mired to Kelvin**, for display: `1,000,000 / mired` rounded to the nearest multiple of 50, rounding .5 up. **In one step.** Don't round to whole Kelvin first:

| mired | exact    | correct | rounded twice |
| ----- | -------- | ------- | ------------- |
| 213   | 4694.84K | 4700K   | 4700K         |
| 181   | 5524.86K | 5500K   | 5550K         |
| 221   | 4524.89K | 4500K   | 4550K         |
| 226   | 4424.78K | 4400K   | 4450K         |
| 292   | 3424.66K | 3400K   | 3450K         |
| 315   | 3174.60K | 3150K   | 3200K         |
| 320   | 3125.00K | 3150K   | 3150K         |

In integers: `(1_000_000 + 25 * mired) / (50 * mired) * 50`.

These are the only five values from 143 to 344 where rounding twice goes wrong. c-legacy's comment claims mired 315 should show 3200K. It shouldn't: 3174.6 is 24.6 away from 3150 and 25.4 away from 3200.

Every multiple of 50 from 2900K to 7000K survives the round trip: `on -t 4700` shows `4700K` afterwards. Values in between show the nearest multiple of 50.

### Attempts, timeouts and retries

- **Every light at once.** A dead light must not delay the others. All results are in when the slowest light finishes or gives up.
- **Up to 3 attempts per light.** Each attempt opens a new connection.
- **3 seconds per attempt**, from starting to connect until the response is complete. It's one budget for all steps of the attempt, not 3 seconds per step.
- **Any failure starts the next attempt:** connect error, timeout, early close, non-200, bad response. A `PUT` is retried too, since setting the same state twice is harmless.
- **Backoff:** 100 ms before attempt 2, 200 ms before attempt 3. No wait after the last attempt.
- After the last failed attempt, the light's result is the error of that last attempt.

So a light that never answers gives up after about 9.3 s (3 × 3 s + 0.1 s + 0.2 s), and a refused light after about 0.3 s.

### Light errors

| Failure                                                                                       | Message                          |
| --------------------------------------------------------------------------------------------- | -------------------------------- |
| connection refused, unreachable, any other connect failure                                    | `failed to connect`              |
| the attempt's 3 seconds ran out, at any step                                                  | `timed out`                      |
| the light closed or reset the connection, or reading failed, before the response was complete | `connection closed unexpectedly` |
| status other than 200                                                                         | `unexpected status <code>`       |
| response too large, not JSON, or missing or invalid fields                                    | `invalid response`               |
| anything else (socket creation, a write failing)                                              | `failed to connect`              |

The messages are fixed. OS error text never appears in the output, so it's the same on every OS.

## Output

### Per light

For each target light, in config order, on stdout. On success, the state the light reported:

```
Status of light "<name>":
  Power:       <ON|OFF>
  Brightness:  <brightness>%
  Temperature: <kelvin>K
```

- `ON` if `on` is 1, `OFF` for anything else.
- Brightness is printed as the light reports it, temperature converted as in [Units](#units).
- `on` and `off` print the same block, with the state from the `PUT` response.

On failure, one line:

```
<Status|Update> of light "<name>": error: <message>
```

`Status` for `status`, `Update` for `on` and `off`. No empty lines between lights.

### Exit codes

| Code | Meaning                                                                   |
| ---- | ------------------------------------------------------------------------- |
| 0    | every target light succeeded, or `help`/`version`                         |
| 1    | at least one light failed, a config error, or `-l` named an unknown light |
| 2    | usage error                                                               |

### Version string

```
klair version <semver>[+<hash>] (<lang>[, debug])
```

The part before the space says which source it was built from, the part in parentheses says how.

- **`<semver>`**: `MAJOR.MINOR.PATCH`, the same in every implementation and bumped by hand together. Git tags are `v<semver>`. `v0.1.0` to `v0.3.0` are c-legacy releases, the shared versions start at `0.4.0`.
- **`<hash>`**: the short commit hash from `git describe --always --dirty --exclude '*'`, so `-dirty` is part of it when the working tree has uncommitted changes. Left out, with its `+`, when built outside a git checkout. It's [semver build metadata](https://semver.org/#spec-item-10), so `<semver>[+<hash>]` is a valid semver version and parsable by semver tools.
- **`<lang>`**: `c`, `odin` or `go`.
- **`, debug`** in debug builds.

| Build                        | Version string                                  |
| ---------------------------- | ----------------------------------------------- |
| release, clean checkout      | `klair version 0.4.0+8b30b7a (c)`               |
| release, uncommitted changes | `klair version 0.4.0+8b30b7a-dirty (odin)`      |
| debug, uncommitted changes   | `klair version 0.4.0+8b30b7a-dirty (go, debug)` |
| debug, no git                | `klair version 0.4.0 (c, debug)`                |
| release, no git              | `klair version 0.4.0 (go)`                      |

### Debug builds

A debug build prints `[debug build]` to stderr as its very first output, for every invocation including `help` and `version`. On stderr, so stdout is the same in debug and release builds and scripts can parse it.

What counts as a debug build is up to each `build.sh`. Each one defaults to `debug` and takes `release`.

## Examples

Config with `left`, `right` and a `dead` light that never answers:

```
$ klair
Status of light "left":
  Power:       ON
  Brightness:  20%
  Temperature: 4700K
Status of light "right":
  Power:       OFF
  Brightness:  35%
  Temperature: 3150K
Status of light "dead": error: timed out
$ echo $?
1
```

```
$ klair on -b 50 -t 4700 -l right
Status of light "right":
  Power:       ON
  Brightness:  50%
  Temperature: 4700K
```

```
$ klair on -b 101
invalid brightness: must be between 3 and 100

Usage: klair [command] [flags]
...
$ echo $?
2
```

```
$ klair status -l middle
light "middle" not found; available: left, right, dead
$ echo $?
1
```

## Changes from c-legacy

|                                   | c-legacy                                                                                    | spec                                                                      |
| --------------------------------- | ------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------- |
| Help and version                  | `-h` and `-v` as the first argument                                                         | `help` and `version` commands, no flags                                   |
| Argument parsing                  | after reading the config, so `klair bogus` without a config complains about the config      | before, `help`, `version` and usage errors never need a config            |
| Usage error exit code             | 1                                                                                           | 2                                                                         |
| Usage error message               | only the usage text, except for unknown commands                                            | what went wrong, then the usage text                                      |
| Range errors (`-b 2`)             | exit 1, no usage text                                                                       | usage error, exit 2                                                       |
| Flags without a command           | `unknown command: -l`                                                                       | `status`                                                                  |
| Integer parsing                   | `strtol`, which also takes leading spaces                                                   | the standard library's integer parser, then a range check, never wrapping |
| `[debug build]`                   | stdout                                                                                      | stderr                                                                    |
| Config location                   | `$HOME/.config/klair`                                                                       | XDG on Unix, `%APPDATA%` on Windows                                       |
| Config comments                   | none                                                                                        | `#` lines                                                                 |
| Config errors                     | `error loading config: <path>` for everything                                               | which line and what's wrong                                               |
| Config checks                     | none beyond `=`                                                                             | name, address, duplicates, no lights                                      |
| Limits                            | 16 lights, 63 character names and addresses, 8 KB config, 8 KB response                     | none, 64 KiB response                                                     |
| GET status code                   | ignored, any status with a parsable body is fine                                            | 200 required                                                              |
| Response without `Content-Length` | complete as soon as the headers arrive                                                      | complete when the light closes the connection                             |
| Mired to Kelvin                   | rounds twice, off by 50K for 5 values                                                       | rounds once                                                               |
| `-t 2900`                         | sends mired 345, outside the light's range                                                  | clamped to 344                                                            |
| Timeout message                   | `timeout while connecting`, even when reading timed out                                     | `timed out`                                                               |
| Other light errors                | raw text: `unexpected status 500: <body>`, `connect: <strerror>`, `parsing response failed` | fixed messages, no OS or body text                                        |
| Version string                    | `git describe --tags` output, e.g. `v0.3.0-4-g8b30b7a`                                      | `klair version 0.4.0+8b30b7a (c)`                                         |

## Not yet verified on real lights

- **The mired range.** 143 to 344 is what the Elgato apps and community docs use. What a real light does with 345 hasn't been tested.
