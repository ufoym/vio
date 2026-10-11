// vio::jpeg - forward DCT kernels and quantization.
#pragma once

#include <cmath>

#include "common.hpp"
#include "../simd/dispatch.hpp"

namespace vio::jpeg::detail {

// DCT elements: 32 bits for 8-bit samples, 64 bits otherwise.
template <class T>
using DctElem = std::conditional_t<kBits<T> == 8, int, JLong>;

template <class T>
using FdctFn = void (*)(DctElem<T>* data);
using FdctFloatFn = void (*)(float* data);

namespace fdct {

inline constexpr JLong FIX_0_298631336 = 2446, FIX_0_390180644 = 3196, FIX_0_541196100 = 4433,
                       FIX_0_765366865 = 6270, FIX_0_899976223 = 7373, FIX_1_175875602 = 9633,
                       FIX_1_501321110 = 12299, FIX_1_847759065 = 15137,
                       FIX_1_961570560 = 16069, FIX_2_053119869 = 16819,
                       FIX_2_562915447 = 20995, FIX_3_072711026 = 25172;

VIO_INLINE constexpr JLong descale(JLong x, int n) { return (x + (JLong(1) << (n - 1))) >> n; }
VIO_INLINE constexpr JLong lsh(JLong x, int n) {
  return static_cast<JLong>(static_cast<unsigned long long>(x) << n);
}

template <class T>
void fdct_islow(DctElem<T>* data) {
  using E = DctElem<T>;
  constexpr int CONST_BITS = 13;
  constexpr int PASS1_BITS = kBits<T> == 8 ? 2 : 1;
  E* dataptr = data;
  for (int ctr = DCTSIZE - 1; ctr >= 0; ctr--) {
    JLong tmp0 = dataptr[0] + dataptr[7];
    JLong tmp7 = dataptr[0] - dataptr[7];
    JLong tmp1 = dataptr[1] + dataptr[6];
    JLong tmp6 = dataptr[1] - dataptr[6];
    JLong tmp2 = dataptr[2] + dataptr[5];
    JLong tmp5 = dataptr[2] - dataptr[5];
    JLong tmp3 = dataptr[3] + dataptr[4];
    JLong tmp4 = dataptr[3] - dataptr[4];
    JLong tmp10 = tmp0 + tmp3, tmp13 = tmp0 - tmp3;
    JLong tmp11 = tmp1 + tmp2, tmp12 = tmp1 - tmp2;
    dataptr[0] = static_cast<E>(lsh(tmp10 + tmp11, PASS1_BITS));
    dataptr[4] = static_cast<E>(lsh(tmp10 - tmp11, PASS1_BITS));
    JLong z1 = (tmp12 + tmp13) * FIX_0_541196100;
    dataptr[2] = static_cast<E>(descale(z1 + tmp13 * FIX_0_765366865, CONST_BITS - PASS1_BITS));
    dataptr[6] = static_cast<E>(descale(z1 + tmp12 * -FIX_1_847759065, CONST_BITS - PASS1_BITS));
    z1 = tmp4 + tmp7;
    JLong z2 = tmp5 + tmp6, z3 = tmp4 + tmp6, z4 = tmp5 + tmp7;
    JLong z5 = (z3 + z4) * FIX_1_175875602;
    tmp4 = tmp4 * FIX_0_298631336;
    tmp5 = tmp5 * FIX_2_053119869;
    tmp6 = tmp6 * FIX_3_072711026;
    tmp7 = tmp7 * FIX_1_501321110;
    z1 = z1 * -FIX_0_899976223;
    z2 = z2 * -FIX_2_562915447;
    z3 = z3 * -FIX_1_961570560;
    z4 = z4 * -FIX_0_390180644;
    z3 += z5;
    z4 += z5;
    dataptr[7] = static_cast<E>(descale(tmp4 + z1 + z3, CONST_BITS - PASS1_BITS));
    dataptr[5] = static_cast<E>(descale(tmp5 + z2 + z4, CONST_BITS - PASS1_BITS));
    dataptr[3] = static_cast<E>(descale(tmp6 + z2 + z3, CONST_BITS - PASS1_BITS));
    dataptr[1] = static_cast<E>(descale(tmp7 + z1 + z4, CONST_BITS - PASS1_BITS));
    dataptr += DCTSIZE;
  }
  dataptr = data;
  for (int ctr = DCTSIZE - 1; ctr >= 0; ctr--) {
    JLong tmp0 = dataptr[DCTSIZE * 0] + dataptr[DCTSIZE * 7];
    JLong tmp7 = dataptr[DCTSIZE * 0] - dataptr[DCTSIZE * 7];
    JLong tmp1 = dataptr[DCTSIZE * 1] + dataptr[DCTSIZE * 6];
    JLong tmp6 = dataptr[DCTSIZE * 1] - dataptr[DCTSIZE * 6];
    JLong tmp2 = dataptr[DCTSIZE * 2] + dataptr[DCTSIZE * 5];
    JLong tmp5 = dataptr[DCTSIZE * 2] - dataptr[DCTSIZE * 5];
    JLong tmp3 = dataptr[DCTSIZE * 3] + dataptr[DCTSIZE * 4];
    JLong tmp4 = dataptr[DCTSIZE * 3] - dataptr[DCTSIZE * 4];
    JLong tmp10 = tmp0 + tmp3, tmp13 = tmp0 - tmp3;
    JLong tmp11 = tmp1 + tmp2, tmp12 = tmp1 - tmp2;
    dataptr[DCTSIZE * 0] = static_cast<E>(descale(tmp10 + tmp11, PASS1_BITS));
    dataptr[DCTSIZE * 4] = static_cast<E>(descale(tmp10 - tmp11, PASS1_BITS));
    JLong z1 = (tmp12 + tmp13) * FIX_0_541196100;
    dataptr[DCTSIZE * 2] =
        static_cast<E>(descale(z1 + tmp13 * FIX_0_765366865, CONST_BITS + PASS1_BITS));
    dataptr[DCTSIZE * 6] =
        static_cast<E>(descale(z1 + tmp12 * -FIX_1_847759065, CONST_BITS + PASS1_BITS));
    z1 = tmp4 + tmp7;
    JLong z2 = tmp5 + tmp6, z3 = tmp4 + tmp6, z4 = tmp5 + tmp7;
    JLong z5 = (z3 + z4) * FIX_1_175875602;
    tmp4 = tmp4 * FIX_0_298631336;
    tmp5 = tmp5 * FIX_2_053119869;
    tmp6 = tmp6 * FIX_3_072711026;
    tmp7 = tmp7 * FIX_1_501321110;
    z1 = z1 * -FIX_0_899976223;
    z2 = z2 * -FIX_2_562915447;
    z3 = z3 * -FIX_1_961570560;
    z4 = z4 * -FIX_0_390180644;
    z3 += z5;
    z4 += z5;
    dataptr[DCTSIZE * 7] = static_cast<E>(descale(tmp4 + z1 + z3, CONST_BITS + PASS1_BITS));
    dataptr[DCTSIZE * 5] = static_cast<E>(descale(tmp5 + z2 + z4, CONST_BITS + PASS1_BITS));
    dataptr[DCTSIZE * 3] = static_cast<E>(descale(tmp6 + z2 + z3, CONST_BITS + PASS1_BITS));
    dataptr[DCTSIZE * 1] = static_cast<E>(descale(tmp7 + z1 + z4, CONST_BITS + PASS1_BITS));
    dataptr++;
  }
}

template <class T>
void fdct_ifast(DctElem<T>* data) {
  using E = DctElem<T>;
  constexpr JLong FIX_0_382683433 = 98, FIX_0_541196100 = 139, FIX_0_707106781 = 181,
                  FIX_1_306562965 = 334;
  auto mul = [](JLong var, JLong c) { return static_cast<E>((var * c) >> 8); };
  for (int pass = 0; pass < 2; pass++) {
    E* dataptr = data;
    const int s = pass == 0 ? 1 : DCTSIZE;   // element stride
    const int adv = pass == 0 ? DCTSIZE : 1;  // vector advance
    for (int ctr = DCTSIZE - 1; ctr >= 0; ctr--) {
      E tmp0 = dataptr[0 * s] + dataptr[7 * s];
      E tmp7 = dataptr[0 * s] - dataptr[7 * s];
      E tmp1 = dataptr[1 * s] + dataptr[6 * s];
      E tmp6 = dataptr[1 * s] - dataptr[6 * s];
      E tmp2 = dataptr[2 * s] + dataptr[5 * s];
      E tmp5 = dataptr[2 * s] - dataptr[5 * s];
      E tmp3 = dataptr[3 * s] + dataptr[4 * s];
      E tmp4 = dataptr[3 * s] - dataptr[4 * s];
      E tmp10 = tmp0 + tmp3, tmp13 = tmp0 - tmp3;
      E tmp11 = tmp1 + tmp2, tmp12 = tmp1 - tmp2;
      dataptr[0 * s] = tmp10 + tmp11;
      dataptr[4 * s] = tmp10 - tmp11;
      E z1 = mul(tmp12 + tmp13, FIX_0_707106781);
      dataptr[2 * s] = tmp13 + z1;
      dataptr[6 * s] = tmp13 - z1;
      tmp10 = tmp4 + tmp5;
      tmp11 = tmp5 + tmp6;
      tmp12 = tmp6 + tmp7;
      E z5 = mul(tmp10 - tmp12, FIX_0_382683433);
      E z2 = mul(tmp10, FIX_0_541196100) + z5;
      E z4 = mul(tmp12, FIX_1_306562965) + z5;
      E z3 = mul(tmp11, FIX_0_707106781);
      E z11 = tmp7 + z3, z13 = tmp7 - z3;
      dataptr[5 * s] = z13 + z2;
      dataptr[3 * s] = z13 - z2;
      dataptr[1 * s] = z11 + z4;
      dataptr[7 * s] = z11 - z4;
      dataptr += adv;
    }
  }
}

VIO_NO_FP_CONTRACT inline void fdct_float(float* data) {
  VIO_FP_CONTRACT_OFF
  for (int pass = 0; pass < 2; pass++) {
    float* dataptr = data;
    const int s = pass == 0 ? 1 : DCTSIZE;
    const int adv = pass == 0 ? DCTSIZE : 1;
    for (int ctr = DCTSIZE - 1; ctr >= 0; ctr--) {
      float tmp0 = dataptr[0 * s] + dataptr[7 * s];
      float tmp7 = dataptr[0 * s] - dataptr[7 * s];
      float tmp1 = dataptr[1 * s] + dataptr[6 * s];
      float tmp6 = dataptr[1 * s] - dataptr[6 * s];
      float tmp2 = dataptr[2 * s] + dataptr[5 * s];
      float tmp5 = dataptr[2 * s] - dataptr[5 * s];
      float tmp3 = dataptr[3 * s] + dataptr[4 * s];
      float tmp4 = dataptr[3 * s] - dataptr[4 * s];
      float tmp10 = tmp0 + tmp3, tmp13 = tmp0 - tmp3;
      float tmp11 = tmp1 + tmp2, tmp12 = tmp1 - tmp2;
      dataptr[0 * s] = tmp10 + tmp11;
      dataptr[4 * s] = tmp10 - tmp11;
      float z1 = (tmp12 + tmp13) * 0.707106781f;
      dataptr[2 * s] = tmp13 + z1;
      dataptr[6 * s] = tmp13 - z1;
      tmp10 = tmp4 + tmp5;
      tmp11 = tmp5 + tmp6;
      tmp12 = tmp6 + tmp7;
      float z5 = (tmp10 - tmp12) * 0.382683433f;
      float z2 = std::fma(0.541196100f, tmp10, z5);
      float z4 = std::fma(1.306562965f, tmp12, z5);
      float z3 = tmp11 * 0.707106781f;
      float z11 = tmp7 + z3, z13 = tmp7 - z3;
      dataptr[5 * s] = z13 + z2;
      dataptr[3 * s] = z13 - z2;
      dataptr[1 * s] = z11 + z4;
      dataptr[7 * s] = z11 - z4;
      dataptr += adv;
    }
  }
}

}  // namespace fdct

// ---------------------------------------------------------------------------
// Forward DCT manager
template <class T>
class ForwardDct {
 public:
  using E = DctElem<T>;

