package klair

import "core:fmt"
import "core:os"
import "core:path/filepath"
import "core:strconv"
import "core:strings"
import vmem "core:mem/virtual"

BUILD_NAME :: "klair"
BUILD_VERSION :: "0.4.0"
BUILD_GIT_HASH :: #config(BUILD_GIT_HASH, "")
BUILD_LANG :: "odin"

when BUILD_GIT_HASH != "" {
    BUILD_GIT_HASH_APPEND :: "+" + BUILD_GIT_HASH
} else {
    BUILD_GIT_HASH_APPEND :: ""
}

when ODIN_DEBUG {
    BUILD_DEBUG_APPEND :: ", debug"
} else {
    BUILD_DEBUG_APPEND :: ""
}

// klair version 0.4.0+8b30b7a-dirty (odin, debug)
BUILD_VERSION_FULL :: BUILD_NAME + " version " + BUILD_VERSION + BUILD_GIT_HASH_APPEND + " (" + BUILD_LANG + BUILD_DEBUG_APPEND + ")"

USAGE :: `Usage: klair [-h] [-v] <command> [flags]

Commands:
  status  [-l NAME]
  on      [-b 3-100] [-t 2900-7000] [-l NAME]
  off     [-l NAME]

No command: same as status.
`

BRIGHTNESS_MIN :: 3
BRIGHTNESS_MAX :: 100
TEMPERATURE_MIN :: 2900 // Kelvin
TEMPERATURE_MAX :: 7000

Command :: enum {
    Status,
    On,
    Off,
    Help,
    Version,
}

@(rodata)
COMMAND_NAMES := [Command]string {
    .Status  = "status",
    .On      = "on",
    .Off     = "off",
    .Help    = "help",
    .Version = "version",
}

Flag :: enum {
    Light,
    Brightness,
    Temperature,
}

@(rodata)
FLAG_NAMES := [Flag]string {
	.Light       = "-l",
	.Brightness  = "-b",
	.Temperature = "-t",
}

// Which command takes which flag
@(rodata)
COMMAND_FLAGS := [Command]bit_set[Flag] {
	.Status  = {.Light},
	.On      = {.Light, .Brightness, .Temperature},
	.Off     = {.Light},
	.Help    = {},
	.Version = {},
}

Args :: struct {
	command:     Command,
	light_name:  Maybe(string),
	brightness:  Maybe(int),
	temperature: Maybe(int), // Kelvin
}

// Fills Args or returns the message for a usage error. Range checks come
// last, so a bad flag later in the list wins over an out of range value.
args_from_list :: proc(list: []string) -> (args: Args, usage_error: string) {
	rest := list
	if len(list) > 0 && !strings.has_prefix(list[0], "-") {
		known := false
		for name, command in COMMAND_NAMES {
			if name == list[0] {
				args.command = command
				known = true
			}
		}
		if !known {
			return {}, fmt.aprintf("unknown command: %s", list[0])
		}
		rest = list[1:]
	}

	for i := 0; i < len(rest); i += 1 {
		arg := rest[i]
		if !strings.has_prefix(arg, "-") {
			return {}, fmt.aprintf("unexpected argument: %s", arg)
		}

		flag: Flag
		known := false
		for name, f in FLAG_NAMES {
			if name == arg && f in COMMAND_FLAGS[args.command] {
				flag = f
				known = true
			}
		}
		if !known {
			return {}, fmt.aprintf("unknown flag for %s: %s", COMMAND_NAMES[args.command], arg)
		}

		if i + 1 >= len(rest) {
			return {}, fmt.aprintf("missing value for %s", arg)
		}
		i += 1
		value := rest[i]

		switch flag {
		case .Light:
			args.light_name = value
		case .Brightness, .Temperature:
			number, is_number := strconv.parse_int(value, 10)
			if !is_number {
				return {}, fmt.aprintf("invalid value for %s: %s", arg, value)
			}
			if flag == .Brightness {
				args.brightness = number
			} else {
				args.temperature = number
			}
		}
	}

	if b, given := args.brightness.?; given && (b < BRIGHTNESS_MIN || b > BRIGHTNESS_MAX) {
		return {}, "invalid brightness: must be between 3 and 100"
	}
	if t, given := args.temperature.?; given && (t < TEMPERATURE_MIN || t > TEMPERATURE_MAX) {
		return {}, "invalid temperature: must be between 2900K and 7000K"
	}
	return args, ""
}


run :: proc(args: []string) -> int {
	args, usage_error := args_from_list(args)
	if usage_error != "" {
		fmt.eprintf("%s\n\n%s", usage_error, USAGE)
		return 2
	}

	switch args.command {
    case .Status:
        return 0
    case .On:
        return 0
    case .Off:
        return 0
	case .Help:
		fmt.print(USAGE)
		return 0
	case .Version:
		fmt.println(BUILD_VERSION_FULL)
		return 0
	}

	config_home, ok := config_dir()
	if !ok {
		fmt.eprintln("cannot determine config directory")
		return 1
	}
    config_path, err := filepath.join({config_home, "klair"})
	if err != nil {
		fmt.eprintln("cannot determine config directory")
		return 1
	}

	_, read_err := os.read_entire_file(config_path, context.allocator)
	if read_err != nil {
		fmt.eprintfln("cannot read config: %s", config_path)
		return 1
	}

    return 0
}

main :: proc() {
	if ODIN_DEBUG {
		fmt.eprintln("[debug build]")
	}

    arena: vmem.Arena
	arena_err := vmem.arena_init_growing(&arena)
    ensure(arena_err == nil)
    context.allocator = vmem.arena_allocator(&arena)

    os.exit(run(os.args[1:]))
}

