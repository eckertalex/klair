package klair

import vmem "core:mem/virtual"
import "core:net"
import "core:testing"

@(test)
config_trims_and_skips :: proc(t: ^testing.T) {
	arena: vmem.Arena
	context.allocator = test_allocator(&arena)
	defer vmem.arena_destroy(&arena)

	text := "# desk\r\n\r\n  left  = 192.168.2.164:9123\r\n\tright=[fe80::1]:9123  \n"
	lights, _, ok := lights_from_config(text)
	testing.expect(t, ok)
	testing.expect_value(t, len(lights), 2)
	testing.expect_value(t, lights[0].name, "left")
	testing.expect_value(t, lights[0].host, "192.168.2.164")
	testing.expect_value(t, lights[0].endpoint.port, 9123)
	testing.expect_value(t, lights[0].endpoint.address, net.Address(net.IP4_Address{192, 168, 2, 164}))
	testing.expect_value(t, lights[1].name, "right")
	testing.expect_value(t, lights[1].address, "[fe80::1]:9123")
	testing.expect_value(t, lights[1].host, "[fe80::1]")
}

@(test)
config_reports_the_first_problem_with_its_line :: proc(t: ^testing.T) {
	arena: vmem.Arena
	context.allocator = test_allocator(&arena)
	defer vmem.arena_destroy(&arena)

	Case :: struct {
		text:    string,
		line:    int,
		message: string,
	}
	cases := [?]Case {
		{"left 127.0.0.1:19123", 1, "expected name = address"},
		{" = 127.0.0.1:19123", 1, "empty name"},
		{"my light = 127.0.0.1:19123", 1, `name "my light" contains whitespace`},
		{"left =", 1, "empty address"},
		{"left = lights.local:9123", 1, `invalid address "lights.local:9123"`},
		{"left = 127.0.0.1:0", 1, `invalid address "127.0.0.1:0"`},
		{"left = 127.0.0.1", 1, `invalid address "127.0.0.1"`},
		{"left = 127.0.0.1:65536", 1, `invalid address "127.0.0.1:65536"`},
		{"left = [::1]", 1, `invalid address "[::1]"`},
		{"left = 127.0.0.1:1\n# x\n\nleft = 127.0.0.1:2", 4, `duplicate name "left"`},
		{"= nope", 1, "empty name"},
	}
	for c in cases {
		_, err, ok := lights_from_config(c.text)
		testing.expect(t, !ok, c.text)
		testing.expect_value(t, err.line, c.line)
		testing.expect_value(t, err.message, c.message)
	}
}

@(test)
config_without_lights_is_not_an_error_here :: proc(t: ^testing.T) {
	arena: vmem.Arena
	context.allocator = test_allocator(&arena)
	defer vmem.arena_destroy(&arena)

	lights, _, ok := lights_from_config("# only a comment\n")
	testing.expect(t, ok)
	testing.expect_value(t, len(lights), 0)
}
