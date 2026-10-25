package klair

// The Elgato wire protocol speaks mired, the CLI speaks Kelvin.

// Adding half the divisor first makes the division round instead of truncate
round_div :: proc(numerator, denominator: int) -> int {
	return (numerator + denominator/2) / denominator
}

// For display, rounded to the nearest 50K. Round before bucketing: mired 315
// truncates to 3150K, but the real value is 3174.6K, which rounds to 3200K.
mired_to_kelvin :: proc(mired: int) -> int {
	assert(mired > 0, "mired must be positive")
	return round_div(round_div(1_000_000, mired), 50) * 50
}

// Sent straight to the light, so round to the nearest mired
kelvin_to_mired :: proc(kelvin: int) -> int {
	return round_div(1_000_000, kelvin)
}
