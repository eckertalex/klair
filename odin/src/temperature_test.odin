package klair

import vmem "core:mem/virtual"
import "core:mem"
import "core:testing"

test_allocator :: proc(arena: ^vmem.Arena) -> mem.Allocator {
	ensure(vmem.arena_init_growing(arena) == nil)
	return vmem.arena_allocator(arena)
}

@(test)
kelvin_from_mired_rounds_once :: proc(t: ^testing.T) {
	Case :: struct {
		mired, kelvin: int,
	}
	// a common one and an exact tie, which rounds up
	cases := [?]Case{{213, 4700}, {181, 5500}, {221, 4500}, {226, 4400}, {292, 3400}, {315, 3150}, {320, 3150}}
	for c in cases {
		testing.expect_value(t, kelvin_from_mired(c.mired), c.kelvin)
	}
}

@(test)
mired_from_kelvin_rounds_and_clamps :: proc(t: ^testing.T) {
	testing.expect_value(t, mired_from_kelvin(4700), 213)
	testing.expect_value(t, mired_from_kelvin(3150), 317)
	testing.expect_value(t, mired_from_kelvin(7000), 143)
	testing.expect_value(t, mired_from_kelvin(2900), 344)
}

@(test)
every_50k_step_survives_the_round_trip :: proc(t: ^testing.T) {
	for kelvin := TEMPERATURE_MIN; kelvin <= TEMPERATURE_MAX; kelvin += 50 {
		testing.expect_value(t, kelvin_from_mired(mired_from_kelvin(kelvin)), kelvin)
	}
}
