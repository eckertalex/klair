# klair

Controls Elgato Key Light Air devices from the terminal: get status, turn lights
on and off, and set brightness and temperature. Pure C (C23 + POSIX sockets, no
dependencies), written handmade-hero style as a learning exercise.

Lights are read from `~/.config/klair`, one `name = ip` pair per line (up to
16 lights):

```
left  = 192.168.2.164:9123
right = 192.168.2.165:9123
```

```sh
klair             # no command: same as status
klair status      # show each light's status
klair on          # turn on (-b brightness 3-100, -t temperature 2900-7000K, -l light)
klair off         # turn off (-l light)
klair -h          # print usage
klair -v          # print version
```

## Build

Requires clang 18+ or gcc 14+ (C23).

```sh
./build.sh
```

Set `CC` to pick a compiler: `CC=clang ./build.sh`.

## Install

```sh
cp klair ~/.local/bin/
```

Make sure `~/.local/bin` is on your `PATH`. To uninstall, delete the binary.
