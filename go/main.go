package main

import (
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strconv"
	"strings"
)

const (
	BUILD_NAME    = "klair"
	BUILD_VERSION = "0.4.0"
	BUILD_LANG    = "go"
)

var (
	BUILD_GIT_HASH = ""
	BUILD_DEBUG    = "true"
)

func assert(cond bool, msg string) {
	if !cond {
		panic("assertion failed: " + msg)
	}
}

func buildVersionFull() string {
	s := BUILD_NAME + " version " + BUILD_VERSION
	if BUILD_GIT_HASH != "" {
		s += "+" + BUILD_GIT_HASH
	}
	s += " (" + BUILD_LANG
	if BUILD_DEBUG == "true" {
		s += ", debug"
	}
	return s + ")"
}

const USAGE = `Usage: klair [command] [flags]

Commands:
  status   [-l NAME]
  on       [-b 3-100] [-t 2900-7000] [-l NAME]
  off      [-l NAME]
  help
  version

No command: same as status.
`

const (
	BRIGHTNESS_MIN  = 3
	BRIGHTNESS_MAX  = 100
	TEMPERATURE_MIN = 2900 // Kelvin
	TEMPERATURE_MAX = 7000
)

type Command int

const (
	COMMAND_STATUS Command = iota
	COMMAND_ON
	COMMAND_OFF
	COMMAND_HELP
	COMMAND_VERSION
)

var COMMAND_NAMES = [...]string{
	COMMAND_STATUS:  "status",
	COMMAND_ON:      "on",
	COMMAND_OFF:     "off",
	COMMAND_HELP:    "help",
	COMMAND_VERSION: "version",
}

type Args struct {
	Command      Command
	LightName    string
	HasLightName bool
	Brightness   *int64 // nil means not given
	Temperature  *int64 // Kelvin, nil means not given
}

func parseArgs(args []string) (Args, string) {
	var parsed Args

	i := 0
	if len(args) > 0 && !strings.HasPrefix(args[0], "-") {
		found := false
		for command, name := range COMMAND_NAMES {
			if args[0] == name {
				parsed.Command = Command(command)
				found = true
			}
		}
		if !found {
			return Args{}, "unknown command: " + args[0]
		}
		i = 1
	}

	for ; i < len(args); i += 2 {
		flag := args[i]
		if !strings.HasPrefix(flag, "-") {
			return Args{}, "unexpected argument: " + flag
		}

		takesL := flag == "-l" && parsed.Command <= COMMAND_OFF
		takesBT := (flag == "-b" || flag == "-t") && parsed.Command == COMMAND_ON
		if !takesL && !takesBT {
			return Args{}, "unknown flag for " + COMMAND_NAMES[parsed.Command] + ": " + flag
		}
		if i+1 >= len(args) {
			return Args{}, "missing value for " + flag
		}

		value := args[i+1]
		if flag == "-l" {
			parsed.LightName = value
			parsed.HasLightName = true
			continue
		}

		n, err := strconv.ParseInt(value, 10, 64)
		if err != nil {
			return Args{}, "invalid value for " + flag + ": " + value
		}
		if flag == "-b" {
			parsed.Brightness = &n
		} else {
			parsed.Temperature = &n
		}
	}

	if b := parsed.Brightness; b != nil && (*b < BRIGHTNESS_MIN || *b > BRIGHTNESS_MAX) {
		return Args{}, "invalid brightness: must be between 3 and 100"
	}
	if t := parsed.Temperature; t != nil && (*t < TEMPERATURE_MIN || *t > TEMPERATURE_MAX) {
		return Args{}, "invalid temperature: must be between 2900K and 7000K"
	}

	return parsed, ""
}

func run(args []string, stdout, stderr io.Writer) int {
	parsed, problem := parseArgs(args)
	if problem != "" {
		fmt.Fprintf(stderr, "%s\n\n%s", problem, USAGE)
		return 2
	}

	switch parsed.Command {
	case COMMAND_HELP:
		fmt.Fprint(stdout, USAGE)
		return 0
	case COMMAND_VERSION:
		fmt.Fprintln(stdout, buildVersionFull())
		return 0
	}

	dir, err := configDir()
	if err != nil {
		fmt.Fprintln(stderr, "cannot determine config directory")
		return 1
	}
	configPath := filepath.Join(dir, "klair")

	text, err := os.ReadFile(configPath)
	if err != nil {
		fmt.Fprintf(stderr, "cannot read config: %s\n", configPath)
		return 1
	}

	lights, err := parseConfig(string(text))
	if err != nil {
		fmt.Fprintf(stderr, "invalid config %s: %v\n", configPath, err)
		return 1
	}
	if len(lights) == 0 {
		fmt.Fprintf(stderr, "no lights in config: %s\n", configPath)
		return 1
	}

	targets, found := selectLights(lights, parsed.LightName, parsed.HasLightName)
	if !found {
		names := make([]string, len(lights))
		for i, light := range lights {
			names[i] = light.Name
		}
		fmt.Fprintf(stderr, "light \"%s\" not found; available: %s\n", parsed.LightName, strings.Join(names, ", "))
		return 1
	}

	var settings *Settings
	verb := "Status"
	switch parsed.Command {
	case COMMAND_ON:
		settings = &Settings{On: 1}
		if b := parsed.Brightness; b != nil {
			brightness := int(*b)
			settings.Brightness = &brightness
		}
		if kelvin := parsed.Temperature; kelvin != nil {
			mired := kelvinToMired(int(*kelvin))
			settings.Temperature = &mired
		}
		verb = "Update"
	case COMMAND_OFF:
		settings = &Settings{On: 0}
		verb = "Update"
	}

	results := requestAll(targets, settings)

	code := 0
	for i, result := range results {
		name := targets[i].Name
		if result.Error != ERROR_NONE {
			fmt.Fprintf(stdout, "%s of light \"%s\": error: %s\n", verb, name, messageFromResult(result))
			code = 1
			continue
		}

		power := "OFF"
		if result.State.On == 1 {
			power = "ON"
		}
		fmt.Fprintf(stdout, "Status of light \"%s\":\n", name)
		fmt.Fprintf(stdout, "  Power:       %s\n", power)
		fmt.Fprintf(stdout, "  Brightness:  %d%%\n", result.State.Brightness)
		fmt.Fprintf(stdout, "  Temperature: %dK\n", miredToKelvin(result.State.Temperature))
	}

	return code
}

func main() {
	os.Exit(run(os.Args[1:], os.Stdout, os.Stderr))
}
