package main

import "testing"

func expectPanic(t *testing.T, name string, f func()) {
	t.Helper()
	defer func() {
		if recover() == nil {
			t.Errorf("%s did not panic", name)
		}
	}()
	f()
}

func TestKelvinAndMiredRoundTrip(t *testing.T) {
	if got := kelvinToMired(4700); got != 213 {
		t.Errorf("kelvinToMired(4700) = %d, want 213", got)
	}

	if got := miredToKelvin(213); got != 4700 {
		t.Errorf("miredToKelvin(213) = %d, want 4700", got)
	}
}

func TestMiredToKelvinRoundsOnce(t *testing.T) {
	// 315 mired is 3174.6K, below the 3175 midpoint
	if got := miredToKelvin(315); got != 3150 {
		t.Errorf("miredToKelvin(315) = %d, want 3150", got)
	}

	// 314 mired is 3184.7K, above the midpoint
	if got := miredToKelvin(314); got != 3200 {
		t.Errorf("miredToKelvin(314) = %d, want 3200", got)
	}
}

func TestKelvinRangeEnds(t *testing.T) {
	if got := kelvinToMired(2900); got != 345 {
		t.Errorf("kelvinToMired(2900) = %d, want 345", got)
	}
	if got := kelvinToMired(7000); got != 143 {
		t.Errorf("kelvinToMired(7000) = %d, want 143", got)
	}
	if got := miredToKelvin(345); got != 2900 {
		t.Errorf("miredToKelvin(345) = %d, want 2900", got)
	}
	if got := miredToKelvin(143); got != 7000 {
		t.Errorf("miredToKelvin(143) = %d, want 7000", got)
	}
}

func TestConversionAssertsOnNonPositive(t *testing.T) {
	expectPanic(t, "miredToKelvin(0)", func() { miredToKelvin(0) })
	expectPanic(t, "miredToKelvin(-1)", func() { miredToKelvin(-1) })
	expectPanic(t, "kelvinToMired(0)", func() { kelvinToMired(0) })
	expectPanic(t, "kelvinToMired(-1)", func() { kelvinToMired(-1) })
}
