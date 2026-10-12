// vio::jpeg::simd::mmx - MMX kernels for 32-bit x86 CPUs without SSE2: the
// generic kernels instantiated with 64-bit MMX register pairs.  Every entry
// point ends with EMMS so that x87 floating point works afterwards.
#pragma once

#include <mmintrin.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

#include "../../config.hpp"
#include "../../types.hpp"
#include "../common.hpp"

// The helpers below leave MMX state live on purpose (see above).
#if defined(_MSC_VER) && !defined(__clang__)
#pragma warning(push)
#pragma warning(disable : 4799)
#endif
VIO_TARGET_PUSH_("mmx")
namespace vio::jpeg::simd::mmx {

namespace impl {
using ColorConvertFn = void (*)(unsigned, std::uint8_t**, std::uint8_t***, unsigned, int);
using ColorDeconvertFn = void (*)(unsigned, std::uint8_t***, unsigned, std::uint8_t**, int);
using MergedUpsampleFn = void (*)(unsigned, std::uint8_t***, unsigned, std::uint8_t**);
using UpsampleFn = void (*)(int, unsigned, std::uint8_t**, std::uint8_t**);

#include "v64.inc"
#include "../generic/ccolor.inc"
#include "../generic/csample.inc"
#include "../generic/dcolor.inc"
#include "../generic/dsample.inc"
#include "../generic/dct.inc"
}  // namespace impl

// F followed by EMMS.
template <auto F>
struct Emms;
template <class... A, void (*F)(A...)>
struct Emms<F> {
  static void call(A... a) {
    F(a...);
    _mm_empty();
  }
};

using impl::ColorConvertFn;
using impl::ColorDeconvertFn;
using impl::MergedUpsampleFn;
using impl::UpsampleFn;

#define VIO_MMX_RGB_SWITCH(fn, ...)                                                    \
  switch (cs) {                                                                        \
    case ColorSpace::RGB:                                                              \
    case ColorSpace::ExtRGB: return Emms<impl::fn<__VA_ARGS__ 0, 1, 2, 3>>::call;      \
    case ColorSpace::ExtRGBX:                                                          \
    case ColorSpace::ExtRGBA: return Emms<impl::fn<__VA_ARGS__ 0, 1, 2, 4>>::call;     \
    case ColorSpace::ExtBGR: return Emms<impl::fn<__VA_ARGS__ 2, 1, 0, 3>>::call;      \
    case ColorSpace::ExtBGRX:                                                          \
    case ColorSpace::ExtBGRA: return Emms<impl::fn<__VA_ARGS__ 2, 1, 0, 4>>::call;     \
    case ColorSpace::ExtXBGR:                                                          \
    case ColorSpace::ExtABGR: return Emms<impl::fn<__VA_ARGS__ 3, 2, 1, 4>>::call;     \
    case ColorSpace::ExtXRGB:                                                          \
    case ColorSpace::ExtARGB: return Emms<impl::fn<__VA_ARGS__ 1, 2, 3, 4>>::call;     \
    default: return nullptr;                                                           \
  }

inline ColorConvertFn rgb_ycc_kernel(ColorSpace cs) { VIO_MMX_RGB_SWITCH(rgb_ycc_convert) }
inline ColorConvertFn rgb_gray_kernel(ColorSpace cs) { VIO_MMX_RGB_SWITCH(rgb_gray_convert) }
inline ColorDeconvertFn ycc_rgb_kernel(ColorSpace cs) { VIO_MMX_RGB_SWITCH(ycc_rgb_convert) }
template <bool V2>
inline MergedUpsampleFn merged_upsample_kernel(ColorSpace cs) { VIO_MMX_RGB_SWITCH(merged_upsample, V2,) }
#undef VIO_MMX_RGB_SWITCH

inline UpsampleFn upsample_kernel(int kind) {
  switch (kind) {
    case 0: return Emms<impl::h2v1_upsample>::call;
    case 1: return Emms<impl::h2v2_upsample>::call;
    case 2: return Emms<impl::h2v1_fancy_upsample>::call;
    case 3: return Emms<impl::h2v2_fancy_upsample>::call;
    default: return nullptr;
  }
}
inline constexpr auto h2v1_downsample = Emms<impl::h2v1_downsample>::call;
inline constexpr auto h2v2_downsample = Emms<impl::h2v2_downsample>::call;
inline constexpr auto convsamp = Emms<impl::convsamp>::call;
inline constexpr auto fdct_islow = Emms<impl::fdct_islow>::call;
inline constexpr auto fdct_ifast = Emms<impl::fdct_ifast>::call;
inline constexpr auto quantize = Emms<impl::quantize>::call;
inline constexpr auto idct_islow = Emms<impl::idct_islow>::call;
inline constexpr auto idct_ifast = Emms<impl::idct_ifast>::call;
inline constexpr auto idct_4x4 = Emms<impl::idct_4x4>::call;
inline constexpr auto idct_2x2 = Emms<impl::idct_2x2>::call;

}  // namespace vio::jpeg::simd::mmx
VIO_TARGET_POP()
#if defined(_MSC_VER) && !defined(__clang__)
#pragma warning(pop)
#endif
