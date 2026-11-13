package main

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func intPointer(n int64) *int64 {
	return &n
}

func TestParseArgs(t *testing.T) {
	tests := []struct {
		args []string
		want Args
	}{
		{nil, Args{Command: COMMAND_STATUS}},
		{[]string{"status"}, Args{Command: COMMAND_STATUS}},
		{[]string{"-l", "left"}, Args{Command: COMMAND_STATUS, LightName: "left", HasLightName: true}},
		{[]string{"-l", ""}, Args{Command: COMMAND_STATUS, HasLightName: true}},
		{[]string{"off", "-l", "left"}, Args{Command: COMMAND_OFF, LightName: "left", HasLightName: true}},
		{[]string{"on", "-b", "50", "-t", "4700", "-l", "left"}, Args{Command: COMMAND_ON, LightName: "left", HasLightName: true, Brightness: intPointer(50), Temperature: intPointer(4700)}},
		{[]string{"on", "-b", "3"}, Args{Command: COMMAND_ON, Brightness: intPointer(3)}},
		{[]string{"on", "-b", "+50"}, Args{Command: COMMAND_ON, Brightness: intPointer(50)}},
		{[]string{"on", "-t", "7000"}, Args{Command: COMMAND_ON, Temperature: intPointer(7000)}},
		{[]string{"on", "-b", "10", "-b", "60"}, Args{Command: COMMAND_ON, Brightness: intPointer(60)}},
		{[]string{"status", "-l", "-b"}, Args{Command: COMMAND_STATUS, LightName: "-b", HasLightName: true}},
		{[]string{"help"}, Args{Command: COMMAND_HELP}},
		{[]string{"version"}, Args{Command: COMMAND_VERSION}},
	}

	for _, tt := range tests {
		got, problem := parseArgs(tt.args)
		if problem != "" {
			t.Errorf("parseArgs(%q): %s", tt.args, problem)
			continue
		}
		same := got.Command == tt.want.Command && got.LightName == tt.want.LightName && got.HasLightName == tt.want.HasLightName &&
			sameInt(got.Brightness, tt.want.Brightness) && sameInt(got.Temperature, tt.want.Temperature)
		if !same {
			t.Errorf("parseArgs(%q) = %+v, want %+v", tt.args, got, tt.want)
		}
	}
}

func sameInt(a, b *int64) bool {
	if a == nil || b == nil {
		return a == b
	}
	return *a == *b
}

func TestUsageErrors(t *testing.T) {
	tests := []struct {
		args string
		want string
	}{
		{"bogus", "unknown command: bogus"},
		{"status -b 50", "unknown flag for status: -b"},
		{"-b 50", "unknown flag for status: -b"},
		{"off -x", "unknown flag for off: -x"},
		{"on -b=50", "unknown flag for on: -b=50"},
		{"on --b 50", "unknown flag for on: --b"},
		{"-h", "unknown flag for status: -h"},
		{"--help", "unknown flag for status: --help"},
		{"version -l normal", "unknown flag for version: -l"},
		{"status extra", "unexpected argument: extra"},
		{"help me", "unexpected argument: me"},
		{"on -b", "missing value for -b"},
		{"status -l", "missing value for -l"},
		{"on -b 12x", "invalid value for -b: 12x"},
		{"on -t -", "invalid value for -t: -"},
		{"on -b 2", "invalid brightness: must be between 3 and 100"},
		{"on -b -5", "invalid brightness: must be between 3 and 100"},
		{"on -b 4294967346", "invalid brightness: must be between 3 and 100"},
		{"on -b 18446744073709551666", "invalid value for -b: 18446744073709551666"},
		{"on -b 99999999999999999999", "invalid value for -b: 99999999999999999999"},
		{"on -t -99999999999999999999", "invalid value for -t: -99999999999999999999"},
		{"on -t 9000", "invalid temperature: must be between 2900K and 7000K"},
		{"on -t 2899", "invalid temperature: must be between 2900K and 7000K"},
		{"on -b 2 -x", "unknown flag for on: -x"},
		{"on -t 9000 -b 2", "invalid brightness: must be between 3 and 100"},
	}

	for _, tt := range tests {
		if _, problem := parseArgs(strings.Fields(tt.args)); problem != tt.want {
			t.Errorf("parseArgs(%q) = %q, want %q", tt.args, problem, tt.want)
		}
	}
}