  ForwardDct(DctMethod method, const simd::Support& support) : method_(method) {
    switch (method) {
      case DctMethod::IntSlow: dct_ = fdct::fdct_islow<T>; break;
      case DctMethod::IntFast: dct_ = fdct::fdct_ifast<T>; break;
      case DctMethod::Float: float_dct_ = fdct::fdct_float; break;
    }
    if constexpr (kBits<T> == 8) {
      simd_ = simd::fdct_kernels(support);
      dct16_ = method == DctMethod::IntSlow ? simd_.fdct_islow : simd_.fdct_ifast;
      // Any SIMD kernel switches the integer path to 16-bit DCT elements;
      // missing kernels fall back to scalar code.
      use16_ = method != DctMethod::Float && (simd_.convsamp || dct16_ || simd_.quantize);
    }
  }

  // Compute divisors for the given quantization tables (start_pass_fdctmgr).
  void start_pass(const std::array<std::optional<QuantTable>, NUM_QUANT_TBLS>& qtbls,
                  const int* quant_tbl_nos, int num_components) {
    for (int ci = 0; ci < num_components; ci++) {
      int qtblno = quant_tbl_nos[ci];
      if (qtblno < 0 || qtblno >= NUM_QUANT_TBLS || !qtbls[qtblno])
        VIO_FAIL("Quantization table 0x%02x was not defined", qtblno);
      const QuantTable& q = *qtbls[qtblno];
      static constexpr std::int16_t aanscales[DCTSIZE2] = {
          16384, 22725, 21407, 19266, 16384, 12873, 8867,  4520,  22725, 31521, 29692,
          26722, 22725, 17855, 12299, 6270,  21407, 29692, 27969, 25172, 21407, 16819,
          11585, 5906,  19266, 26722, 25172, 22654, 19266, 15137, 10426, 5315,  16384,
          22725, 21407, 19266, 16384, 12873, 8867,  4520,  12873, 17855, 16819, 15137,
          12873, 10114, 6967,  3552,  8867,  12299, 11585, 10426, 8867,  6967,  4799,
          2446,  4520,  6270,  5906,  5315,  4520,  3552,  2446,  1247};
      if constexpr (kBits<T> == 8) {
        if (use16_ && simd_.quantize) {
          bool ok = true;
          for (int i = 0; i < DCTSIZE2; i++) {
            std::uint16_t d = method_ == DctMethod::IntSlow
                                  ? static_cast<std::uint16_t>(q.quantval[i] << 3)
                                  : static_cast<std::uint16_t>(fdct::descale(
                                        static_cast<JLong>(q.quantval[i]) * aanscales[i], 14 - 3));
            ok &= compute_reciprocal16(d, &divisors16_[qtblno][i]);
          }
          // Divisors the 16-bit quantizer cannot represent exactly fall back
          // to the scalar quantizer for this table.
          quantize16_ok_[qtblno] = ok;
        }
      }
      switch (method_) {
        case DctMethod::IntSlow:
          for (int i = 0; i < DCTSIZE2; i++) {
            if constexpr (kBits<T> == 8)
              compute_reciprocal(static_cast<std::uint16_t>(q.quantval[i] << 3),
                                 &divisors_[qtblno][i]);
            else
              divisors_[qtblno][i] = static_cast<E>(q.quantval[i]) << 3;
          }
          break;
        case DctMethod::IntFast:
          for (int i = 0; i < DCTSIZE2; i++) {
            JLong d = fdct::descale(static_cast<JLong>(q.quantval[i]) * aanscales[i], 14 - 3);
            if constexpr (kBits<T> == 8)
              compute_reciprocal(static_cast<std::uint16_t>(d), &divisors_[qtblno][i]);
            else
              divisors_[qtblno][i] = static_cast<E>(d);
          }
          break;
        case DctMethod::Float: {
          static constexpr double aanscalefactor[DCTSIZE] = {
              1.0, 1.387039845, 1.306562965, 1.175875602, 1.0, 0.785694958, 0.541196100,
              0.275899379};
          int i = 0;
          for (int row = 0; row < DCTSIZE; row++)
            for (int col = 0; col < DCTSIZE; col++, i++)
              float_divisors_[qtblno][i] = static_cast<float>(
                  1.0 / (static_cast<double>(q.quantval[i]) * aanscalefactor[row] *
                         aanscalefactor[col] * 8.0));
          break;
        }
      }
    }
  }

