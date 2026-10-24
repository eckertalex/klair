package main

import (
	"fmt"
	"os"
)

var VERSION = "v0.0.0"
var DEBUG = "false"

var usage = `Usage: klair [-h] [-v] <command> [flags]

Commands:
  status  [-l NAME]
  on      [-b 3-100] [-t 2900-7000] [-l NAME]
  off     [-l NAME]

No command: same as status.
`

func main() {
	if DEBUG == "true" {
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
		fmt.Println(VERSION)
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
