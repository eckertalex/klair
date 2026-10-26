package conformance

Light_State :: struct {
	on:          int,
	brightness:  int,
	temperature: int, // mired
}

DEFAULT_STATE :: Light_State{on = 1, brightness = 20, temperature = 213}
