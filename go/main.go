package main

import (
	"flag"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"
)

const (
	BUILD_NAME    = "klair"
	BUILD_VERSION = "0.1.0"
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
		s += ":" + BUILD_GIT_HASH
	}
	if BUILD_DEBUG == "true" {
		s += ":debug"
	}
	return s + " (" + BUILD_LANG + ")"
}

var BUILD_VERSION_FULL = buildVersionFull()

var usage = `Usage: klair [-h] [-v] <command> [flags]

Commands:
  status  [-l NAME]
  on      [-b 3-100] [-t 2900-7000] [-l NAME]
  off     [-l NAME]

No command: same as status.
`

const (
	BRIGHTNESS_MIN  = 3
	BRIGHTNESS_MAX  = 100
	TEMPERATURE_MIN = 2900 // Kelvin
	TEMPERATURE_MAX = 7000
)

type Flags struct {
	LightName   string // "" means all lights
	Brightness  int    // 0 means not given
	Temperature int    // Kelvin, 0 means not given
}

func parseFlags(command string, args []string) (Flags, bool) {
	var flags Flags

	fs := flag.NewFlagSet(command, flag.ContinueOnError)
	fs.SetOutput(io.Discard) // we print our own usage
	fs.StringVar(&flags.LightName, "l", "", "")
	if command == "on" {
		fs.IntVar(&flags.Brightness, "b", 0, "")
		fs.IntVar(&flags.Temperature, "t", 0, "")
	}

	if err := fs.Parse(args); err != nil {
		return Flags{}, false
	}
	if fs.NArg() != 0 {
		return Flags{}, false
	}

	given := map[string]bool{}
	fs.Visit(func(f *flag.Flag) { given[f.Name] = true })

	if given["b"] && flags.Brightness < BRIGHTNESS_MIN || flags.Brightness > BRIGHTNESS_MAX {
		return Flags{}, false
	}
	if given["t"] && flags.Temperature < TEMPERATURE_MIN || flags.Temperature > TEMPERATURE_MAX {
		return Flags{}, false
	}

	return flags, true
}

func run(args []string) int {
	if len(args) > 0 && args[0] == "-h" {
		fmt.Print(usage)
		return 0
	}

	if len(args) > 0 && args[0] == "-v" {
		fmt.Println(BUILD_VERSION_FULL)
		return 0
	}

	command := "status"
	flagArgs := args
	if len(args) > 0 && !strings.HasPrefix(args[0], "-") {
		command, flagArgs = args[0], args[1:]
	}

	if command != "status" && command != "on" && command != "off" {
		fmt.Fprintf(os.Stderr, "unknown command: %s\n\n", command)
		fmt.Fprint(os.Stderr, usage)
		return 1
	}

	flags, ok := parseFlags(command, flagArgs)
	if !ok {
		fmt.Fprint(os.Stderr, usage)
		return 1
	}

	fmt.Println(flags)

	configHome, err := configDir()
	if err != nil {
		fmt.Fprintf(os.Stderr, "cannot determine config directory: %v\n", err)
		return 1
	}
	configPath := filepath.Join(configHome, "klair")

	configText, err := os.ReadFile(configPath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "error reading config: %v\n", err)
		return 1
	}

	lights, err := parseConfig(string(configText))
	if err != nil {
		fmt.Fprintf(os.Stderr, "error parsing config %s: %v\n", configPath, err)
		return 1
	}
	if len(lights) == 0 {
		fmt.Fprintf(os.Stderr, "no lights in config: %s\n", configPath)
		return 1
	}

	return 0
}

func main() {
	if BUILD_DEBUG == "true" {
		fmt.Fprintln(os.Stderr, "[debug build]")
	}

	os.Exit(run(os.Args[1:]))
}
