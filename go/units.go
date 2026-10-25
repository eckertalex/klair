package main

// The Elgato wire protocol speaks mired, the CLI speaks Kelvin.

// Adding half the divisor first makes the division round instead of truncate
func roundDiv(numerator, denominator int) int {
	return (numerator + denominator/2) / denominator
}

// For display, rounded to the nearest 50K. Round before bucketing: mired 315
// truncates to 3150K, but the real value is 3174.6K, which rounds to 3200K.
func miredToKelvin(mired int) int {
	if mired <= 0 {
		panic("mired must be positive")
	}
	return roundDiv(roundDiv(1_000_000, mired), 50) * 50
}

// Sent straight to the light, so round to the nearest mired
func kelvinToMired(kelvin int) int {
	return roundDiv(1_000_000, kelvin)
}
