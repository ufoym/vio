// Merged upsampling layout selection, included inside each SIMD namespace.
template <bool V2>
inline MergedUpsampleFn merged_upsample_kernel(ColorSpace out) {
  switch (out) {
    case ColorSpace::RGB:
    case ColorSpace::ExtRGB: return merged_upsample<V2, 0, 1, 2, 3>;
    case ColorSpace::ExtRGBX:
    case ColorSpace::ExtRGBA: return merged_upsample<V2, 0, 1, 2, 4>;
    case ColorSpace::ExtBGR: return merged_upsample<V2, 2, 1, 0, 3>;
    case ColorSpace::ExtBGRX:
    case ColorSpace::ExtBGRA: return merged_upsample<V2, 2, 1, 0, 4>;
    case ColorSpace::ExtXBGR:
    case ColorSpace::ExtABGR: return merged_upsample<V2, 3, 2, 1, 4>;
    case ColorSpace::ExtXRGB:
    case ColorSpace::ExtARGB: return merged_upsample<V2, 1, 2, 3, 4>;
    default: return nullptr;
  }
}
