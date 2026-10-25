package main

import "testing"

func TestKelvinAndMiredRoundTrip(t *testing.T) {
	if got := kelvinToMired(4700); got != 213 {
		t.Errorf("kelvinToMired(4700) = %d, want 213", got)
	}

	if got := miredToKelvin(213); got != 4700 {
		t.Errorf("miredToKelvin(213) = %d, want 4700", got)
	}

	// 315 mired is about 3174.6 K, which rounds to 3200 K.
	if got := miredToKelvin(315); got != 3200 {
		t.Errorf("miredToKelvin(315) = %d, want 3200", got)
	}
}