  // forward_DCT(): transform num_blocks blocks starting at (start_row, start_col)
  void forward_dct(int qtblno, T** sample_data, Block* coef_blocks, unsigned start_row,
                   unsigned start_col, unsigned num_blocks) {
    sample_data += start_row;
    if (method_ == DctMethod::Float) {
      const float* divisors = float_divisors_[qtblno];
      for (unsigned bi = 0; bi < num_blocks; bi++, start_col += DCTSIZE) {
        if constexpr (kBits<T> == 8) {
          if (simd_.convsamp_float) simd_.convsamp_float(sample_data, start_col, float_workspace_);
          else float_convsamp(sample_data, start_col);
          if (simd_.fdct_float) simd_.fdct_float(float_workspace_);
          else float_dct_(float_workspace_);
          if (simd_.quantize_float)
            simd_.quantize_float(coef_blocks[bi].data(), divisors, float_workspace_);
          else
            quantize_float(coef_blocks[bi].data(), divisors, float_workspace_);
        } else {
          float_convsamp(sample_data, start_col);
          float_dct_(float_workspace_);
          quantize_float(coef_blocks[bi].data(), divisors, float_workspace_);
        }
      }
      return;
    }
    const E* divisors = divisors_[qtblno];
    if constexpr (kBits<T> == 8) {
      if (use16_) {
        const bool simd_quantize = simd_.quantize && quantize16_ok_[qtblno];
        for (unsigned bi = 0; bi < num_blocks; bi++, start_col += DCTSIZE) {
          if (simd_.convsamp) {
            simd_.convsamp(sample_data, start_col, workspace16_);
          } else {
            std::int16_t* ws = workspace16_;
            for (int r = 0; r < DCTSIZE; r++)
              for (int c = 0; c < DCTSIZE; c++)
                *ws++ = static_cast<std::int16_t>(sample_data[r][start_col + c] - kCenterSample<T>);
          }
          if (dct16_) {
            dct16_(workspace16_);
          } else {
            for (int i = 0; i < DCTSIZE2; i++) workspace_[i] = workspace16_[i];
            dct_(workspace_);
            for (int i = 0; i < DCTSIZE2; i++) workspace16_[i] = static_cast<std::int16_t>(workspace_[i]);
          }
          if (simd_quantize) {
            simd_.quantize(coef_blocks[bi].data(), divisors16_[qtblno], workspace16_);
          } else {
            quantize(coef_blocks[bi].data(), divisors, workspace16_);
          }
        }
        return;
      }
    }
    for (unsigned bi = 0; bi < num_blocks; bi++, start_col += DCTSIZE) {
      E* ws = workspace_;
      for (int r = 0; r < DCTSIZE; r++) {
        const T* elemptr = sample_data[r] + start_col;
        for (int c = 0; c < DCTSIZE; c++) *ws++ = static_cast<E>(elemptr[c] - kCenterSample<T>);
      }
      dct_(workspace_);
      quantize(coef_blocks[bi].data(), divisors, workspace_);
    }
  }

