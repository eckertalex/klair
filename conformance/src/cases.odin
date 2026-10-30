package conformance

import "core:time"

// What klair should do, from the CLI spec. Cases run in this order, each
// after a reset of the lights.

Case :: struct {
	name:      string,
	config:    string, // "" is DEFAULT_CONFIG
	no_config: bool,
	runs:      []Run,
	log:       []string, // every request the lights get, in any order. nil: none.
	slow:      bool, // skipped by -q
	min_time:  time.Duration, // with max_time: how long all runs take together
	max_time:  time.Duration,
}

// One klair invocation
Run :: struct {
	args:            []string,
	exit:            int,
	stdout:          string,
	stdout_pattern:  string, // instead of stdout: a regex its one line must match
	stderr:          string, // $CONFIG stands for the config file's path
	stderr_mentions: string, // instead of stderr: a usage error in the flag parser's own words, naming this
}

DEFAULT_CONFIG :: `normal = 127.0.0.1:19123
nolen  = 127.0.0.1:19124
flaky  = 127.0.0.1:19125
`

KLAIR_USAGE :: `Usage: klair [command] [flags]

Commands:
  status   [-l NAME]
  on       [-b 3-100] [-t 2900-7000] [-l NAME]
  off      [-l NAME]
  help
  version

No command: same as status.
`

VERSION_PATTERN :: `^klair version [0-9]+\.[0-9]+\.[0-9]+(\+[0-9a-f]+(-dirty)?)? \((c|odin|go)(, debug)?\)$`

NORMAL_ON :: `Status of light "normal":
  Power:       ON
  Brightness:  20%
  Temperature: 4700K
`

NOLEN_ON :: `Status of light "nolen":
  Power:       ON
  Brightness:  20%
  Temperature: 4700K
`

NOLEN_OFF :: `Status of light "nolen":
  Power:       OFF
  Brightness:  20%
  Temperature: 4700K
`

FLAKY_ON :: `Status of light "flaky":
  Power:       ON
  Brightness:  20%
  Temperature: 4700K
`

// The misbehaving lights, and one with nothing listening
ERROR_LIGHTS_CONFIG :: `normal   = 127.0.0.1:19123
trickle  = 127.0.0.1:19126
error500 = 127.0.0.1:19127
garbage  = 127.0.0.1:19128
zerotemp = 127.0.0.1:19129
hangup   = 127.0.0.1:19130
refused  = 127.0.0.1:1
`

