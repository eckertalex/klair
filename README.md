# klair

Controls Elgato Key Light Air lights from the terminal: show their status, turn them on and off, set brightness and color temperature.

The same tool written three times, in C, Odin and Go, against one [CLI spec](docs/cli-spec.md), to compare the languages on a small but real program. Plus `c-legacy/`, the original C version.

```
$ klair
Status of light "left":
  Power:       ON
  Brightness:  20%
  Temperature: 4700K
Status of light "right":
  Power:       ON
  Brightness:  20%
  Temperature: 4700K
```

## Usage

```sh
klair                       # same as status
klair status                # every light's power, brightness and temperature
klair on                    # turn every light on
klair on -b 50 -t 4700      # on, at 50% and 4700K
klair off -l left           # turn one light off
klair help                  # print usage
klair version               # print the version
```

| Flag      | Value                            | Commands |
| --------- | -------------------------------- | -------- |
| `-l NAME` | a light from the config          | all      |
| `-b N`    | brightness, 3 to 100 percent     | `on`     |
| `-t N`    | temperature, 2900 to 7000 Kelvin | `on`     |

All lights are talked to at the same time, so one that's off the network doesn't hold up the others. Each gets 3 attempts of 3 seconds.

Exit code 0 if every light answered, 1 if one didn't or the config is wrong, 2 for a usage error. The [CLI spec](docs/cli-spec.md) has every message.

## Config

One `name = ip:port` per line, `#` starts a comment line:

```
left  = 192.168.2.164:9123
right = 192.168.2.165:9123
```

| OS           | Path                                           |
| ------------ | ---------------------------------------------- |
| macOS, Linux | `$XDG_CONFIG_HOME/klair`, or `~/.config/klair` |
| Windows      | `%APPDATA%\klair`                              |

Port 9123 is what the lights listen on. Their IP addresses are in the Elgato Control Center app, or in your router. Give them fixed addresses in your router so the config doesn't go stale.

## Implementations

| Folder                   | Language | Waiting on several lights                                | Memory            | Written for           |
| ------------------------ | -------- | -------------------------------------------------------- | ----------------- | --------------------- |
| [`c-legacy/`](c-legacy/) | C23      | `poll()` state machine                                   | fixed buffers     | macOS, Linux          |
| [`c/`](docs/c.md)        | C23      | the native mechanism of each OS, behind a platform layer | arenas, `String8` | macOS, Linux, Windows |
| [`odin/`](docs/odin.md)  | Odin     | `core:nbio` callbacks                                    | one arena         | macOS, Linux, Windows |
| [`go/`](docs/go.md)      | Go       | a goroutine per light                                    | garbage collector | macOS, Linux, Windows |

