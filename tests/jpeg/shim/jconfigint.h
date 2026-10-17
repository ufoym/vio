/* Minimal jconfigint.h for building libjpeg-turbo's test programs against
   vio's TurboJPEG compatibility layer. */
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
#define INLINE  inline
#define THREAD_LOCAL  _Thread_local
#endif