type RunResult struct {
	stdout, stderr string
	code           int
}

func runIn(t *testing.T, dir string, args ...string) RunResult {
	t.Setenv("XDG_CONFIG_HOME", dir)
	t.Setenv("HOME", dir)

	var stdout, stderr strings.Builder
	code := run(args, &stdout, &stderr)
	return RunResult{stdout.String(), stderr.String(), code}
}

func writeConfig(t *testing.T, text string) string {
	dir := t.TempDir()
	if err := os.WriteFile(filepath.Join(dir, "klair"), []byte(text), 0o644); err != nil {
		t.Fatal(err)
	}
	return dir
}

func configFor(lights ...Light) string {
	var text strings.Builder
	for _, light := range lights {
		text.WriteString(light.Name + " = " + light.Address + "\n")
	}
	return text.String()
}

func expectRun(t *testing.T, got RunResult, want RunResult) {
	t.Helper()
	if got != want {
		t.Errorf("got exit %d\nstdout:\n%s\nstderr:\n%s\nwant exit %d\nstdout:\n%s\nstderr:\n%s",
			got.code, got.stdout, got.stderr, want.code, want.stdout, want.stderr)
	}
}

func statusBlock(name, power string, brightness, kelvin int) string {
	return fmt.Sprintf("Status of light \"%s\":\n  Power:       %s\n  Brightness:  %d%%\n  Temperature: %dK\n",
		name, power, brightness, kelvin)
}

func TestRunEveryLightError(t *testing.T) {
	normal := startFakeLight(t, "normal", BEHAVIOR_NORMAL)
	nolen := startFakeLight(t, "nolen", BEHAVIOR_NO_LENGTH)
	error500 := startFakeLight(t, "error500", BEHAVIOR_ERROR_500)
	garbage := startFakeLight(t, "garbage", BEHAVIOR_GARBAGE)
	hangup := startFakeLight(t, "hangup", BEHAVIOR_HANGUP)
	refused := refusedLight(t, "refused")

	dir := writeConfig(t, configFor(normal.Light, nolen.Light, error500.Light, garbage.Light, hangup.Light, refused))

	expectRun(t, runIn(t, dir, "status"), RunResult{
		stdout: statusBlock("normal", "ON", 20, 4700) +
			statusBlock("nolen", "ON", 20, 4700) +
			"Status of light \"error500\": error: unexpected status 500\n" +
			"Status of light \"garbage\": error: invalid response\n" +
			"Status of light \"hangup\": error: connection closed unexpectedly\n" +
			"Status of light \"refused\": error: failed to connect\n",
		code: 1,
	})

	for _, light := range []*FakeLight{error500, garbage, hangup} {
		if n := len(requestsOf(light)); n != 3 {
			t.Errorf("%s got %d requests, want 3", light.Light.Name, n)
		}
	}

	expectRun(t, runIn(t, dir, "off", "-l", "garbage"), RunResult{
		stdout: "Update of light \"garbage\": error: invalid response\n",
		code:   1,
	})
}

