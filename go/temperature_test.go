package main

import "testing"

func TestMiredToKelvin(t *testing.T) {
	tests := []struct {
		mired, kelvin int
	}{
		{213, 4700},
		{143, 7000},
		{344, 2900},
		{181, 5500},
		{221, 4500},
		{226, 4400},
		{292, 3400},
		{315, 3150},
		// Exactly 3125K, .5 rounds up
		{320, 3150},
	}

	for _, tt := range tests {
		if got := miredToKelvin(tt.mired); got != tt.kelvin {
			t.Errorf("miredToKelvin(%d) = %d, want %d", tt.mired, got, tt.kelvin)
		}
	}
}

func TestKelvinToMired(t *testing.T) {
	tests := []struct {
		kelvin, mired int
	}{
		{4700, 213},
		{3150, 317},
		{7000, 143},
		{2900, 344},
	}

	for _, tt := range tests {
		if got := kelvinToMired(tt.kelvin); got != tt.mired {
			t.Errorf("kelvinToMired(%d) = %d, want %d", tt.kelvin, got, tt.mired)
		}
	}
}

func TestEveryMultipleOf50RoundTrips(t *testing.T) {
	for kelvin := TEMPERATURE_MIN; kelvin <= TEMPERATURE_MAX; kelvin += 50 {
		if got := miredToKelvin(kelvinToMired(kelvin)); got != kelvin {
			t.Errorf("%dK came back as %dK", kelvin, got)
		}
	}
}
