
// Round once: mired 315 converts to 3175K, then 3200K.
internal i64 kelvin_from_mired(i64 mired) {
    Assert(mired > 0);
    return (1'000'000 + 25 * mired) / (50 * mired) * 50;
}

// Clamp the rounded mired value to the light’s range.
internal i64 mired_from_kelvin(i64 kelvin) {
    Assert(kelvin > 0);
    i64 mired = (1'000'000 + kelvin / 2) / kelvin;
    return Clamp(MIRED_MIN, mired, MIRED_MAX);
}
