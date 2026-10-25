package main

import "testing"

func TestParseFlags(t *testing.T) {
	tests := []struct {
		name    string
		command string
		args    []string
		ok      bool
		want    Flags
	}{
		{"status no flags", "status", nil, true, Flags{}},
		{"status light", "status", []string{"-l", "left"}, true, Flags{LightName: "left"}},
		{"off light", "off", []string{"-l", "left"}, true, Flags{LightName: "left"}},
		{"on all flags", "on", []string{"-b", "50", "-t", "4700", "-l", "left"}, true, Flags{"left", 50, 4700}},
		{"on equals form", "on", []string{"-b=50"}, true, Flags{Brightness: 50}},
		{"repeated -l last wins", "on", []string{"-l", "a", "-l", "b"}, true, Flags{LightName: "b"}},
		{"-l takes a flag-looking value", "status", []string{"-l", "-b"}, true, Flags{LightName: "-b"}},

		{"-b on status", "status", []string{"-b", "50"}, false, Flags{}},
		{"-t on off", "off", []string{"-t", "4700"}, false, Flags{}},
		{"missing value -l", "status", []string{"-l"}, false, Flags{}},
		{"missing value -b", "on", []string{"-b"}, false, Flags{}},
		{"trailing garbage in number", "on", []string{"-b", "12x"}, false, Flags{}},
		{"empty number", "on", []string{"-b", ""}, false, Flags{}},
		{"int32 overflow", "on", []string{"-t", "99999999999"}, false, Flags{}},
		{"int64 overflow", "on", []string{"-t", "99999999999999999999"}, false, Flags{}},
		{"unknown flag", "status", []string{"-x"}, false, Flags{}},
		{"stray positional", "on", []string{"50"}, false, Flags{}},

		{"brightness explicit 0", "on", []string{"-b", "0"}, false, Flags{}},
		{"brightness below min", "on", []string{"-b", "2"}, false, Flags{}},
		{"brightness at min", "on", []string{"-b", "3"}, true, Flags{Brightness: 3}},
		{"brightness at max", "on", []string{"-b", "100"}, true, Flags{Brightness: 100}},
		{"brightness above max", "on", []string{"-b", "101"}, false, Flags{}},
		{"temperature below min", "on", []string{"-t", "2899"}, false, Flags{}},
		{"temperature at min", "on", []string{"-t", "2900"}, true, Flags{Temperature: 2900}},
		{"temperature at max", "on", []string{"-t", "7000"}, true, Flags{Temperature: 7000}},
		{"temperature above max", "on", []string{"-t", "7001"}, false, Flags{}},
	}

	for _, tt := range tests {
		got, ok := parseFlags(tt.command, tt.args)
		if ok != tt.ok {
			t.Errorf("%s: ok = %v, want %v", tt.name, ok, tt.ok)
			continue
		}
		if ok && got != tt.want {
			t.Errorf("%s: flags = %+v, want %+v", tt.name, got, tt.want)
		}
	}
}

func TestRunExitCodes(t *testing.T) {
	tests := []struct {
		name string
		args []string
		want int
	}{
		{"help", []string{"-h"}, 0},
		{"version", []string{"-v"}, 0},
		{"unknown command", []string{"bogus"}, 1},
		{"-b on status", []string{"status", "-b", "50"}, 1},
		{"brightness out of range", []string{"on", "-b", "1"}, 1},
		{"temperature out of range", []string{"on", "-t", "9000"}, 1},
		{"missing flag value", []string{"on", "-b"}, 1},
		{"stray positional", []string{"off", "left"}, 1},
	}

	for _, tt := range tests {
		if got := run(tt.args); got != tt.want {
			t.Errorf("%s: run(%v) = %d, want %d", tt.name, tt.args, got, tt.want)
		}
	}
}
