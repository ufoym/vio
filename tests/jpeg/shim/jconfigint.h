/* jconfigint.h for building libjpeg-turbo's programs against vio's
   jpeglib.h/turbojpeg.h libraries (bind/c/jpeglib). */
#ifndef VIO_TESTS_JCONFIGINT_H
#define VIO_TESTS_JCONFIGINT_H
#include <stdint.h>
#if SIZE_MAX == 0xFFFFFFFFFFFFFFFFULL
#define SIZEOF_SIZE_T  8
#else
#define SIZEOF_SIZE_T  4
#endif
#define PACKAGE_NAME  "vio"
#define VERSION  "3.2.0"
#define BUILD  "vio"
#define HIDDEN
#undef inline
#define INLINE  inline
#if defined(_MSC_VER)
#define THREAD_LOCAL  __declspec(thread)
#else
#define THREAD_LOCAL  _Thread_local
#endif
#if defined(__has_attribute)
#if __has_attribute(fallthrough)
#define FALLTHROUGH  __attribute__((fallthrough));
#else
#define FALLTHROUGH
#endif
#else
#define FALLTHROUGH
#endif
#endif
