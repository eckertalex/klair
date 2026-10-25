package klair

import "core:testing"

@(test)
kelvin_and_mired_round_trip :: proc(t: ^testing.T) {
	testing.expect_value(t, kelvin_to_mired(4700), 213)
	testing.expect_value(t, mired_to_kelvin(213), 4700)
	// 315 mired is about 3174.6 K, which rounds to 3200 K.
	testing.expect_value(t, mired_to_kelvin(315), 3200)
}
