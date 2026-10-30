package main

import (
	"errors"
	"flag"
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

// klair version 0.4.0+8b30b7a-dirty (go, debug)
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

var USAGE = `Usage: klair [command] [flags]

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
	Command     Command
	LightName   string // "" means all lights
	Brightness  *int64 // nil menas not given
	Temperature *int64 // Kelvin, nil means not given
}

func parseArgs(args []string) (Args, string) {
	var parsed Args

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
		args = args[1:]
	}

	// The standard flag parser, so its -h and error messages come with it.
	// run prints the usage, so the parser itself prints nothing.
	fs := flag.NewFlagSet(COMMAND_NAMES[parsed.Command], flag.ContinueOnError)
	fs.SetOutput(io.Discard)
	if parsed.Command <= COMMAND_OFF {
		fs.StringVar(&parsed.LightName, "l", "", "")
	}
	if parsed.Command == COMMAND_ON {
		int64Flag(fs, "b", &parsed.Brightness)
		int64Flag(fs, "t", &parsed.Temperature)
	}

	err := fs.Parse(args)
	if errors.Is(err, flag.ErrHelp) {
		return Args{Command: COMMAND_HELP}, ""
	}
	if err != nil {
		return Args{}, err.Error()
	}
	if fs.NArg() > 0 {
		return Args{}, "unexpected argument: " + fs.Arg(0)
	}

	if b := parsed.Brightness; b != nil && (*b < BRIGHTNESS_MIN || *b > BRIGHTNESS_MAX) {
		return Args{}, "invalid brightness: must be between 3 and 100"
	}
	if t := parsed.Temperature; t != nil && (*t < TEMPERATURE_MIN || *t > TEMPERATURE_MAX) {
		return Args{}, "invalid temperature: must be between 2900K and 7000K"
	}

	return parsed, ""
}

// 64 bits on every platform, then parseArgs checks the range on whatever
// parsed. Base 10, so 010 is ten. ParseInt also takes "+50", which the spec
// allows. Both syntax errors and numbers too large for 64 bits are invalid.
func int64Flag(fs *flag.FlagSet, name string, value **int64) {
	fs.Func(name, "", func(s string) error {
		n, err := strconv.ParseInt(s, 10, 64)
		if err != nil {
			return errors.New("not a 64 bit integer")
		}
		*value = &n
		return nil
	})
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

	configHome, err := configDir()
	if err != nil {
		fmt.Fprintf(stderr, "cannot determine config directory")
		return 1
	}
	configPath := filepath.Join(configHome, "klair")

	text, err := os.ReadFile(configPath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "cannot read config: %s\n", configPath)
		return 1
	}

	lights, err := parseConfig(string(text))
	if err != nil {
		fmt.Fprintf(os.Stderr, "invalid config %s: %v\n", configPath, err)
		return 1
	}
	if len(lights) == 0 {
		fmt.Fprintf(os.Stderr, "no lights in config: %s\n", configPath)
		return 1
	}

	return 0
}

func main() {
	os.Exit(run(os.Args[1:], os.Stdout, os.Stderr))
}
