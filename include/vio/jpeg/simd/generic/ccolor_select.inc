// RGB-family kernel selection, included inside each SIMD namespace.
template <bool Gray, int R, int G, int B, int S>
inline ColorConvertFn rgb_convert_kernel() {
  if constexpr (Gray) return rgb_gray_convert<R, G, B, S>;
  else return rgb_ycc_convert<R, G, B, S>;
}

template <bool Gray>
inline ColorConvertFn rgb_kernel(ColorSpace in) {
  switch (in) {
    case ColorSpace::RGB:
    case ColorSpace::ExtRGB: return rgb_convert_kernel<Gray, 0, 1, 2, 3>();
    case ColorSpace::ExtRGBX:
    case ColorSpace::ExtRGBA: return rgb_convert_kernel<Gray, 0, 1, 2, 4>();
    case ColorSpace::ExtBGR: return rgb_convert_kernel<Gray, 2, 1, 0, 3>();
    case ColorSpace::ExtBGRX:
    case ColorSpace::ExtBGRA: return rgb_convert_kernel<Gray, 2, 1, 0, 4>();
    case ColorSpace::ExtXBGR:
    case ColorSpace::ExtABGR: return rgb_convert_kernel<Gray, 3, 2, 1, 4>();
    case ColorSpace::ExtXRGB:
    case ColorSpace::ExtARGB: return rgb_convert_kernel<Gray, 1, 2, 3, 4>();
    default: return nullptr;
  }
}

inline ColorConvertFn rgb_ycc_kernel(ColorSpace in) { return rgb_kernel<false>(in); }
inline ColorConvertFn rgb_gray_kernel(ColorSpace in) { return rgb_kernel<true>(in); }