func TestRunOnAndOff(t *testing.T) {
	normal := startFakeLight(t, "normal", BEHAVIOR_NORMAL)
	nolen := startFakeLight(t, "nolen", BEHAVIOR_NO_LENGTH)
	flaky := startFakeLight(t, "flaky", BEHAVIOR_FLAKY)
	dir := writeConfig(t, configFor(normal.Light, nolen.Light, flaky.Light))

	expectRun(t, runIn(t, dir, "on", "-b", "50", "-t", "4700", "-l", "normal"), RunResult{
		stdout: statusBlock("normal", "ON", 50, 4700),
	})
	expectRun(t, runIn(t, dir, "on", "-t", "2900", "-l", "normal"), RunResult{
		stdout: statusBlock("normal", "ON", 50, 2900),
	})
	expectRun(t, runIn(t, dir, "on", "-l", "normal"), RunResult{
		stdout: statusBlock("normal", "ON", 50, 2900),
	})
	expectRun(t, runIn(t, dir, "off", "-l", "nolen"), RunResult{
		stdout: statusBlock("nolen", "OFF", 20, 4700),
	})
	expectRun(t, runIn(t, dir), RunResult{
		stdout: statusBlock("normal", "ON", 50, 2900) +
			statusBlock("nolen", "OFF", 20, 4700) +
			statusBlock("flaky", "ON", 20, 4700),
	})

	want := []string{
		`PUT {"lights":[{"on":1,"brightness":50,"temperature":213}]}`,
		`PUT {"lights":[{"on":1,"temperature":344}]}`,
		`PUT {"lights":[{"on":1}]}`,
		`GET`,
	}
	got := requestsOf(normal)
	if strings.Join(got, "\n") != strings.Join(want, "\n") {
		t.Errorf("normal got requests\n%s\nwant\n%s", strings.Join(got, "\n"), strings.Join(want, "\n"))
	}
	if got := requestsOf(nolen); len(got) == 0 || got[0] != `PUT {"lights":[{"on":0}]}` {
		t.Errorf("nolen got requests %q", got)
	}
	if got := requestsOf(flaky); strings.Join(got, ",") != "dropped,GET" {
		t.Errorf("flaky got requests %q", got)
	}
	for _, light := range []*FakeLight{normal, nolen, flaky} {
		if problems := problemsOf(light); len(problems) != 0 {
			t.Errorf("%s: requests broke the spec: %v", light.Light.Name, problems)
		}
	}
}

func TestRunConfigErrors(t *testing.T) {
	empty := t.TempDir()
	path := filepath.Join(empty, "klair")

	expectRun(t, runIn(t, empty, "status"), RunResult{stderr: "cannot read config: " + path + "\n", code: 1})
	expectRun(t, runIn(t, empty, "help"), RunResult{stdout: USAGE})
	expectRun(t, runIn(t, empty, "version"), RunResult{stdout: buildVersionFull() + "\n"})
	expectRun(t, runIn(t, empty, "bogus"), RunResult{stderr: "unknown command: bogus\n\n" + USAGE, code: 2})
	expectRun(t, runIn(t, empty, "on", "-b", "2"), RunResult{stderr: "invalid brightness: must be between 3 and 100\n\n" + USAGE, code: 2})

	tests := []struct {
		config string
		want   string
	}{
		{"# only a comment\n", "no lights in config: %s"},
		{"left 127.0.0.1:19123\n", "invalid config %s: line 1: expected name = address"},
		{"left = 127.0.0.1:1\n# x\n\nleft = 127.0.0.1:2\n", "invalid config %s: line 4: duplicate name \"left\""},
	}
	for _, tt := range tests {
		dir := writeConfig(t, tt.config)
		want := strings.Replace(tt.want, "%s", filepath.Join(dir, "klair"), 1) + "\n"
		expectRun(t, runIn(t, dir, "status"), RunResult{stderr: want, code: 1})
	}

	dir := writeConfig(t, "normal = 127.0.0.1:19123\nnolen = 127.0.0.1:19124\n")
	expectRun(t, runIn(t, dir, "status", "-l", "nope"), RunResult{
		stderr: "light \"nope\" not found; available: normal, nolen\n",
		code:   1,
	})
}

func TestVersionString(t *testing.T) {
	tests := []struct {
		hash, debug string
		want        string
	}{
		{"8b30b7a", "false", "klair version 0.4.0+8b30b7a (go)"},
		{"8b30b7a-dirty", "false", "klair version 0.4.0+8b30b7a-dirty (go)"},
		{"8b30b7a-dirty", "true", "klair version 0.4.0+8b30b7a-dirty (go, debug)"},
		{"", "true", "klair version 0.4.0 (go, debug)"},
		{"", "false", "klair version 0.4.0 (go)"},
	}

	hash, debug := BUILD_GIT_HASH, BUILD_DEBUG
	defer func() { BUILD_GIT_HASH, BUILD_DEBUG = hash, debug }()

	for _, tt := range tests {
		BUILD_GIT_HASH, BUILD_DEBUG = tt.hash, tt.debug
		if got := buildVersionFull(); got != tt.want {
			t.Errorf("buildVersionFull() = %q, want %q", got, tt.want)
		}
	}
}