- **`c-legacy/`** is the original: one file, POSIX sockets. It predates the spec, its differences are [listed there](docs/cli-spec.md#changes-from-c-legacy).
- **`c/`** is the handmade style version: a base layer, a socket layer with one backend per OS, a unity build.
- **`odin/`** is the same design in a language made for that style, to see how many of C's papercuts go away.
- **`go/`** is the comparison point, the one of the three I'm most comfortable in, straight line code and the runtime does the waiting.

### Features

Ticked when it matches the [CLI spec](docs/cli-spec.md) and passes every [conformance](#testing) case.

#### `c`

- [ ] `help` and the usage text
- [ ] `version` and the version string
- [ ] Usage errors with a message, exit 2
- [ ] Config location (XDG, `%APPDATA%`)
- [ ] Config parsing and its errors
- [ ] `-l` to pick a light
- [ ] `status`
- [ ] `on` and `off`
- [ ] `-b` brightness and `-t` temperature
- [ ] Kelvin and mired conversion
- [ ] All lights at once
- [ ] Retries and timeouts
- [ ] Light error messages
- [ ] Unit tests
- [ ] All conformance cases pass
- [ ] Builds and runs on Linux
- [ ] Builds and runs on Windows

#### `odin`

- [ ] `help` and the usage text
- [ ] `version` and the version string
- [ ] Usage errors with a message, exit 2
- [ ] Config location (XDG, `%APPDATA%`)
- [ ] Config parsing and its errors
- [ ] `-l` to pick a light
- [ ] `status`
- [ ] `on` and `off`
- [ ] `-b` brightness and `-t` temperature
- [ ] Kelvin and mired conversion
- [ ] All lights at once
- [ ] Retries and timeouts
- [ ] Light error messages
- [ ] Unit tests
- [ ] All conformance cases pass
- [ ] Builds and runs on Linux
- [ ] Builds and runs on Windows

#### `go`

- [ ] `help` and the usage text
- [ ] `version` and the version string
- [ ] Usage errors with a message, exit 2
- [ ] Config location (XDG, `%APPDATA%`)
- [ ] Config parsing and its errors
- [ ] `-l` to pick a light
- [ ] `status`
- [ ] `on` and `off`
- [ ] `-b` brightness and `-t` temperature
- [ ] Kelvin and mired conversion
- [ ] All lights at once
- [ ] Retries and timeouts
- [ ] Light error messages
- [ ] Unit tests
- [ ] All conformance cases pass
- [ ] Builds and runs on Linux
- [ ] Builds and runs on Windows

#### Testing

- [ ] Fake lights
- [ ] Conformance checker and the cases
- [ ] `./check.sh` builds everything and runs every case

## Build

Each folder has a `build.sh` that builds into its own `build/` folder. No make, no build system: each version is a unity build or a single package, so a build is one compiler call.

```sh
./build.sh                  # c, odin and go, debug
./build.sh release          # c, odin and go, release
./build.sh go test          # name implementations first to pick them
./check.sh                  # build, then run the conformance cases

c/build.sh release          # c/build/klair
odin/build.sh release       # odin/build/klair
go/build.sh release         # go/build/klair
c-legacy/build.sh release   # c-legacy/build/klair
```

Every `build.sh` takes `debug` (the default) or `release`. The C ones use clang unless `CC` says otherwise, e.g. `CC=gcc c/build.sh`. Odin and Go take `test`. Go builds also run `go vet`.

Needs: clang 18+ or gcc 14+ for C, a recent Odin (it uses `core:nbio`), Go from `go/go.mod`.

## Install

Install `c-legacy`. The others aren't complete until every box in their list under [Features](#features) is ticked.

```sh
c-legacy/build.sh release
cp c-legacy/build/klair ~/.local/bin/
```

`~/.local/bin` needs to be on your `PATH`. To uninstall, delete the binary.

`c-legacy` takes `-h` and `-v` instead of `help` and `version`, and only reads `~/.config/klair`. Any other version installs the same way from its own `build/` folder.

## Testing

[`conformance/`](conformance/) is one Odin program with fake Elgato lights on localhost and a checker. Each fake light misbehaves differently: drops a connection, answers slowly, answers garbage, never answers. The checker runs klair binaries through every case and compares stdout, stderr, exit code, the requests the lights received and timing against the spec. That's what keeps three implementations in line.

```sh
./check.sh                   # c, go and odin
./check.sh go odin -q        # only these, without the 10 s case
./check.sh c -run config     # only cases with "config" in their name
./check.sh release           # release builds
conformance/build/conformance serve  # only the lights, to try klair by hand
```

Unit tests run with `<folder>/build.sh test` for `odin`, `go` and `conformance`, or `./build.sh odin go test`.

## History

1. [`elgato`](https://github.com/eckertalex/dotfiles/blob/c39b1e8bfa1bb2715be84d5fdfcb752e6f1f7c76/common/.local/bin/elgato) (March 2024 to November 2024), a Bash script in my dotfiles.
2. [keylightctl](https://github.com/eckertalex/keylightctl) (November 2024 to July 2026), a CLI in Go with a TUI on top. I kept using the CLI and never used the TUI.
3. `c-legacy/` (July 2026 onward), the same in C, handmade inspired, no TUI.
4. `c/`, `odin/` and `go/`, the same tool three more times, to compare.
