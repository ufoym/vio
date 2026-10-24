/* jconfig.h - configuration of vio's jpeglib.h-compatible library. */

#ifndef JCONFIG_INCLUDED
#define JCONFIG_INCLUDED

/* API/ABI level: 62 (the default), 70 or 80 select the corresponding
   structure layouts and functions. */
#ifndef JPEG_LIB_VERSION
#define JPEG_LIB_VERSION  62
#endif

/* Version of the jpeglib.h/turbojpeg.h extensions this library provides. */
#define LIBJPEG_TURBO_VERSION  3.2.0
#define LIBJPEG_TURBO_VERSION_NUMBER  3002000

#define C_ARITH_CODING_SUPPORTED  1
#define D_ARITH_CODING_SUPPORTED  1
#define MEM_SRCDST_SUPPORTED  1
#define WITH_SIMD  1

#ifndef BITS_IN_JSAMPLE
#define BITS_IN_JSAMPLE  8
#endif

#ifdef _WIN32

#undef RIGHT_SHIFT_IS_UNSIGNED

/* Use the Windows definitions of boolean, INT16 and INT32. */
#ifndef __RPCNDR_H__
typedef unsigned char boolean;
#endif
#define HAVE_BOOLEAN

#if !(defined(_BASETSD_H_) || defined(_BASETSD_H))
typedef short INT16;
typedef signed int INT32;
#endif
#define XMD_H

#endif /* _WIN32 */

#endif /* JCONFIG_INCLUDED */
