/* Minimal stand-in for libjpeg-turbo's cdjpeg.h, providing only what
   tjbench.c needs when built for IJG libjpeg (jpeg-bench-libjpeg), whose
   jpeglib.h lacks what the real cdjpeg.h uses. */
#ifndef VIO_BENCH_CDJPEG_H
#define VIO_BENCH_CDJPEG_H
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jconfigint.h"
#define JMSG_LENGTH_MAX  200
#endif
