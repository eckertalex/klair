package conformance

import "core:strings"

// A line diff for failure reports: "- " only expected, "+ " only actual,
// "  " both. Outputs are a few lines, so every line is shown.

diff :: proc(want, got: string) -> string {
	return diff_lines(lines_of(want), lines_of(got))
}

diff_lines :: proc(want, got: []string) -> string {
	// lcs[i][j] is the longest common subsequence of want[i:] and got[j:]
	n, m := len(want), len(got)
	lcs := make([][]int, n + 1, context.temp_allocator)
	for &row in lcs {
		row = make([]int, m + 1, context.temp_allocator)
	}
	for i := n - 1; i >= 0; i -= 1 {
		for j := m - 1; j >= 0; j -= 1 {
			lcs[i][j] = lcs[i + 1][j + 1] + 1 if want[i] == got[j] else max(lcs[i + 1][j], lcs[i][j + 1])
		}
	}

	b := strings.builder_make(context.temp_allocator)
	i, j := 0, 0
	for i < n || j < m {
		switch {
		case i < n && j < m && want[i] == got[j]:
			write_line(&b, "  ", want[i])
			i += 1
			j += 1
		case i < n && (j == m || lcs[i + 1][j] >= lcs[i][j + 1]):
			write_line(&b, "- ", want[i])
			i += 1
		case:
			write_line(&b, "+ ", got[j])
			j += 1
		}
	}
	return strings.trim_suffix(strings.to_string(b), "\n")
}

// A last line without "\n" is marked, so a missing newline shows up
@(private = "file")
lines_of :: proc(text: string) -> []string {
	if text == "" {
		return nil
	}
	lines := strings.split(strings.trim_suffix(text, "\n"), "\n", context.temp_allocator)
	if !strings.has_suffix(text, "\n") {
		last := &lines[len(lines) - 1]
		last^ = strings.concatenate({last^, " (no newline at end)"}, context.temp_allocator)
	}
	return lines
}

// \r is invisible in a terminal, so it's written out
@(private = "file")
write_line :: proc(b: ^strings.Builder, marker, line: string) {
	strings.write_string(b, marker)
	for c in transmute([]u8)line {
		if c == '\r' {
			strings.write_string(b, `\r`)
		} else {
			strings.write_byte(b, c)
		}
	}
	strings.write_byte(b, '\n')
}
