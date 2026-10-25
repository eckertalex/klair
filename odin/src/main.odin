package klair

import "core:fmt"
import vmem "core:mem/virtual"
import "core:os"

BUILD_NAME :: "klair"
BUILD_VERSION :: "0.4.0"
BUILD_GIT_HASH :: #config(BUILD_GIT_HASH, "")
BUILD_LANG :: "odin"

when BUILD_GIT_HASH != "" {
    BUILD_GIT_HASH_APPEND :: ":" + BUILD_GIT_HASH
} else {
    BUILD_GIT_HASH_APPEND :: ""
}

when ODIN_DEBUG {
    BUILD_DEBUG_APPEND :: ":debug"
} else {
    BUILD_DEBUG_APPEND :: ""
}

BUILD_VERSION_FULL :: BUILD_NAME + " version " + BUILD_VERSION + BUILD_GIT_HASH_APPEND + BUILD_DEBUG_APPEND + " (" + BUILD_LANG + ")"

USAGE :: `Usage: klair [-h] [-v] <command> [flags]

Commands:
  status  [-l NAME]
  on      [-b 3-100] [-t 2900-7000] [-l NAME]
  off     [-l NAME]

No command: same as status.
`

Command :: enum {
    Status,
    On,
    Off,
}

Flags :: struct {
    light_name:   string,
    brightness:   Maybe(int),
    temperature:  Maybe(int), // Kelvin
}

main :: proc() {
	if ODIN_DEBUG {
		fmt.println("[debug build]")
	}

    arena: vmem.Arena
	arena_err := vmem.arena_init_growing(&arena)
    ensure(arena_err == nil)
    context.allocator = vmem.arena_allocator(&arena)

    os.exit(run(os.args[1:]))
}

run :: proc(args: []string) -> int {
    if len(args) > 0 && args[0] == "-h" {
        fmt.print(USAGE)
        return 0
    }

    if len(args) > 0 && args[0] == "-v" {
        fmt.println(BUILD_VERSION_FULL)
        return 0
    }

    home, ok := config_path()
    if !ok {
        fmt.eprintln("cannot determine home directory")
        return 1
    }

    fmt.println(home)

    return 0
}
