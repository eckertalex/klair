package main

// The Elgato wire protocol uses mireds; the CLI uses Kelvin.

func roundDiv(numerator, denominator int) int {
	assert(denominator > 0, "roundDiv denominator must be positive")
	return (numerator + denominator/2) / denominator
}

func miredToKelvin(mired int) int {
	assert(mired > 0, "mired must be positive")
	// Round to the nearest 50 K.
	return roundDiv(1_000_000, mired*50) * 50
}

func kelvinToMired(kelvin int) int {
	assert(kelvin > 0, "kelvin must be positive")
	return roundDiv(1_000_000, kelvin)
}
