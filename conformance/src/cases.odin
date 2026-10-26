package conformance

import "core:time"

// What klair should do, from the CLI spec. Cases run in this order, each
// after a reset of the lights.

Case :: struct {
    name:      string,
    config:    string,        // "" is DEFAULT_CONFIG
    no_config: bool,
    runs:      []Run,
    log:       []string,      // every request the lights get, in any order. nil: none.
    slow:      bool,          // skipped by -q
    min_time:  time.Duration, // with max_time: how long all runs take together
    max_time:  time.Duration,
}

// One klair invocation
Run :: struct {
    args:           []string,
    exit:           int,
    stdout:         string,
    stdout_pattern: string, // instead of stdout: a regex its one line must match
    stderr:         string, // $CONFIG stands for the config file's path
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

	{name = "help", runs = {{args = {"help"}, stdout = KLAIR_USAGE}}},
	{name = "version", runs = {{args = {"version"}, stdout_pattern = VERSION_PATTERN}}},
}
