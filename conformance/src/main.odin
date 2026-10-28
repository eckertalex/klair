// Conformance checks for klair: fake Elgato lights on localhost, and a
// checker that runs klair binaries against them and compares what they do
// with the CLI spec.
package conformance

import "core:fmt"
import "core:os"
import "core:time"

USAGE :: `usage: conformance check [-q] [-run TEXT] <klair>...
       conformance serve

check  runs every case against each klair binary, exits 0 if all pass
       -q        skip slow cases
       -run TEXT only cases whose name contains TEXT
serve  runs the lights until Ctrl-C and prints every request they get`

main :: proc() {
	args := os.args[1:]
	if len(args) == 0 {
		fail_usage()
	}

	switch args[0] {
	case "check":
		options, ok := parse_check_args(args[1:])
		if !ok {
			fail_usage()
		}
		start_lights()
		os.exit(check(options) ? 0 : 1)
	case "serve":
		if len(args) > 1 {
			fail_usage()
		}
		server.echo = true
		start_lights()
		fmt.println("lights running, Ctrl-C to stop")
		for {
			time.sleep(time.Hour)
		}
	case:
		fail_usage()
	}
}

fail_usage :: proc() -> ! {
	fmt.eprintln(USAGE)
	os.exit(2)
}
