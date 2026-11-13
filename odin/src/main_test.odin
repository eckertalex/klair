package klair

import vmem "core:mem/virtual"
import "core:testing"

@(test)
args_pick_command_and_flags :: proc(t: ^testing.T) {
	arena: vmem.Arena
	context.allocator = test_allocator(&arena)
	defer vmem.arena_destroy(&arena)

	args, err := args_from_list({})
	testing.expect_value(t, err, "")
	testing.expect_value(t, args.command, Command.Status)

	args, err = args_from_list({"-l", "left"})
	testing.expect_value(t, err, "")
	testing.expect_value(t, args.command, Command.Status)
	testing.expect_value(t, args.light_name.?, "left")

	args, err = args_from_list({"on", "-b", "10", "-t", "4700", "-b", "60", "-l", "-b"})
	testing.expect_value(t, err, "")
	testing.expect_value(t, args.command, Command.On)
	testing.expect_value(t, args.brightness.?, 60) // the last one wins
	testing.expect_value(t, args.temperature.?, 4700)
	testing.expect_value(t, args.light_name.?, "-b") // values are taken verbatim

	args, err = args_from_list({"on", "-b", "+50"})
	testing.expect_value(t, err, "")
	testing.expect_value(t, args.brightness.?, 50)

	args, err = args_from_list({"version"})
	testing.expect_value(t, err, "")
	testing.expect_value(t, args.command, Command.Version)
}

@(test)
args_usage_errors :: proc(t: ^testing.T) {
	arena: vmem.Arena
	context.allocator = test_allocator(&arena)
	defer vmem.arena_destroy(&arena)

	Case :: struct {
		list:    []string,
		message: string,
	}
	cases := [?]Case {
		{{"bogus"}, "unknown command: bogus"},
		{{"status", "-b", "50"}, "unknown flag for status: -b"},
		{{"-b", "50"}, "unknown flag for status: -b"},
		{{"off", "-x"}, "unknown flag for off: -x"},
		{{"on", "-b=50"}, "unknown flag for on: -b=50"},
		{{"on", "--b", "50"}, "unknown flag for on: --b"},
		{{"-h"}, "unknown flag for status: -h"},
		{{"--help"}, "unknown flag for status: --help"},
		{{"version", "-l", "left"}, "unknown flag for version: -l"},
		{{"status", "extra"}, "unexpected argument: extra"},
		{{"help", "me"}, "unexpected argument: me"},
		{{"on", "-b"}, "missing value for -b"},
		{{"on", "-b", "12x"}, "invalid value for -b: 12x"},
		{{"on", "-b", "2"}, "invalid brightness: must be between 3 and 100"},
		{{"on", "-b", "101"}, "invalid brightness: must be between 3 and 100"},
		{{"on", "-b", "4294967346"}, "invalid brightness: must be between 3 and 100"},
		{{"on", "-b", "18446744073709551666"}, "invalid value for -b: 18446744073709551666"},
		{{"on", "-t", "-5"}, "invalid temperature: must be between 2900K and 7000K"},
		{{"on", "-t", "9000"}, "invalid temperature: must be between 2900K and 7000K"},
		{{"on", "-t", "2850"}, "invalid temperature: must be between 2900K and 7000K"},
		{{"on", "-b", "2", "-x"}, "unknown flag for on: -x"},
		{{"on", "-t", "9000", "-b", "2"}, "invalid brightness: must be between 3 and 100"},
	}
	for c in cases {
		_, err := args_from_list(c.list)
		testing.expect_value(t, err, c.message)
	}
}

@(test)
lights_from_name_selects :: proc(t: ^testing.T) {
	lights := [?]Light{{name = "left"}, {name = "right"}}

	all, found := lights_from_name(lights[:], nil)
	testing.expect(t, found)
	testing.expect_value(t, len(all), 2)

	one: []Light
	one, found = lights_from_name(lights[:], "right")
	testing.expect(t, found)
	testing.expect_value(t, len(one), 1)
	testing.expect_value(t, one[0].name, "right")

	_, found = lights_from_name(lights[:], "Right")
	testing.expect(t, !found)
}
