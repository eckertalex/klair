package main

import (
	"io"
	"reflect"
	"testing"
)

func TestParseArgs(t *testing.T) {
	n := func(v int64) *int64 { return &v }

	tests := []struct {
		name string
		args []string
		ok   bool
		want Args
	}{
		{"no command is status", nil, true, Args{Command: COMMAND_STATUS}},
		{"flag first is status", []string{"-l", "left"}, true, Args{Command: COMMAND_STATUS, LightName: "left"}},
		{"status light", []string{"status", "-l", "left"}, true, Args{Command: COMMAND_STATUS, LightName: "left"}},
		{"off light", []string{"off", "-l", "left"}, true, Args{Command: COMMAND_OFF, LightName: "left"}},
		{"on all flags", []string{"on", "-b", "50", "-t", "4700", "-l", "left"}, true, Args{COMMAND_ON, "left", n(50), n(4700)}},
		{"repeated -l last wins", []string{"on", "-l", "a", "-l", "b"}, true, Args{Command: COMMAND_ON, LightName: "b"}},
		{"-l takes a flag-looking value", []string{"status", "-l", "-b"}, true, Args{Command: COMMAND_STATUS, LightName: "-b"}},
		{"equals form", []string{"on", "-b=50"}, true, Args{Command: COMMAND_ON, Brightness: n(50)}},
		{"leading zero is decimal", []string{"on", "-b", "010"}, true, Args{Command: COMMAND_ON, Brightness: n(10)}},
		{"help", []string{"help"}, true, Args{Command: COMMAND_HELP}},
		{"-h is help", []string{"-h"}, true, Args{Command: COMMAND_HELP}},
		{"on -h is help", []string{"on", "-h"}, true, Args{Command: COMMAND_HELP}},
		{"version", []string{"version"}, true, Args{Command: COMMAND_VERSION}},

		{"unknown command", []string{"bogus"}, false, Args{}},
		{"-v is an unknown flag", []string{"-v"}, false, Args{}},
		{"--version is an unknown flag", []string{"--version"}, false, Args{}},
		{"help with argument", []string{"help", "me"}, false, Args{}},
		{"version with flag", []string{"version", "-l", "left"}, false, Args{}},
		{"-b on status", []string{"status", "-b", "50"}, false, Args{}},
		{"-t on off", []string{"off", "-t", "4700"}, false, Args{}},
		{"missing value -l", []string{"status", "-l"}, false, Args{}},
		{"missing value -b", []string{"on", "-b"}, false, Args{}},
		{"trailing garbage in number", []string{"on", "-b", "12x"}, false, Args{}},
		{"empty number", []string{"on", "-b", ""}, false, Args{}},
		{"fits 64 bits but out of range", []string{"on", "-b", "4294967346"}, false, Args{}},
		{"int64 overflow", []string{"on", "-t", "99999999999999999999"}, false, Args{}},
		{"unknown flag", []string{"status", "-x"}, false, Args{}},
		{"stray positional", []string{"on", "50"}, false, Args{}},

		{"brightness explicit 0", []string{"on", "-b", "0"}, false, Args{}},
		{"brightness below min", []string{"on", "-b", "2"}, false, Args{}},
		{"brightness at min", []string{"on", "-b", "3"}, true, Args{Command: COMMAND_ON, Brightness: n(3)}},
		{"brightness at max", []string{"on", "-b", "100"}, true, Args{Command: COMMAND_ON, Brightness: n(100)}},
		{"brightness above max", []string{"on", "-b", "101"}, false, Args{}},
		{"temperature below min", []string{"on", "-t", "2899"}, false, Args{}},
		{"temperature at min", []string{"on", "-t", "2900"}, true, Args{Command: COMMAND_ON, Temperature: n(2900)}},
		{"temperature at max", []string{"on", "-t", "7000"}, true, Args{Command: COMMAND_ON, Temperature: n(7000)}},
		{"temperature above max", []string{"on", "-t", "7001"}, false, Args{}},
	}

	for _, tt := range tests {
		got, problem := parseArgs(tt.args)
		if ok := problem == ""; ok != tt.ok {
			t.Errorf("%s: problem = %q, want ok = %v", tt.name, problem, tt.ok)
			continue
		}
		if !reflect.DeepEqual(got, tt.want) {
			t.Errorf("%s: args = %+v, want %+v", tt.name, got, tt.want)
		}
	}
}

func TestRunExitCodes(t *testing.T) {
	tests := []struct {
		name string
		args []string
		want int
	}{
		{"help", []string{"help"}, 0},
		{"version", []string{"version"}, 0},
		{"-h", []string{"-h"}, 0},
		{"--version", []string{"--version"}, 2},
		{"unknown command", []string{"bogus"}, 2},
		{"-b on status", []string{"status", "-b", "50"}, 2},
		{"brightness out of range", []string{"on", "-b", "1"}, 2},
		{"temperature out of range", []string{"on", "-t", "9000"}, 2},
		{"missing flag value", []string{"on", "-b"}, 2},
		{"stray positional", []string{"off", "left"}, 2},
	}

	for _, tt := range tests {
		if got := run(tt.args, io.Discard, io.Discard); got != tt.want {
			t.Errorf("%s: run(%v) = %d, want %d", tt.name, tt.args, got, tt.want)
		}
	}
}