  FdctFn<T> dct_ = nullptr;
  FdctFloatFn float_dct_ = nullptr;

 private:
  static int flss(std::uint16_t val) {
    int bit = 16;
    if (!val) return 0;
    if (!(val & 0xff00)) { bit -= 8; val = static_cast<std::uint16_t>(val << 8); }
    if (!(val & 0xf000)) { bit -= 4; val = static_cast<std::uint16_t>(val << 4); }
    if (!(val & 0xc000)) { bit -= 2; val = static_cast<std::uint16_t>(val << 2); }
    if (!(val & 0x8000)) { bit -= 1; }
    return bit;
  }

  // 8-bit: reciprocal/correction/scale/shift layout (DCTELEM == 32 bits)
  static void compute_reciprocal(std::uint16_t divisor, E* dtbl) {
    using U2 = unsigned long long;
    constexpr int ebits = 32;
    if (divisor <= 1) {
      dtbl[DCTSIZE2 * 0] = 1;
      dtbl[DCTSIZE2 * 1] = 0;
      dtbl[DCTSIZE2 * 2] = 1;
      dtbl[DCTSIZE2 * 3] = -ebits;
      return;
    }
    int b = flss(divisor) - 1;
    int r = ebits + b;
    U2 fq = (U2(1) << r) / divisor;
    U2 fr = (U2(1) << r) % divisor;
    unsigned c = divisor / 2;
    if (fr == 0) {
      fq >>= 1;
      r--;
    } else if (fr <= divisor / 2u) {
      c++;
    } else {
      fq++;
    }
    dtbl[DCTSIZE2 * 0] = static_cast<E>(fq);
    dtbl[DCTSIZE2 * 1] = static_cast<E>(c);
    dtbl[DCTSIZE2 * 2] = 1;
    dtbl[DCTSIZE2 * 3] = static_cast<E>(r - ebits);
  }

