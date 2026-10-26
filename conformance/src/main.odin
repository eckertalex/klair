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
serve  runs the lights and prints every request they get`

main :: proc() {
    args := os.args[1:]
    if len(args) == 0 {
        fmt.eprintln(USAGE)
        os.exit(2)
    }

    switch args[0] {
    case "check":
        options, ok := parse_check_args(args[1:])
        if !ok {
            fmt.eprintln(USAGE)
            os.exit(2)
        }

        start_lights()
        os.exit(check(options) ? 0 : 1)
    case "serve":
        if len(args) > 1 {
            fmt.eprintln(USAGE)
            os.exit(2)
        }

        server.echo = true
        start_lights()
        fmt.println("fake lights running")

        for {
            time.sleep(time.Hour)
        }
    case:
        fmt.eprintln(USAGE)
        os.exit(2)
    }
}

