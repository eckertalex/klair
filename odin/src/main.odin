package klair

import "core:fmt"
import vmem "core:mem/virtual"
import "core:os"
import "core:path/filepath"
import "core:strconv"
import "core:strings"

BUILD_NAME :: "klair"
BUILD_VERSION :: "0.4.0"
BUILD_LANG :: "odin"
BUILD_GIT_HASH :: #config(BUILD_GIT_HASH, "")

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

BUILD_VERSION_FULL :: BUILD_NAME + " version " + BUILD_VERSION + BUILD_GIT_HASH_APPEND + " (" + BUILD_LANG + BUILD_DEBUG_APPEND + ")"

USAGE :: `Usage: klair [command] [flags]

Commands:
  status   [-l NAME]
  on       [-b 3-100] [-t 2900-7000] [-l NAME]
  off      [-l NAME]
  help
  version

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

main :: proc() {
	arena: vmem.Arena
	ensure(vmem.arena_init_growing(&arena) == nil)
	context.allocator = vmem.arena_allocator(&arena)

	os.exit(run(os.args[1:]))
}

run :: proc(arg_list: []string) -> int {
	args, usage_error := args_from_list(arg_list)
	if usage_error != "" {
		fmt.eprintf("%s\n\n%s", usage_error, USAGE)
		return 2
	}

	method: Method
	settings: Settings
	switch args.command {
	case .Help:
		fmt.print(USAGE)
		return 0
	case .Version:
		fmt.println(BUILD_VERSION_FULL)
		return 0
	case .Status:
		method = .Get
	case .On:
		method = .Put
		settings.on = true
		settings.brightness = args.brightness
		if kelvin, given := args.temperature.?; given {
			settings.temperature = mired_from_kelvin(kelvin)
		}
	case .Off:
		method = .Put
	}

	dir, has_path := config_dir()
	if !has_path {
		fmt.eprintln("cannot determine config directory")
		return 1
	}
	path, path_err := filepath.join({dir, "klair"})
	if path_err != nil {
		fmt.eprintln("cannot determine config directory")
		return 1
	}
	text, read_err := os.read_entire_file(path, context.allocator)
	if read_err != nil {
		fmt.eprintfln("cannot read config: %s", path)
		return 1
	}
	lights, config_err, config_ok := lights_from_config(string(text))
	if !config_ok {
		fmt.eprintfln("invalid config %s: line %d: %s", path, config_err.line, config_err.message)
		return 1
	}
	if len(lights) == 0 {
		fmt.eprintfln("no lights in config: %s", path)
		return 1
	}

	targets, found := lights_from_name(lights[:], args.light_name)
	if !found {
		names := make([]string, len(lights))
		for light, i in lights {
			names[i] = light.name
		}
		fmt.eprintfln(`light "%s" not found; available: %s`, args.light_name.?, strings.join(names, ", "))
		return 1
	}

	jobs := make([]Job, len(targets))
	for &light, i in targets {
		job_init(&jobs[i], &light, method, settings)
	}
	run_jobs(jobs)

	verb := "Status" if args.command == .Status else "Update"
	all_ok := true
	for &job in jobs {
		print_job(&job, verb)
		all_ok &&= job.ok
	}
	return 0 if all_ok else 1
}

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

lights_from_name :: proc(lights: []Light, name: Maybe(string)) -> (targets: []Light, found: bool) {
	wanted, given := name.?
	if !given {
		return lights, true
	}
	for light, i in lights {
		if light.name == wanted {
			return lights[i:i + 1], true
		}
	}
	return nil, false
}

print_job :: proc(job: ^Job, verb: string) {
	if !job.ok {
		fmt.printfln(`%s of light "%s": error: %s`, verb, job.light.name, message_from_job(job))
		return
	}
	fmt.printfln(`Status of light "%s":`, job.light.name)
	fmt.printfln("  Power:       %s", "ON" if job.state.on == 1 else "OFF")
	fmt.printfln("  Brightness:  %d%%", job.state.brightness)
	fmt.printfln("  Temperature: %dK", kelvin_from_mired(job.state.temperature))
}