  // Reciprocal for the SIMD (16-bit DCT element) quantizer; returns false
  // when it cannot represent the divisor exactly.
  static bool compute_reciprocal16(std::uint16_t divisor, std::uint16_t* dtbl) {
    if (divisor <= 1) {
      dtbl[DCTSIZE2 * 0] = 1;
      dtbl[DCTSIZE2 * 1] = 0;
      dtbl[DCTSIZE2 * 2] = 1;
      dtbl[DCTSIZE2 * 3] = static_cast<std::uint16_t>(-16);
      return false;
    }
    int b = flss(divisor) - 1;
    int r = 16 + b;
    unsigned fq = (1u << r) / divisor;
    unsigned fr = (1u << r) % divisor;
    unsigned c = divisor / 2;
    if (fr == 0) {
      fq >>= 1;
      r--;
    } else if (fr <= divisor / 2u) {
      c++;
    } else {
      fq++;
    }
    dtbl[DCTSIZE2 * 0] = static_cast<std::uint16_t>(fq);
    dtbl[DCTSIZE2 * 1] = static_cast<std::uint16_t>(c);
    dtbl[DCTSIZE2 * 2] = static_cast<std::uint16_t>(1u << (32 - r));
    dtbl[DCTSIZE2 * 3] = static_cast<std::uint16_t>(r - 16);
    return r > 16;
  }