CASES := []Case {
	// --- Commands and output ---

	{
		name = "no-command",
		runs = {{stdout = NORMAL_ON + NOLEN_ON + FLAKY_ON}},
		log = {"normal GET", "nolen GET", "flaky dropped", "flaky GET"},
	},
	{
		name = "status",
		runs = {{args = {"status"}, stdout = NORMAL_ON + NOLEN_ON + FLAKY_ON}},
		log = {"normal GET", "nolen GET", "flaky dropped", "flaky GET"},
	},
	{
		name = "one-light",
		runs = {{args = {"status", "-l", "nolen"}, stdout = NOLEN_ON}},
		log = {"nolen GET"},
	},
	{
		name = "flags-without-command",
		runs = {{args = {"-l", "normal"}, stdout = NORMAL_ON}},
		log = {"normal GET"},
	},
	{
		name = "on-brightness-temperature",
		runs = {{
			args = {"on", "-b", "50", "-t", "4700", "-l", "normal"},
			stdout = `Status of light "normal":
  Power:       ON
  Brightness:  50%
  Temperature: 4700K
`,
		}},
		log = {`normal PUT {"lights":[{"on":1,"brightness":50,"temperature":213}]}`},
	},
	{
		name = "on-without-flags",
		runs = {{args = {"on", "-l", "normal"}, stdout = NORMAL_ON}},
		log = {`normal PUT {"lights":[{"on":1}]}`},
	},
	{
		name = "off",
		runs = {{args = {"off", "-l", "nolen"}, stdout = NOLEN_OFF}},
		log = {`nolen PUT {"lights":[{"on":0}]}`},
	},
	{
		name = "off-leaves-others-alone",
		runs = {
			{args = {"off", "-l", "nolen"}, stdout = NOLEN_OFF},
			{args = {"status"}, stdout = NORMAL_ON + NOLEN_OFF + FLAKY_ON},
		},
		log = {`nolen PUT {"lights":[{"on":0}]}`, "normal GET", "nolen GET", "flaky dropped", "flaky GET"},
	},
	{
		name = "retry",
		runs = {{args = {"status", "-l", "flaky"}, stdout = FLAKY_ON}},
		log = {"flaky dropped", "flaky GET"},
	},
	{
		// 2900K is mired 345, one past what the light takes
		name = "clamp",
		runs = {{
			args = {"on", "-t", "2900", "-l", "normal"},
			stdout = `Status of light "normal":
  Power:       ON
  Brightness:  20%
  Temperature: 2900K
`,
		}},
		log = {`normal PUT {"lights":[{"on":1,"temperature":344}]}`},
	},
	{
		name = "rounding",
		runs = {{
			args = {"on", "-t", "3150", "-l", "normal"},
			stdout = `Status of light "normal":
  Power:       ON
  Brightness:  20%
  Temperature: 3150K
`,
		}},
		log = {`normal PUT {"lights":[{"on":1,"temperature":317}]}`},
	},
	{
		// Mired 315 is 3174.6K: 3150K rounded once, 3200K rounded twice
		name = "rounding-once",
		runs = {{
			args = {"on", "-t", "3175", "-l", "normal"},
			stdout = `Status of light "normal":
  Power:       ON
  Brightness:  20%
  Temperature: 3150K
`,
		}},
		log = {`normal PUT {"lights":[{"on":1,"temperature":315}]}`},
	},
	{
		name = "last-flag-wins",
		runs = {{
			args = {"on", "-b", "10", "-b", "60", "-l", "normal"},
			stdout = `Status of light "normal":
  Power:       ON
  Brightness:  60%
  Temperature: 4700K
`,
		}},
		log = {`normal PUT {"lights":[{"on":1,"brightness":60}]}`},
	},
	{name = "help", runs = {{args = {"help"}, stdout = KLAIR_USAGE}}},
	{name = "version", runs = {{args = {"version"}, stdout_pattern = VERSION_PATTERN}}},

	// --- Usage errors: exit 2, the message and the usage text on stderr ---

	{name = "unknown-command", runs = {{args = {"bogus"}, exit = 2, stderr = "unknown command: bogus\n\n" + KLAIR_USAGE}}},
	{name = "flag-for-another-command", runs = {{args = {"status", "-b", "50"}, exit = 2, stderr_mentions = "-b"}}},
	{name = "flag-for-default-command", runs = {{args = {"-b", "50"}, exit = 2, stderr_mentions = "-b"}}},
	{name = "unknown-flag", runs = {{args = {"off", "-x"}, exit = 2, stderr_mentions = "-x"}}},
	{name = "unexpected-argument", runs = {{args = {"status", "extra"}, exit = 2, stderr = "unexpected argument: extra\n\n" + KLAIR_USAGE}}},
	{name = "dash-v", runs = {{args = {"-v"}, exit = 2, stderr_mentions = "-v"}}},
	{name = "dash-version", runs = {{args = {"-version"}, exit = 2, stderr_mentions = "-version"}}},
	{name = "dash-dash-version", runs = {{args = {"--version"}, exit = 2, stderr_mentions = "-version"}}},
	{name = "help-with-argument", runs = {{args = {"help", "me"}, exit = 2, stderr = "unexpected argument: me\n\n" + KLAIR_USAGE}}},
	{name = "version-with-flag", runs = {{args = {"version", "-l", "normal"}, exit = 2, stderr_mentions = "-l"}}},
	{name = "missing-value", runs = {{args = {"on", "-b"}, exit = 2, stderr_mentions = "-b"}}},
	{name = "trailing-junk", runs = {{args = {"on", "-b", "12x"}, exit = 2, stderr_mentions = "12x"}}},
	{name = "wraps-to-valid", runs = {{args = {"on", "-b", "4294967346"}, exit = 2, stderr = "invalid brightness: must be between 3 and 100\n\n" + KLAIR_USAGE}}},
	{name = "brightness-range", runs = {{args = {"on", "-b", "2"}, exit = 2, stderr = "invalid brightness: must be between 3 and 100\n\n" + KLAIR_USAGE}}},
	{name = "too-big-for-64-bits", runs = {{args = {"on", "-b", "18446744073709551666"}, exit = 2, stderr_mentions = "18446744073709551666"}}},
	{name = "temperature-range", runs = {{args = {"on", "-t", "9000"}, exit = 2, stderr = "invalid temperature: must be between 2900K and 7000K\n\n" + KLAIR_USAGE}}},
	{name = "flag-before-range", runs = {{args = {"on", "-b", "2", "-x"}, exit = 2, stderr_mentions = "-x"}}},

	// --- Config ---

	{name = "no-config-help", no_config = true, runs = {{args = {"help"}, stdout = KLAIR_USAGE}}},
	{name = "no-config-version", no_config = true, runs = {{args = {"version"}, stdout_pattern = VERSION_PATTERN}}},
	{name = "no-config-usage-error", no_config = true, runs = {{args = {"bogus"}, exit = 2, stderr = "unknown command: bogus\n\n" + KLAIR_USAGE}}},
	{name = "no-config", no_config = true, runs = {{args = {"status"}, exit = 1, stderr = "cannot read config: $CONFIG\n"}}},
	{name = "only-comment", config = "# only a comment\n", runs = {{args = {"status"}, exit = 1, stderr = "no lights in config: $CONFIG\n"}}},
	{name = "missing-equals", config = "left 127.0.0.1:19123\n", runs = {{args = {"status"}, exit = 1, stderr = "invalid config $CONFIG: line 1: expected name = address\n"}}},
	{name = "empty-name", config = " = 127.0.0.1:19123\n", runs = {{args = {"status"}, exit = 1, stderr = "invalid config $CONFIG: line 1: empty name\n"}}},
	{name = "name-with-whitespace", config = "my light = 127.0.0.1:19123\n", runs = {{args = {"status"}, exit = 1, stderr = "invalid config $CONFIG: line 1: name \"my light\" contains whitespace\n"}}},
	{name = "empty-address", config = "left =\n", runs = {{args = {"status"}, exit = 1, stderr = "invalid config $CONFIG: line 1: empty address\n"}}},
	{name = "hostname", config = "left = lights.local:9123\n", runs = {{args = {"status"}, exit = 1, stderr = "invalid config $CONFIG: line 1: invalid address \"lights.local:9123\"\n"}}},
	{name = "port-zero", config = "left = 127.0.0.1:0\n", runs = {{args = {"status"}, exit = 1, stderr = "invalid config $CONFIG: line 1: invalid address \"127.0.0.1:0\"\n"}}},
	{name = "no-port", config = "left = 127.0.0.1\n", runs = {{args = {"status"}, exit = 1, stderr = "invalid config $CONFIG: line 1: invalid address \"127.0.0.1\"\n"}}},
	{
		name = "duplicate-name",
		config = "left = 127.0.0.1:19123\n# desk\n\nleft = 127.0.0.1:19124\n",
		runs = {{args = {"status"}, exit = 1, stderr = "invalid config $CONFIG: line 4: duplicate name \"left\"\n"}},
	},
	{
		name = "crlf",
		config = "normal = 127.0.0.1:19123\r\nnolen  = 127.0.0.1:19124\r\nflaky  = 127.0.0.1:19125\r\n",
		runs = {{args = {"status"}, stdout = NORMAL_ON + NOLEN_ON + FLAKY_ON}},
		log = {"normal GET", "nolen GET", "flaky dropped", "flaky GET"},
	},
	{
		name = "comments-and-empty-lines",
		config = "# desk lights\n\n\tnormal = 127.0.0.1:19123\t\n  # indented comment\n\n",
		runs = {{args = {"status"}, stdout = NORMAL_ON}},
		log = {"normal GET"},
	},
	{
		name = "unknown-light",
		runs = {{args = {"status", "-l", "nope"}, exit = 1, stderr = "light \"nope\" not found; available: normal, nolen, flaky\n"}},
	},

	// --- Light errors ---

	{
		name = "light-errors-off",
		config = ERROR_LIGHTS_CONFIG,
		runs = {{
			args = {"off"},
			exit = 1,
			stdout = `Status of light "normal":
  Power:       OFF
  Brightness:  20%
  Temperature: 4700K
Status of light "trickle":
  Power:       OFF
  Brightness:  20%
  Temperature: 4700K
Update of light "error500": error: unexpected status 500
Update of light "garbage": error: invalid response
Update of light "zerotemp": error: invalid response
Update of light "hangup": error: connection closed unexpectedly
Update of light "refused": error: failed to connect
`,
		}},
		log = {
			`normal PUT {"lights":[{"on":0}]}`,
			`trickle PUT {"lights":[{"on":0}]}`,
			`error500 PUT {"lights":[{"on":0}]}`, `error500 PUT {"lights":[{"on":0}]}`, `error500 PUT {"lights":[{"on":0}]}`,
			`garbage PUT {"lights":[{"on":0}]}`, `garbage PUT {"lights":[{"on":0}]}`, `garbage PUT {"lights":[{"on":0}]}`,
			`zerotemp PUT {"lights":[{"on":0}]}`, `zerotemp PUT {"lights":[{"on":0}]}`, `zerotemp PUT {"lights":[{"on":0}]}`,
			`hangup PUT {"lights":[{"on":0}]}`, `hangup PUT {"lights":[{"on":0}]}`, `hangup PUT {"lights":[{"on":0}]}`,
		},
	},
	{
		// silent takes 3 attempts of 3s plus 0.3s of backoff. If it takes
		// much longer, the lights wait on each other.
		name = "light-errors-with-timeout",
		config = ERROR_LIGHTS_CONFIG + "silent   = 127.0.0.1:19131\n",
		slow = true,
		min_time = 9 * time.Second,
		max_time = 10 * time.Second,
		runs = {{
			args = {"status"},
			exit = 1,
			stdout = NORMAL_ON + `Status of light "trickle":
  Power:       ON
  Brightness:  20%
  Temperature: 4700K
Status of light "error500": error: unexpected status 500
Status of light "garbage": error: invalid response
Status of light "zerotemp": error: invalid response
Status of light "hangup": error: connection closed unexpectedly
Status of light "refused": error: failed to connect
Status of light "silent": error: timed out
`,
		}},
		log = {
			"normal GET",
			"trickle GET",
			"error500 GET", "error500 GET", "error500 GET",
			"garbage GET", "garbage GET", "garbage GET",
			"zerotemp GET", "zerotemp GET", "zerotemp GET",
			"hangup GET", "hangup GET", "hangup GET",
			"silent GET", "silent GET", "silent GET",
		},
	},
}
