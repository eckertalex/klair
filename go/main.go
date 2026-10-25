package main

import (
	"fmt"
	"os"
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

func main() {
	if BUILD_DEBUG == "true" {
		fmt.Println("[debug build]")
	}

	os.Exit(run(os.Args[1:]))
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

	// HOME on macOS and Linux, USERPROFILE on Windows
	home, err := os.UserHomeDir()
	if err != nil {
		fmt.Fprintln(os.Stderr, "cannot determine home directory")
		return 1
	}

	fmt.Println(home)

	return 0
}
