// Color space conversions.

// Linear to sRGB with the exact piecewise curve, the same one the _SRGB
// hardware formats use. Expects values in [0, 1].
float3 linearToSrgb(float3 c) {
    let low = c * 12.92;
    let high = 1.055 * pow(c, 1.0 / 2.4) - 0.055;
    return select(c <= 0.0031308, low, high);
}

// sRGB to linear, the inverse of linearToSrgb.
float3 srgbToLinear(float3 c) {
    let low = c / 12.92;
    let high = pow((c + 0.055) / 1.055, 2.4);
    return select(c <= 0.04045, low, high);
}
