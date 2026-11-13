package main


const (
	MIRED_MIN = 143 // 7000K
	MIRED_MAX = 344 // 2900K
)

func roundDiv(numerator, denominator int) int {
	assert(denominator > 0, "roundDiv denominator must be positive")
	return (numerator + denominator/2) / denominator
}

// Round once: mired 315 converts to 3175K, then 3200K.
func miredToKelvin(mired int) int {
	assert(mired > 0, "mired must be positive")
	return roundDiv(1_000_000, mired*50) * 50
}

// Clamp to the light’s supported range.
func kelvinToMired(kelvin int) int {
	assert(kelvin > 0, "kelvin must be positive")
	return min(max(roundDiv(1_000_000, kelvin), MIRED_MIN), MIRED_MAX)
}