  // W: DCT element type of the workspace (E, or int16_t on the SIMD path; the
  // latter is read directly rather than copied, which also sidesteps a GCC
  // auto-vectorization miscompile of that widening copy on Loongson MMI).
  template <class W>
  static void quantize(Coef* output_ptr, const E* divisors, const W* workspace) {
    if constexpr (kBits<T> == 8) {
      for (int i = 0; i < DCTSIZE2; i++) {
        E temp = static_cast<E>(workspace[i]);
        unsigned recip = static_cast<unsigned>(divisors[i + DCTSIZE2 * 0]);
        unsigned corr = static_cast<unsigned>(divisors[i + DCTSIZE2 * 1]);
        int shift = divisors[i + DCTSIZE2 * 3];
        unsigned long long product;
        if (temp < 0) {
          temp = -temp;
          product = static_cast<unsigned long long>(static_cast<unsigned>(temp) + corr) * recip;
          product >>= shift + 32;
          temp = static_cast<E>(product);
          temp = -temp;
        } else {
          product = static_cast<unsigned long long>(static_cast<unsigned>(temp) + corr) * recip;
          product >>= shift + 32;
          temp = static_cast<E>(product);
        }
        output_ptr[i] = static_cast<Coef>(temp);
      }
    } else {
      for (int i = 0; i < DCTSIZE2; i++) {
        E qval = divisors[i];
        E temp = static_cast<E>(workspace[i]);
        if (temp < 0) {
          temp = -temp;
          temp += qval >> 1;
          if (temp >= qval) temp /= qval;
          else temp = 0;
          temp = -temp;
        } else {
          temp += qval >> 1;
          if (temp >= qval) temp /= qval;
          else temp = 0;
        }
        output_ptr[i] = static_cast<Coef>(temp);
      }
    }
  }

  void float_convsamp(T** sample_data, unsigned start_col) {
    float* ws = float_workspace_;
    for (int r = 0; r < DCTSIZE; r++) {
      const T* elemptr = sample_data[r] + start_col;
      for (int c = 0; c < DCTSIZE; c++) *ws++ = static_cast<float>(elemptr[c] - kCenterSample<T>);
    }
  }

  VIO_NO_FP_CONTRACT static void quantize_float(Coef* output_ptr, const float* divisors,
                                                const float* workspace) {
    VIO_FP_CONTRACT_OFF
    for (int i = 0; i < DCTSIZE2; i++) {
      float temp = workspace[i] * divisors[i];
      output_ptr[i] = static_cast<Coef>(static_cast<int>(temp + 16384.5f) - 16384);
    }
  }

  DctMethod method_;
  alignas(32) E divisors_[NUM_QUANT_TBLS][DCTSIZE2 * 4]{};
  alignas(32) float float_divisors_[NUM_QUANT_TBLS][DCTSIZE2]{};
  alignas(32) E workspace_[DCTSIZE2]{};
  alignas(32) float float_workspace_[DCTSIZE2]{};
  simd::FdctKernels simd_{};
  void (*dct16_)(std::int16_t*) = nullptr;
  bool use16_ = false;
  alignas(32) std::uint16_t divisors16_[NUM_QUANT_TBLS][DCTSIZE2 * 4]{};
  bool quantize16_ok_[NUM_QUANT_TBLS]{};
  alignas(32) std::int16_t workspace16_[DCTSIZE2]{};
};

}  // namespace vio::jpeg::detail
