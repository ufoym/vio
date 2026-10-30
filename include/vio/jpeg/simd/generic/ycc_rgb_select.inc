// YCbCr output layout selection, included inside each SIMD namespace.
inline ColorDeconvertFn ycc_rgb_kernel(ColorSpace out) {
  switch (out) {
    case ColorSpace::RGB:
    case ColorSpace::ExtRGB: return ycc_rgb_convert<0, 1, 2, 3>;
    case ColorSpace::ExtRGBX:
    case ColorSpace::ExtRGBA: return ycc_rgb_convert<0, 1, 2, 4>;
    case ColorSpace::ExtBGR: return ycc_rgb_convert<2, 1, 0, 3>;
    case ColorSpace::ExtBGRX:
    case ColorSpace::ExtBGRA: return ycc_rgb_convert<2, 1, 0, 4>;
    case ColorSpace::ExtXBGR:
    case ColorSpace::ExtABGR: return ycc_rgb_convert<3, 2, 1, 4>;
    case ColorSpace::ExtXRGB:
    case ColorSpace::ExtARGB: return ycc_rgb_convert<1, 2, 3, 4>;
#if VIO_YCC_RGB565
    case ColorSpace::RGB565: return ycc_rgb_convert<0, 1, 2, 2>;
#endif
    default: return nullptr;
  }
}
