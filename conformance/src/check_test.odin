package conformance

import "core:testing"

@(test)
diff_marks_each_line :: proc(t: ^testing.T) {
	defer free_all(context.temp_allocator)

	testing.expect_value(t, diff("a\nb\nc\n", "a\nx\nc\n"), "  a\n- b\n+ x\n  c")
	testing.expect_value(t, diff("a\n", ""), "- a")
	testing.expect_value(t, diff("a\n", "a"), "- a\n+ a (no newline at end)")
	testing.expect_value(t, diff("a\n", "a\r\n"), "- a\n+ a\\r")
}

@(test)
check_args :: proc(t: ^testing.T) {
	options, ok := parse_check_args({"-q", "-run", "config", "c/build/klair", "go/build/klair"})
	defer delete(options.klairs)
	testing.expect(t, ok)
	testing.expect(t, options.quick)
	testing.expect_value(t, options.filter, "config")
	testing.expect_value(t, len(options.klairs), 2)

	_, ok = parse_check_args({"-q"})
	testing.expect(t, !ok, "no klair")
	_, ok = parse_check_args({"-x", "go/build/klair"})
	testing.expect(t, !ok, "unknown flag")
	_, ok = parse_check_args({"go/build/klair", "-run"})
	testing.expect(t, !ok, "-run without a value")
}

@(test)
cases_are_well_formed :: proc(t: ^testing.T) {
	seen: map[string]bool
	defer delete(seen)
	for c in CASES {
		testing.expectf(t, !seen[c.name], "%s: name used twice", c.name)
		seen[c.name] = true
		testing.expectf(t, len(c.runs) > 0, "%s: no runs", c.name)
		testing.expectf(t, !(c.no_config && c.config != ""), "%s: config and no_config", c.name)
	}
}
