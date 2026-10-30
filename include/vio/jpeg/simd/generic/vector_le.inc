// Little-endian helpers; byte k is the k-th byte in memory.  Included inside
// each vector type.  interleave_u8 reverses even_u8/odd_u8 for values < 256.
  static VIO_INLINE T widenlo_u8(T a) { return unpacklo_8(a, zero()); }
  static VIO_INLINE T widenhi_u8(T a) { return unpackhi_8(a, zero()); }
  static VIO_INLINE T even_u8(T a) { return and_(a, set1_16(0xFF)); }
  static VIO_INLINE T odd_u8(T a) { return srli_16<8>(a); }
  static VIO_INLINE T interleave_u8(T even, T odd) { return or_(even, slli_16<8>(odd)); }
  static VIO_INLINE T lo16_shl16(T a) { return unpacklo_16(zero(), a); }
  static VIO_INLINE T hi16_shl16(T a) { return unpackhi_16(zero(), a); }
