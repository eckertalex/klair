package klair


MIRED_MIN :: 143 // 7000K
MIRED_MAX :: 344 // 2900K

round_div :: proc(numerator, denominator: int) -> int {
	assert(denominator > 0)
	return (numerator + denominator / 2) / denominator
}

kelvin_from_mired :: proc(mired: int) -> int {
	assert(mired > 0)
	return round_div(1_000_000, mired * 50) * 50
}

// Clamp to the light’s supported range.
mired_from_kelvin :: proc(kelvin: int) -> int {
	assert(kelvin > 0)
	return clamp(round_div(1_000_000, kelvin), MIRED_MIN, MIRED_MAX)
}
