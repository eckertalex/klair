package conformance

import "core:fmt"
import "core:os"
import "core:path/filepath"
import "core:slice"
import "core:strings"
import "core:text/regex"
import "core:time"

// The checker: runs each case against each klair binary and reports every
// difference from what the case expects.

Check_Options :: struct {
	quick:  bool, // skip slow cases
	filter: string, // only cases whose name contains this
	klairs: []string,
}

// Files klair sees. HOME is an empty folder of its own, so an implementation
// that ignores XDG_CONFIG_HOME finds no config instead of passing by accident.
Klair_Env :: struct {
	config_path: string, // $CONFIG in expected output
	env:         []string,
}

Totals :: struct {
	passed, failed, skipped: int,
}

parse_check_args :: proc(args: []string) -> (options: Check_Options, ok: bool) {
	klairs: [dynamic]string
	for i := 0; i < len(args); i += 1 {
		switch {
		case args[i] == "-q":
			options.quick = true
		case args[i] == "-run" && i + 1 < len(args):
			i += 1
			options.filter = args[i]
		case strings.has_prefix(args[i], "-"):
			delete(klairs)
			return {}, false
		case:
			append(&klairs, args[i])
		}
	}
	if len(klairs) == 0 {
		return {}, false
	}
	options.klairs = klairs[:]
	return options, true
}

// One klair after the other, since they share the lights
check :: proc(options: Check_Options) -> (all_passed: bool) {
	dir, err := os.make_directory_temp("", "conformance-*", context.allocator)
	ensure(err == nil, "cannot create a temporary folder")
	defer os.remove_all(dir)

	home, _ := filepath.join({dir, "home"}, context.allocator)
	config_dir, _ := filepath.join({dir, "config"}, context.allocator)
	config_path, _ := filepath.join({config_dir, "klair"}, context.allocator)
	os.make_directory(home)
	os.make_directory(config_dir)
	klair_env := Klair_Env {
		config_path = config_path,
		env         = {
			fmt.aprintf("HOME=%s", home),
			fmt.aprintf("XDG_CONFIG_HOME=%s", config_dir),
			fmt.aprintf("APPDATA=%s", config_dir),
		},
	}

	totals := make([]Totals, len(options.klairs))
	for klair, i in options.klairs {
		fmt.printfln("=== %s", klair)
		totals[i] = check_klair(klair, options, klair_env)
		fmt.printfln("%s\n", summary(totals[i]))
	}

	all_passed = true
	for t, i in totals {
		if len(totals) > 1 {
			fmt.printfln("%-20s %s", options.klairs[i], summary(t))
		}
		all_passed &&= t.failed == 0
	}
	return all_passed
}

check_klair :: proc(klair: string, options: Check_Options, klair_env: Klair_Env) -> (totals: Totals) {
	for c in CASES {
		if !strings.contains(c.name, options.filter) {
			continue
		}
		if options.quick && c.slow {
			totals.skipped += 1
			continue
		}

		problems := run_case(c, klair, klair_env)
		if len(problems) == 0 {
			fmt.printfln("ok   %s", c.name)
			totals.passed += 1
		} else {
			fmt.printfln("FAIL %s", c.name)
			for problem in problems {
				fmt.println(indent(problem, "     "))
			}
			totals.failed += 1
		}
		free_all(context.temp_allocator)
	}
	return totals
}

// Everything that differs from what the case expects, empty if nothing does
run_case :: proc(c: Case, klair: string, klair_env: Klair_Env) -> []string {
	problems: [dynamic]string
	problems.allocator = context.temp_allocator

	reset_lights()
	os.remove(klair_env.config_path)
	if !c.no_config {
		config := c.config if c.config != "" else DEFAULT_CONFIG
		err := os.write_entire_file(klair_env.config_path, config)
		ensure(err == nil, "cannot write the config")
	}

	start := time.tick_now()
	for run, i in c.runs {
		label := fmt.tprintf("run %d ", i + 1) if len(c.runs) > 1 else ""
		command := make([dynamic]string, context.temp_allocator)
		append(&command, klair)
		append(&command, ..run.args)

		state, stdout_bytes, stderr_bytes, err := os.process_exec(
			{command = command[:], env = klair_env.env},
			context.temp_allocator,
		)
		if err != nil {
			append(&problems, fmt.tprintf("%scannot run %s: %v", label, klair, err))
			continue
		}
		stdout := string(stdout_bytes)
		// The spec has debug builds print this first. Release builds don't.
		stderr := strings.trim_prefix(string(stderr_bytes), "[debug build]\n")

		if state.exit_code != run.exit {
			append(&problems, fmt.tprintf("%sexit code %d, expected %d", label, state.exit_code, run.exit))
		}
		if run.stdout_pattern != "" {
			if !is_one_matching_line(stdout, run.stdout_pattern) {
				append(&problems, fmt.tprintf("%sstdout doesn't match %s:\n%s", label, run.stdout_pattern, stdout))
			}
		} else if stdout != run.stdout {
			append(&problems, fmt.tprintf("%sstdout:\n%s", label, diff(run.stdout, stdout)))
		}
		want_stderr, _ := strings.replace_all(run.stderr, "$CONFIG", klair_env.config_path, context.temp_allocator)
		if stderr != want_stderr {
			append(&problems, fmt.tprintf("%sstderr:\n%s", label, diff(want_stderr, stderr)))
		}
	}
	elapsed := time.tick_since(start)

	want_log := slice.clone(c.log, context.temp_allocator)
	slice.sort(want_log)
	got_log := light_log()
	if !slice.equal(want_log, got_log) {
		append(&problems, fmt.tprintf("log:\n%s", diff_lines(want_log, got_log)))
	}

	if c.max_time > 0 && (elapsed < c.min_time || elapsed > c.max_time) {
		append(&problems, fmt.tprintf("took %v, expected %v to %v", elapsed, c.min_time, c.max_time))
	}
	return problems[:]
}

summary :: proc(t: Totals) -> string {
	return fmt.tprintf("%d passed, %d failed, %d skipped", t.passed, t.failed, t.skipped)
}

// For the version string: exactly one line, matching the pattern
is_one_matching_line :: proc(text, pattern: string) -> bool {
	line := strings.trim_suffix(text, "\n")
	if line == text || strings.contains(line, "\n") {
		return false
	}
	re, err := regex.create(pattern, {}, context.temp_allocator, context.temp_allocator)
	ensure(err == nil, pattern)
	_, matched := regex.match(re, line, context.temp_allocator)
	return matched
}

indent :: proc(text, prefix: string) -> string {
	indented, _ := strings.replace_all(text, "\n", fmt.tprintf("\n%s", prefix), context.temp_allocator)
	return strings.concatenate({prefix, indented}, context.temp_allocator)
}
